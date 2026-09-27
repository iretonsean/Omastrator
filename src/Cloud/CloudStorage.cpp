#include "Cloud/CloudStorage.h"
#include "Cloud/CloudProviders.h"
#include "Logging.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int maximumErrorLength = 300;
// rclone's "not found" exit codes: directory (3) and file (4).
constexpr int directoryNotFound = 3;
constexpr int fileNotFound = 4;

QString stripLogPrefix(QString line)
{
    static const QRegularExpression prefix(QStringLiteral("^\\d{4}/\\d{2}/\\d{2} \\d{2}:\\d{2}:\\d{2}(\\.\\d+)? *[A-Z]* *: *"));
    return line.remove(prefix).trimmed();
}
}

CloudJob::CloudJob(const QString &program, const QStringList &arguments, QObject *parent) : QObject(parent), m_process(new QProcess(this))
{
    // An encrypted config would wait for a password on stdin; it fails instead.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("RCLONE_ASK_PASSWORD"), QStringLiteral("false"));
    m_process->setProcessEnvironment(environment);
    m_process->setStandardInputFile(QProcess::nullDevice());
    // Local remotes' relative paths resolve from the root, like the absolute ones they are.
    m_process->setWorkingDirectory(QDir::rootPath());
    connect(m_process, &QProcess::finished, this, &CloudJob::done);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_failedToStart = true;
            done();
        }
    });
    // The first word is the command; the rest may name files but never holds a secret we log.
    qCInfo(lcIO).noquote() << "rclone" << arguments.value(0) << arguments.value(1);
    m_process->start(program, arguments);
}

CloudJob::~CloudJob()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void CloudJob::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    m_process->kill();
}

void CloudJob::done()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_failedToStart) {
        m_error = QStringLiteral("rclone isn't installed, so cloud storage can't be reached.");
    } else {
        m_output = m_process->readAllStandardOutput();
        m_exitCode = m_process->exitStatus() == QProcess::NormalExit ? m_process->exitCode() : -1;
        if (m_cancelled)
            m_error = QStringLiteral("Cancelled.");
        else if (m_exitCode != 0)
            m_error = CloudStorage::cleanError(m_process->readAllStandardError(), m_exitCode);
    }
    if (!m_error.isEmpty() && !m_cancelled)
        qCWarning(lcIO).noquote() << "rclone failed:" << m_error;
    // Callers may delete us from the slot; the signal goes out from the event loop.
    QTimer::singleShot(0, this, [this] {
        emit finished();
        deleteLater();
    });
}

CloudStorage::CloudStorage(QObject *parent) : QObject(parent) {}

QString CloudStorage::program()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_RCLONE");
    return overridden.isEmpty() ? QStringLiteral("rclone") : overridden;
}

bool CloudStorage::isInstalled()
{
    const QString name = program();
    if (name.contains(QLatin1Char('/')))
        return QFileInfo(name).isExecutable();
    return !QStandardPaths::findExecutable(name).isEmpty();
}

std::optional<CloudRemote> CloudStorage::remote(const QString &name) const
{
    for (const CloudRemote &each : m_remotes) {
        if (each.name == name)
            return each;
    }
    return std::nullopt;
}

QString CloudStorage::serviceName(const QString &name) const
{
    const std::optional<CloudRemote> found = remote(name);
    if (!found)
        return name;
    const qsizetype sharing = std::count_if(m_remotes.begin(), m_remotes.end(), [&](const CloudRemote &each) { return each.type == found->type; });
    const QString service = CloudProviders::forType(found->type).name;
    return sharing > 1 ? QStringLiteral("%1 (%2)").arg(service, name) : service;
}

CloudJob *CloudStorage::run(const QStringList &arguments, std::function<void(CloudJob &)> done)
{
    QStringList full = arguments;
    if (!m_configFile.isEmpty())
        full << QStringLiteral("--config") << m_configFile;
    auto *job = new CloudJob(program(), full, this);
    connect(job, &CloudJob::finished, this, [job, done = std::move(done)] { done(*job); });
    return job;
}

void CloudStorage::refreshRemotes()
{
    if (!isInstalled()) {
        if (!m_remotes.isEmpty()) {
            m_remotes.clear();
            emit remotesChanged();
        }
        return;
    }
    if (m_listing)
        return;
    m_listing = run({QStringLiteral("listremotes"), QStringLiteral("--json")}, [this](CloudJob &job) {
        // A failed listing keeps what was known: offline doesn't disconnect anyone.
        if (job.succeeded()) {
            m_remotes = parseRemotes(job.output());
            emit remotesChanged();
        }
    });
}

