#include "Cloud/CloudUploader.h"
#include "Logging.h"
#include <QFileInfo>

CloudUploader::CloudUploader(CloudStorage &storage, QObject *parent) : QObject(parent), m_storage(storage) {}

void CloudUploader::setRetryDelays(int firstMs, int maximumMs)
{
    m_firstDelayMs = std::max(1, firstMs);
    m_maximumDelayMs = std::max(m_firstDelayMs, maximumMs);
}

QString CloudUploader::conflictName(const QString &fileName, const QDateTime &when)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString suffix = dot > 0 ? fileName.mid(dot) : QString();
    return QStringLiteral("%1 (conflict %2)%3").arg(stem, when.toString(QStringLiteral("yyyy-MM-dd HHmm")), suffix);
}

CloudUploader::Status CloudUploader::status(const QString &key) const
{
    const auto found = m_jobs.constFind(key);
    return found == m_jobs.cend() ? Status{} : found->status;
}

void CloudUploader::setPhase(const QString &key, Phase phase)
{
    m_jobs[key].status.phase = phase;
    emit statusChanged(key);
}

void CloudUploader::upload(const QString &key, const QString &local, const CloudLocation &target, std::optional<CloudStamp> base)
{
    auto found = m_jobs.find(key);
    if (found != m_jobs.end()) {
        found->local = local;
        switch (found->status.phase) {
        case Phase::checking:
        case Phase::uploading: found->again = true; return;
        // The answer to the conflict uploads whatever was saved last.
        case Phase::conflict: return;
        case Phase::waiting:
        case Phase::idle: found->target = target; start(key); return;
        }
    }
    Job job;
    job.local = local;
    job.target = target;
    job.base = base;
    job.status.target = target;
    job.timer = new QTimer(this);
    job.timer->setSingleShot(true);
    connect(job.timer, &QTimer::timeout, this, [this, key] { start(key); });
    m_jobs.insert(key, job);
    start(key);
}

void CloudUploader::start(const QString &key)
{
    Job &job = m_jobs[key];
    job.timer->stop();
    job.status.target = job.target;
    if (!job.base) {
        send(key);
        return;
    }
    setPhase(key, Phase::checking);
    job.running = m_storage.stat(job.target, [this, key](const CloudStamp &now, const QString &error) {
        if (!m_jobs.contains(key))
            return;
        if (!error.isEmpty()) {
            fail(key, error);
            return;
        }
        Job &checked = m_jobs[key];
        if (!now.sameVersion(*checked.base)) {
            qCInfo(lcIO).noquote() << "cloud conflict on" << checked.target.toString();
            checked.status.theirs = now;
            checked.again = false;
            setPhase(key, Phase::conflict);
            emit conflicted(key);
            return;
        }
        send(key);
    });
}

void CloudUploader::send(const QString &key)
{
    setPhase(key, Phase::uploading);
    Job &job = m_jobs[key];
    job.running = m_storage.upload(job.local, job.target, [this, key](const QString &error) {
        if (!m_jobs.contains(key))
            return;
        if (!error.isEmpty()) {
            fail(key, error);
            return;
        }
        // The new version is the base for the next save.
        m_jobs[key].running = m_storage.stat(m_jobs[key].target, [this, key](const CloudStamp &now, const QString &statError) {
            if (!m_jobs.contains(key))
                return;
            CloudStamp stamp = now;
            if (!statError.isEmpty() || !now.exists) {
                const QFileInfo sent(m_jobs[key].local);
                stamp = CloudStamp{true, sent.size(), sent.lastModified(), {}};
            }
            finish(key, stamp);
        });
    });
}

void CloudUploader::fail(const QString &key, const QString &error)
{
    Job &job = m_jobs[key];
    job.status.attempts += 1;
    job.status.error = error;
    job.delayMs = job.delayMs == 0 ? m_firstDelayMs : std::min(job.delayMs * 2, m_maximumDelayMs);
    job.status.retryInMs = job.delayMs;
    job.again = false;
    qCWarning(lcIO).noquote() << "upload to" << job.target.toString() << "failed, retrying in" << job.delayMs << "ms";
    job.timer->start(job.delayMs);
    setPhase(key, Phase::waiting);
}

void CloudUploader::finish(const QString &key, const CloudStamp &stamp)
{
    Job &job = m_jobs[key];
    const CloudLocation target = job.target;
    qCInfo(lcIO).noquote() << "uploaded" << target.toString();
    if (job.again) {
        job.again = false;
        job.base = stamp;
        job.delayMs = 0;
        job.status.attempts = 0;
        emit uploaded(key, target, stamp);
        start(key);
        return;
    }
    job.timer->deleteLater();
    m_jobs.remove(key);
    emit uploaded(key, target, stamp);
    emit statusChanged(key);
}

void CloudUploader::overwrite(const QString &key)
{
    if (!m_jobs.contains(key))
        return;
    m_jobs[key].base.reset();
    start(key);
}

void CloudUploader::keepBoth(const QString &key, const QDateTime &when)
{
    if (!m_jobs.contains(key))
        return;
    Job &job = m_jobs[key];
    job.target = job.target.parent().child(conflictName(job.target.fileName(), when));
    // The copy's name is new: expect nothing there.
    job.base = CloudStamp{};
    start(key);
}

void CloudUploader::retryNow(const QString &key)
{
    if (m_jobs.contains(key) && m_jobs[key].status.phase == Phase::waiting)
        start(key);
}

void CloudUploader::cancel(const QString &key)
{
    if (!m_jobs.contains(key))
        return;
    Job job = m_jobs.take(key);
    job.timer->deleteLater();
    if (job.running)
        job.running->cancel();
    emit statusChanged(key);
}
