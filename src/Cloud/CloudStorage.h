#pragma once
#include "Cloud/CloudLocation.h"
#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <functional>
#include <optional>

// A remote in the user's rclone config: its name and backend type, never its settings.
struct CloudRemote {
    QString name;
    QString type;
};

struct CloudEntry {
    QString name;
    bool isDir = false;
    qint64 size = -1;
    QDateTime modified;
};

// One question rclone asks while it sets up a remote (a 2FA code, which drive, "use the browser?").
struct CloudQuestion {
    QString state;
    QString name;
    QString help;
    QString defaultValue;
    QStringList choices;
    QStringList choiceHelp;
    bool exclusive = false;
    bool password = false;
    bool required = false;
    QString error;
};

// Where a connect stands after one rclone step: finished, a question, or an error.
struct CloudConfigStep {
    bool finished = false;
    std::optional<CloudQuestion> question;
    QString error;
};

// One rclone process. Its stdout is never logged: `config` output can hold tokens.
class CloudJob : public QObject {
    Q_OBJECT
public:
    CloudJob(const QString &program, const QStringList &arguments, QObject *parent);
    ~CloudJob() override;

    bool isRunning() const { return m_running; }
    bool succeeded() const { return !m_running && !m_cancelled && m_error.isEmpty(); }
    bool wasCancelled() const { return m_cancelled; }
    int exitCode() const { return m_exitCode; }
    // The plain reason, cleaned from rclone's stderr.
    QString error() const { return m_error; }
    QByteArray output() const { return m_output; }
    // Stops the process; `finished` still follows, marked cancelled.
    void cancel();

signals:
    void finished();

private:
    void done();

    QProcess *const m_process;
    QByteArray m_output;
    QString m_error;
    int m_exitCode = -1;
    bool m_running = true;
    bool m_cancelled = false;
    bool m_failedToStart = false;
};

// Every cloud service through rclone. rclone keeps the sign-ins; Omastrator only runs it.
class CloudStorage : public QObject {
    Q_OBJECT
public:
    explicit CloudStorage(QObject *parent = nullptr);

    // OMASTRATOR_RCLONE, else `rclone` on PATH.
    static QString program();
    static bool isInstalled();
    // A config file passed as --config; tests use a throwaway one.
    void setConfigFile(const QString &path) { m_configFile = path; }

    // As last listed; empty until refreshRemotes() finishes.
    const QList<CloudRemote> &remotes() const { return m_remotes; }
    std::optional<CloudRemote> remote(const QString &name) const;
    // "Google Drive" for a drive remote; the remote's own name when two share a service.
    QString serviceName(const QString &remote) const;
    void refreshRemotes();

    CloudJob *list(const CloudLocation &folder, std::function<void(const QList<CloudEntry> &, const QString &error)> done);
    // A missing file is a stamp with exists false, not an error.
    CloudJob *stat(const CloudLocation &file, std::function<void(const CloudStamp &, const QString &error)> done);
    CloudJob *download(const CloudLocation &file, const QString &localPath, std::function<void(const QString &error)> done);
    CloudJob *upload(const QString &localPath, const CloudLocation &file, std::function<void(const QString &error)> done);
    CloudJob *makeFolder(const CloudLocation &folder, std::function<void(const QString &error)> done);
    // `rclone config create`, answering with rclone's questions one at a time; `all` asks every basic one.
    CloudJob *createRemote(const QString &name, const QString &type, const QList<std::pair<QString, QString>> &options, bool all,
                           std::function<void(const CloudConfigStep &)> done);
    CloudJob *answer(const QString &name, const QString &state, const QString &result, std::function<void(const CloudConfigStep &)> done);
    CloudJob *deleteRemote(const QString &name, std::function<void(const QString &error)> done);

    // Parsers, public for tests.
    static QList<CloudRemote> parseRemotes(const QByteArray &json);
    static QList<CloudEntry> parseEntries(const QByteArray &json);
    static CloudStamp parseStamp(const QByteArray &json);
    static CloudConfigStep parseStep(const QByteArray &json);
    // The last useful line of rclone's stderr, with anything secret-looking removed.
    static QString cleanError(const QByteArray &stderrBytes, int exitCode);
    static QString scrub(const QString &text);

signals:
    void remotesChanged();

private:
    CloudJob *run(const QStringList &arguments, std::function<void(CloudJob &)> done);

    QList<CloudRemote> m_remotes;
    QString m_configFile;
    QPointer<CloudJob> m_listing;
};