CloudJob *CloudStorage::list(const CloudLocation &folder, std::function<void(const QList<CloudEntry> &, const QString &)> done)
{
    return run({QStringLiteral("lsjson"), QStringLiteral("--no-mimetype"), folder.toString()},
               [done](CloudJob &job) { done(job.succeeded() ? parseEntries(job.output()) : QList<CloudEntry>(), job.error()); });
}

CloudJob *CloudStorage::stat(const CloudLocation &file, std::function<void(const CloudStamp &, const QString &)> done)
{
    return run({QStringLiteral("lsjson"), QStringLiteral("--stat"), QStringLiteral("--hash"), QStringLiteral("--no-mimetype"), file.toString()},
               [done](CloudJob &job) {
                   if (job.succeeded())
                       done(parseStamp(job.output()), QString());
                   else if (!job.wasCancelled() && (job.exitCode() == directoryNotFound || job.exitCode() == fileNotFound))
                       done(CloudStamp{}, QString());
                   else
                       done(CloudStamp{}, job.error());
               });
}

CloudJob *CloudStorage::download(const CloudLocation &file, const QString &localPath, std::function<void(const QString &)> done)
{
    QDir().mkpath(QFileInfo(localPath).absolutePath());
    return run({QStringLiteral("copyto"), file.toString(), localPath}, [done](CloudJob &job) { done(job.error()); });
}

CloudJob *CloudStorage::upload(const QString &localPath, const CloudLocation &file, std::function<void(const QString &)> done)
{
    // The uploader retries with its own backoff.
    return run({QStringLiteral("copyto"), QStringLiteral("--retries"), QStringLiteral("1"), localPath, file.toString()},
               [done](CloudJob &job) { done(job.error()); });
}

CloudJob *CloudStorage::makeFolder(const CloudLocation &folder, std::function<void(const QString &)> done)
{
    return run({QStringLiteral("mkdir"), folder.toString()}, [done](CloudJob &job) { done(job.error()); });
}

CloudJob *CloudStorage::createRemote(const QString &name, const QString &type, const QList<std::pair<QString, QString>> &options, bool all,
                                     std::function<void(const CloudConfigStep &)> done)
{
    // key=value pairs; --obscure so rclone never stores a password in the clear.
    QStringList arguments{QStringLiteral("config"), QStringLiteral("create"), name, type};
    for (const auto &[key, value] : options)
        arguments << key + QLatin1Char('=') + value;
    arguments << QStringLiteral("--obscure") << QStringLiteral("--non-interactive");
    if (all)
        arguments << QStringLiteral("--all");
    return run(arguments, [done](CloudJob &job) {
        done(job.succeeded() ? parseStep(job.output()) : CloudConfigStep{false, std::nullopt, job.error()});
    });
}

CloudJob *CloudStorage::answer(const QString &name, const QString &state, const QString &result, std::function<void(const CloudConfigStep &)> done)
{
    return run({QStringLiteral("config"), QStringLiteral("update"), name, QStringLiteral("--continue"), QStringLiteral("--non-interactive"),
                QStringLiteral("--state"), state, QStringLiteral("--result"), result},
               [done](CloudJob &job) {
                   done(job.succeeded() ? parseStep(job.output()) : CloudConfigStep{false, std::nullopt, job.error()});
               });
}

CloudJob *CloudStorage::deleteRemote(const QString &name, std::function<void(const QString &)> done)
{
    return run({QStringLiteral("config"), QStringLiteral("delete"), name}, [done](CloudJob &job) { done(job.error()); });
}

QList<CloudRemote> CloudStorage::parseRemotes(const QByteArray &json)
{
    QList<CloudRemote> remotes;
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (CloudLocation::isValidRemoteName(name))
            remotes.append({name, object.value(QStringLiteral("type")).toString()});
    }
    return remotes;
}

