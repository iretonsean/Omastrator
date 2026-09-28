#include "UI/DeviceSend.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <functional>

namespace {
// A window that has just opened needs a few seconds to hear anyone.
constexpr int listAttempts = 10;
constexpr int listRetryMs = 1000;

// Tests shorten the wait between looks.
int retryMs()
{
    return qEnvironmentVariableIntValue("OMASTRATOR_DEVICE_RETRY_MS") > 0 ? qEnvironmentVariableIntValue("OMASTRATOR_DEVICE_RETRY_MS") : listRetryMs;
}
// Asking each device its name takes 15 to 20 seconds.
constexpr int nameLimitMs = 45'000;
constexpr int quickLimitMs = 15'000;
// omadrop waits 45 seconds for a sleeping iPhone's receiver, and then the transfer.
constexpr int sendLimitMs = 120'000;

QString findProgram(const char *variable, const QString &name, const QStringList &extra)
{
    // An override that isn't there means the program is missing, so a test can say so without finding the real one.
    if (const QString overridden = qEnvironmentVariable(variable); !overridden.isEmpty())
        return QFileInfo::exists(overridden) ? overridden : QString();
    if (const QString found = QStandardPaths::findExecutable(name); !found.isEmpty())
        return found;
    // The daemon may start without ~/.local/bin on its PATH.
    for (const QString &path : extra) {
        if (QFileInfo info(QDir::home().filePath(path)); info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return {};
}

QString lastLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.back().trimmed();
}
}

QString DeviceSend::Device::label() const
{
    if (!name.isEmpty())
        return name;
    return signal ? QStringLiteral("Apple device (%1 dBm)").arg(*signal) : QStringLiteral("Apple device");
}

DeviceSend::DeviceSend(QObject *parent) : QObject(parent) {}

DeviceSend::~DeviceSend()
{
    // Nothing it started may outlive the window.
    for (QProcess *process : {m_searchProcess.data(), m_sendProcess.data()}) {
        if (process) {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(500);
        }
    }
}

QString DeviceSend::omdropProgram()
{
    return findProgram("OMASTRATOR_OMDROP", QStringLiteral("omdrop"),
                       {QStringLiteral(".local/bin/omdrop"), QStringLiteral(".config/omarchy/plugins/netmojo.omdrop/bin/omdrop")});
}

QString DeviceSend::omadropProgram()
{
    return findProgram("OMASTRATOR_OMADROP", QStringLiteral("omadrop"), {QStringLiteral(".local/bin/omadrop")});
}

QString DeviceSend::missingText()
{
    return QStringLiteral("AirDrop isn't set up on this computer. Install the omdrop plugin (omarchy plugin add "
                          "https://github.com/brentkearney/omdrop-plugin.git), then try again.");
}

std::vector<DeviceSend::Device> DeviceSend::parsePeers(const QByteArray &json)
{
    std::vector<Device> found;
    const auto add = [&found](const QJsonValue &value) {
        const QJsonObject peer = value.toObject();
        const QString address = peer.value(QLatin1String("mac")).toString();
        if (address.isEmpty())
            return;
        Device device;
        device.address = address;
        device.name = peer.value(QLatin1String("name")).toString().trimmed();
        if (peer.value(QLatin1String("rssi")).isDouble())
            device.signal = peer.value(QLatin1String("rssi")).toInt();
        found.push_back(device);
    };
    const QJsonDocument whole = QJsonDocument::fromJson(json.trimmed());
    if (whole.isArray()) {
        for (const QJsonValue &value : whole.array())
            add(value);
        return found;
    }
    // --stream: one object a line.
    for (const QByteArray &line : json.split('\n')) {
        if (const QJsonDocument each = QJsonDocument::fromJson(line.trimmed()); each.isObject())
            add(each.object());
    }
    return found;
}

QString DeviceSend::failureText(int exitCode, const QString &output, const QString &name)
{
    const QString text = output.toLower();
    if (text.contains(QLatin1String("declin")))
        return QStringLiteral("%1 declined it.").arg(name);
    // The phone didn't wake: almost always Everyone for 10 Minutes has lapsed back to Contacts Only.
    if (text.contains(QLatin1String("never opened")) || text.contains(QLatin1String("never answered")))
        return QStringLiteral("%1 didn't answer. Set AirDrop to Everyone for 10 Minutes, unlock it, then send again.").arg(name);
    if (text.contains(QLatin1String("no iphone nearby")) || text.contains(QLatin1String("no device")))
        return QStringLiteral("%1 isn't nearby any more. Unlock it and open its share sheet, then choose it again.").arg(name);
    if (exitCode == 0 && !text.contains(QLatin1String("fail")))
        return {};
    const QString line = lastLine(output);
    return line.isEmpty() ? QStringLiteral("The send to %1 failed.").arg(name) : QStringLiteral("The send to %1 failed: %2").arg(name, line);
}

void DeviceSend::run(const QString &program, const QStringList &arguments, int limitMs, Done done)
{
    auto *process = new QProcess(this);
    auto *limit = new QTimer(process);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, process, [process] { process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done(-1, QString(), QStringLiteral("Couldn't start %1.").arg(QFileInfo(process->program()).fileName()));
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        QString err = QString::fromUtf8(process->readAllStandardError());
        if (status == QProcess::CrashExit)
            err += QStringLiteral("\nIt didn't finish.");
        done(status == QProcess::CrashExit ? -1 : code, out, err);
        process->deleteLater();
    });
    m_searchProcess = process;
    process->start(program, arguments);
    limit->start(limitMs);
}

