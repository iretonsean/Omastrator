#pragma once
#include "Cloud/CloudStorage.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <optional>

// Background uploads after a local save: a check for someone else's change, then rclone copyto,
// retried with backoff until it works or is cancelled.
class CloudUploader : public QObject {
    Q_OBJECT
public:
    enum class Phase { idle, checking, uploading, waiting, conflict };
    struct Status {
        Phase phase = Phase::idle;
        CloudLocation target;
        // The last failure, while waiting to retry.
        QString error;
        int attempts = 0;
        int retryInMs = 0;
        // What's there now, when it isn't what was opened.
        CloudStamp theirs;
    };

    explicit CloudUploader(CloudStorage &storage, QObject *parent = nullptr);

    // 2 s, doubling to 5 min; tests shorten it.
    void setRetryDelays(int firstMs, int maximumMs);
    // Uploads `local` to `target` under `key` (a tab, an export). With a base, a remote that no
    // longer matches it stops as a conflict instead of being overwritten.
    void upload(const QString &key, const QString &local, const CloudLocation &target, std::optional<CloudStamp> base);
    // Conflict answers.
    void overwrite(const QString &key);
    void keepBoth(const QString &key, const QDateTime &when = QDateTime::currentDateTime());
    void retryNow(const QString &key);
    void cancel(const QString &key);

    bool isPending(const QString &key) const { return m_jobs.contains(key); }
    Status status(const QString &key) const;
    QStringList pendingKeys() const { return m_jobs.keys(); }
    // "logo.omai" → "logo (conflict 2026-09-27 1412).omai"
    static QString conflictName(const QString &fileName, const QDateTime &when);

signals:
    void statusChanged(const QString &key);
    // Done; the stamp is the new version, the base for the next save.
    void uploaded(const QString &key, const CloudLocation &target, const CloudStamp &stamp);
    void conflicted(const QString &key);

private:
    struct Job {
        QString local;
        CloudLocation target;
        std::optional<CloudStamp> base;
        Status status;
        int delayMs = 0;
        // A save arrived mid-upload: go again with the new base afterwards.
        bool again = false;
        QPointer<CloudJob> running;
        QTimer *timer = nullptr;
    };

    void start(const QString &key);
    void send(const QString &key);
    void fail(const QString &key, const QString &error);
    void finish(const QString &key, const CloudStamp &stamp);
    void setPhase(const QString &key, Phase phase);

    CloudStorage &m_storage;
    QHash<QString, Job> m_jobs;
    int m_firstDelayMs = 2000;
    int m_maximumDelayMs = 5 * 60 * 1000;
};