QList<CloudEntry> CloudStorage::parseEntries(const QByteArray &json)
{
    QList<CloudEntry> entries;
    for (const QJsonValue &value : QJsonDocument::fromJson(json).array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("Name")).toString();
        if (name.isEmpty() || name.contains(QLatin1Char('/')))
            continue;
        entries.append({name, object.value(QStringLiteral("IsDir")).toBool(), object.value(QStringLiteral("Size")).toInteger(-1),
                        QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs)});
    }
    // Folders first, then by name as people sort.
    std::sort(entries.begin(), entries.end(), [](const CloudEntry &a, const CloudEntry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return entries;
}

CloudStamp CloudStorage::parseStamp(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    CloudStamp stamp;
    if (object.isEmpty())
        return stamp;
    stamp.exists = true;
    stamp.size = object.value(QStringLiteral("Size")).toInteger(-1);
    stamp.modified = QDateTime::fromString(object.value(QStringLiteral("ModTime")).toString(), Qt::ISODateWithMs);
    stamp.hashes = object.value(QStringLiteral("Hashes")).toObject().toVariantMap();
    return stamp;
}

CloudConfigStep CloudStorage::parseStep(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    CloudConfigStep step;
    const QString state = object.value(QStringLiteral("State")).toString();
    const QJsonObject option = object.value(QStringLiteral("Option")).toObject();
    const QString error = scrub(object.value(QStringLiteral("Error")).toString());
    if (state.isEmpty() || option.isEmpty()) {
        step.finished = error.isEmpty();
        step.error = error;
        return step;
    }
    CloudQuestion question;
    question.state = state;
    question.name = option.value(QStringLiteral("Name")).toString();
    question.help = option.value(QStringLiteral("Help")).toString().trimmed();
    question.password = option.value(QStringLiteral("IsPassword")).toBool();
    // A sensitive or password default is never shown back.
    if (!question.password && !option.value(QStringLiteral("Sensitive")).toBool())
        question.defaultValue = option.value(QStringLiteral("DefaultStr")).toString();
    question.exclusive = option.value(QStringLiteral("Exclusive")).toBool();
    question.required = option.value(QStringLiteral("Required")).toBool();
    for (const QJsonValue &example : option.value(QStringLiteral("Examples")).toArray()) {
        question.choices << example.toObject().value(QStringLiteral("Value")).toString();
        question.choiceHelp << example.toObject().value(QStringLiteral("Help")).toString();
    }
    question.error = error;
    step.question = question;
    return step;
}

QString CloudStorage::scrub(const QString &text)
{
    static const QRegularExpression keyed(
        QStringLiteral("(?i)((?:access_|refresh_)?token|client_secret|secret|password|passwd|\\bpass\\b|\\bkey\\b|authorization|bearer)"
                       "(\"?\\s*[:=]\\s*)(\"[^\"]*\"|\\{[^}]*\\}?|\\S+)"));
    // Token-like runs: long, no dots, slashes or spaces, with digits among the letters. Paths stay readable.
    static const QRegularExpression longRun(QStringLiteral("[A-Za-z0-9_\\-+=]{32,}"));
    static const QRegularExpression digit(QStringLiteral("[0-9]"));
    QString clean = text;
    clean.replace(keyed, QStringLiteral("\\1\\2[hidden]"));
    for (qsizetype from = 0;;) {
        const QRegularExpressionMatch run = longRun.match(clean, from);
        if (!run.hasMatch())
            break;
        if (run.captured().contains(digit)) {
            clean.replace(run.capturedStart(), run.capturedLength(), QStringLiteral("[hidden]"));
            from = run.capturedStart() + 8;
        } else {
            from = run.capturedEnd();
        }
    }
    return clean;
}

QString CloudStorage::cleanError(const QByteArray &stderrBytes, int exitCode)
{
    QString chosen, last;
    for (const QString &raw : QString::fromUtf8(stderrBytes).split(QLatin1Char('\n'))) {
        const QString line = stripLogPrefix(raw);
        // Cobra's usage block follows a bad command; nothing after it is the reason.
        if (line.startsWith(QLatin1String("Usage:")))
            break;
        if (line.isEmpty() || line.startsWith(QLatin1String("Attempt ")) || line.startsWith(QLatin1String("Config file")))
            continue;
        last = line;
        if (line.contains(QLatin1String("Failed to")) || line.startsWith(QLatin1String("Fatal error")) || line.startsWith(QLatin1String("Error:")))
            chosen = line;
    }
    QString reason = chosen.isEmpty() ? last : chosen;
    static const QRegularExpression verb(QStringLiteral("^(Failed to [a-z]+|Fatal error|Error): +"));
    reason.remove(verb);
    reason = scrub(reason).trimmed();
    if (reason.isEmpty())
        return QStringLiteral("rclone stopped with code %1.").arg(exitCode);
    if (reason.size() > maximumErrorLength)
        reason = reason.left(maximumErrorLength - 1) + QStringLiteral("…");
    reason[0] = reason[0].toUpper();
    if (!reason.endsWith(QLatin1Char('.')) && !reason.endsWith(QStringLiteral("…")))
        reason += QLatin1Char('.');
    return reason;
}