void DeviceSend::refresh()
{
    if (m_searching || m_sending)
        return;
    ++m_generation;
    m_problem.clear();
    m_attempts = 0;
    m_turnedOn = false;
    if (omdropProgram().isEmpty()) {
        m_devices.clear();
        m_problem = missingText();
        emit devicesChanged();
        return;
    }
    m_searching = true;
    m_searchText = QStringLiteral("Looking for nearby devices…");
    emit searchChanged();
    listPeers();
}

void DeviceSend::stopSearching()
{
    if (!m_searching)
        return;
    ++m_generation;
    if (m_searchProcess) {
        m_searchProcess->disconnect(this);
        m_searchProcess->kill();
        m_searchProcess->deleteLater();
    }
    m_searching = false;
    m_searchText.clear();
    emit searchChanged();
}

void DeviceSend::endSearch(const QString &problem)
{
    m_searching = false;
    m_searchText.clear();
    m_problem = problem;
    emit searchChanged();
    emit devicesChanged();
}

void DeviceSend::setDevices(std::vector<Device> devices)
{
    m_devices = std::move(devices);
    emit devicesChanged();
}

void DeviceSend::listPeers()
{
    const int generation = m_generation;
    run(omdropProgram(), {QStringLiteral("peers"), QStringLiteral("--json")}, quickLimitMs,
        [this, generation](int code, const QString &out, const QString &err) {
            if (generation != m_generation)
                return;
            if (std::vector<Device> found = parsePeers(out.toUtf8()); !found.empty()) {
                setDevices(std::move(found));
                lookUpNames();
                return;
            }
            // Empty: AirDrop is off, or it's on and nobody has been heard yet.
            if (err.contains(QLatin1String("currently OFF")) && !m_turnedOn) {
                turnOn();
                return;
            }
            if (code == 1 && m_attempts++ < listAttempts) {
                QTimer::singleShot(retryMs(), this, [this, generation] {
                    if (generation == m_generation)
                        listPeers();
                });
                return;
            }
            setDevices({});
            if (code == 0 || code == 1) {
                endSearch(QStringLiteral("No Apple devices nearby. Set the iPhone to AirDrop ▸ Everyone for 10 Minutes, unlock it and open its share sheet."));
                return;
            }
            const QString line = lastLine(err);
            endSearch(line.isEmpty() ? QStringLiteral("Couldn't look for devices.") : line);
        });
}

