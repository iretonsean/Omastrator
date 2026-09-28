#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

// Send to a device (docs/SHARE.md): finds the Apple devices nearby and AirDrops a file to one, through the
// `omdrop` command (netmojo.omdrop), and `omadrop` for the send when it's installed (it wakes a sleeping iPhone
// over Bluetooth). Both programs can be replaced ($OMASTRATOR_OMDROP, $OMASTRATOR_OMADROP; one that doesn't
// exist counts as not installed) so tests never reach the radio.
class DeviceSend : public QObject {
    Q_OBJECT
public:
    struct Device {
        // The AirDrop address omdrop lists, which `send --to` takes.
        QString address;
        // Empty until the device has answered a name lookup.
        QString name;
        std::optional<int> signal;
        QString label() const;
    };

    explicit DeviceSend(QObject *parent = nullptr);
    ~DeviceSend() override;

    // $OMASTRATOR_OMDROP, else omdrop on PATH or where the plugin puts it; empty when it isn't installed.
    static QString omdropProgram();
    // Likewise for omadrop; empty is fine, the send goes through omdrop alone.
    static QString omadropProgram();
    // The line shown when omdrop isn't installed.
    static QString missingText();

    // Looks for nearby devices, turning AirDrop on for a few minutes when it's off.
    void refresh();
    // Stops a search in progress, and leaves a send alone.
    void stopSearching();
    bool searching() const { return m_searching; }
    const std::vector<Device> &devices() const { return m_devices; }
    // Why there's nothing to pick, in one line and with what to do; empty while there are devices.
    QString problem() const { return m_problem; }
    // "Looking for nearby devices…", while a search runs.
    QString searchText() const { return m_searchText; }

    // AirDrops `file` as one transfer.
    void send(const QString &file, const Device &device);
    void cancel();
    bool sending() const { return m_sending; }
    // "Asking Test iPhone to accept it…", while it sends.
    QString stageText() const { return m_stage; }
    // Why the last send failed, in one line.
    QString failure() const { return m_failure; }

    // What a send's output says: the sentence for a failure, or empty when the transfer got through.
    static QString failureText(int exitCode, const QString &output, const QString &name);
    // omdrop's `peers --json` (an array) or `--stream` (a line each) → devices, skipping what isn't one.
    static std::vector<Device> parsePeers(const QByteArray &json);

signals:
    void devicesChanged();
    void searchChanged();
    void stageChanged();
    void finished(bool ok);

private:
    using Done = std::function<void(int exitCode, const QString &out, const QString &err)>;
    // Runs one program to the end, killing it after `limitMs`.
    void run(const QString &program, const QStringList &arguments, int limitMs, Done done);
    void listPeers();
    void turnOn();
    void lookUpNames();
    void endSearch(const QString &problem);
    void setDevices(std::vector<Device> devices);
    void setStage(const QString &stage);
    void endSend(bool ok, const QString &failure);
    void mergeNames(const std::vector<Device> &named);

    std::vector<Device> m_devices;
    QString m_problem;
    QString m_searchText;
    bool m_searching = false;
    bool m_turnedOn = false;
    int m_attempts = 0;
    // A new search makes the running one's answers stale.
    int m_generation = 0;
    QPointer<QProcess> m_searchProcess;

    bool m_sending = false;
    QString m_stage;
    QString m_failure;
    QString m_deviceName;
    QString m_log;
    bool m_cancelled = false;
    QPointer<QProcess> m_sendProcess;
};
