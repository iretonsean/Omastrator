#pragma once
#include "Live/DeployJob.h"
#include "UI/Share.h"
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <functional>

class CloudStorage;

// One share, or one Unshare, in the background: a cloud upload then `rclone link`,
// a secret gist or a release asset through `gh`, or a preview deploy. Nothing it
// runs has its output logged, and failures pass through CloudStorage::scrub.
class ShareJob : public QObject {
    Q_OBJECT
public:
    struct Request {
        // The rendered file; its name is the name the client sees.
        QString file;
        Share::Format format = Share::Format::png;
        QString documentName;
        // "cloud:<remote>" or "github".
        QString destination;
        // The service's name for the toast, "Google Drive".
        QString where;
        QString scope;
        QStringList objects;
    };

    explicit ShareJob(CloudStorage &cloud, QObject *parent = nullptr);
    ~ShareJob() override;

    void start(const Request &request);
    // Live: a preview deploy of `folder`, never production.
    void startPreview(const QString &folder);
    // Takes a share down: deletes the cloud file, the gist, or the release and its tag.
    void unshare(const Share::Record &record);
    void cancel();

    bool running() const { return m_running; }
    // "Uploading to Google Drive…", while it runs.
    QString stageText() const { return m_stage; }
    // Why it failed, in one line.
    QString failure() const { return m_failure; }
    // It failed because GitHub isn't connected: the toast offers Connect.
    bool needsConnection() const { return m_needsConnection; }
    // The share it made.
    const Share::Record &record() const { return m_record; }
    // A preview deploy's log, for Details.
    QString log() const { return m_log; }

    // The repository release assets go in, under the user's own account.
    static const QString repositoryName;
    static QString releaseLink(const QString &repository, const QString &tag, const QString &fileName);
    // "https://gist.github.com/user/abc123" → "abc123".
    static QString gistId(const QString &url);

signals:
    void stageChanged();
    void finished(bool ok);

private:
    void setStage(const QString &stage);
    void succeed();
    void fail(const QString &line, bool needsConnection = false);
    // Runs gh; `done` gets whether it worked and what it printed.
    void gh(const QStringList &arguments, std::function<void(bool, const QString &)> done);
    void shareToCloud();
    void shareToGitHub();
    void uploadRelease(const QString &repository);

    CloudStorage &m_cloud;
    Request m_request;
    Share::Record m_record;
    bool m_running = false;
    QString m_stage;
    QString m_failure;
    bool m_needsConnection = false;
    QString m_log;
    QPointer<QProcess> m_process;
    QPointer<QObject> m_cloudJob;
    DeployJob *m_deploy = nullptr;
};