// Listing devices needs AirDrop's window open, which is what hears them; omadrop opens it the same way to send.
void DeviceSend::turnOn()
{
    const int generation = m_generation;
    m_turnedOn = true;
    m_searchText = QStringLiteral("Turning AirDrop on for 5 minutes…");
    emit searchChanged();
    run(omdropProgram(), {QStringLiteral("on"), QStringLiteral("5m")}, quickLimitMs, [this, generation](int code, const QString &, const QString &err) {
        if (generation != m_generation)
            return;
        if (code != 0) {
            const QString line = lastLine(err);
            endSearch(line.isEmpty() ? QStringLiteral("AirDrop wouldn't turn on.") : QStringLiteral("AirDrop wouldn't turn on: %1").arg(line));
            return;
        }
        m_searchText = QStringLiteral("Looking for nearby devices…");
        emit searchChanged();
        listPeers();
    });
}

// The names arrive a device at a time (--stream) and are worth showing, but the addresses are enough to send.
void DeviceSend::lookUpNames()
{
    const int generation = m_generation;
    m_searchText = QStringLiteral("Asking them their names…");
    emit searchChanged();
    run(omdropProgram(), {QStringLiteral("peers"), QStringLiteral("-n"), QStringLiteral("--json"), QStringLiteral("--stream")}, nameLimitMs,
        [this, generation](int, const QString &out, const QString &) {
            if (generation != m_generation)
                return;
            mergeNames(parsePeers(out.toUtf8()));
            endSearch(QString());
        });
}

void DeviceSend::mergeNames(const std::vector<Device> &named)
{
    for (const Device &each : named) {
        for (Device &known : m_devices) {
            if (known.address == each.address && !each.name.isEmpty())
                known.name = each.name;
        }
    }
}

void DeviceSend::send(const QString &file, const Device &device)
{
    if (m_sending)
        return;
    // A name lookup transmits for as long as it runs, so it stops before the send begins.
    stopSearching();
    const QString omdrop = omdropProgram(), omadrop = omadropProgram();
    m_deviceName = device.label();
    m_failure.clear();
    if (omdrop.isEmpty() && omadrop.isEmpty()) {
        m_failure = missingText();
        emit finished(false);
        return;
    }
    m_sending = true;
    setStage(QStringLiteral("Sending to %1…").arg(m_deviceName));
    // omadrop opens the window and wakes a sleeping iPhone over Bluetooth; without it omdrop sends alone.
    const QStringList arguments = omadrop.isEmpty()
        ? QStringList{QStringLiteral("send"), QStringLiteral("--wait"), QStringLiteral("45"), QStringLiteral("--to"), device.address, QStringLiteral("--"), file}
        : QStringList{QStringLiteral("send"), QStringLiteral("--quiet"), QStringLiteral("--to"), device.address, QStringLiteral("--label"), m_deviceName,
                      QStringLiteral("--"), file};
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_sendProcess = process;
    auto *limit = new QTimer(process);
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, process, [process] { process->kill(); });
    // The sender narrates: the lines that matter become the stage.
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        const QString chunk = QString::fromUtf8(process->readAll());
        m_log += chunk;
        if (chunk.contains(QLatin1String("They accepted")))
            setStage(QStringLiteral("%1 accepted. Sending…").arg(m_deviceName));
        else if (chunk.contains(QLatin1String("accept it")))
            setStage(QStringLiteral("Waiting for %1 to accept…").arg(m_deviceName));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            endSend(false, missingText());
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString output = m_log + QString::fromUtf8(process->readAll());
        process->deleteLater();
        const QString why = status == QProcess::CrashExit ? QStringLiteral("The send to %1 didn't finish.").arg(m_deviceName)
                                                          : failureText(code, output, m_deviceName);
        endSend(why.isEmpty(), why);
    });
    m_log.clear();
    process->start(omadrop.isEmpty() ? omdrop : omadrop, arguments);
    limit->start(sendLimitMs);
}

void DeviceSend::cancel()
{
    if (!m_sending || !m_sendProcess)
        return;
    // Killing it ends the transfer; the finished handler reports it.
    m_cancelled = true;
    m_sendProcess->kill();
}

void DeviceSend::setStage(const QString &stage)
{
    m_stage = stage;
    emit stageChanged();
}

void DeviceSend::endSend(bool ok, const QString &failure)
{
    m_sending = false;
    m_stage.clear();
    m_failure = m_cancelled ? QStringLiteral("Cancelled.") : failure;
    m_cancelled = false;
    emit finished(ok && m_failure.isEmpty());
}
