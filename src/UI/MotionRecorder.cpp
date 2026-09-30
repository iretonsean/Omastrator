#include "UI/MotionRecorder.h"
#include "UI/BrowserViews.h"
#include <QPointer>
#include <QTimer>
#include <cmath>

MotionRecorder::MotionRecorder(BrowserViews &views, QObject *parent) : QObject(parent), m_views(views) {}

MotionRecorder::~MotionRecorder()
{
    abort();
}

int MotionRecorder::steps(double durationMs, int fps)
{
    return std::max(1, int(std::lround(durationMs / 1000.0 * fps)));
}

QString MotionRecorder::start(const Job &job)
{
    if (m_running)
        return QStringLiteral("A recording is already running.");
    if (job.durationMs <= 0 || !job.seek)
        return QStringLiteral("There is no motion to record.");
    auto recorder = std::make_unique<FrameRecorder>();
    if (const QString failure = recorder->start({job.path, job.format, job.fps}); !failure.isEmpty())
        return failure;
    m_recorder = std::move(recorder);
    m_job = job;
    m_step = 0;
    m_steps = steps(job.durationMs, job.fps);
    m_stopping = false;
    m_running = true;
    ++m_generation;
    emit progress(0, m_steps);
    QTimer::singleShot(0, this, &MotionRecorder::next);
    return {};
}

void MotionRecorder::stop()
{
    if (m_running)
        m_stopping = true;
}

void MotionRecorder::abort()
{
    if (!m_running)
        return;
    ++m_generation;
    m_running = false;
    if (m_recorder)
        m_recorder->abort();
    m_recorder.reset();
}

void MotionRecorder::next()
{
    if (!m_running)
        return;
    if (m_stopping || m_step >= m_steps) {
        end();
        return;
    }
    // A run faster than the encoder waits for it.
    if (m_recorder->pending() > 4 * 1024 * 1024) {
        QTimer::singleShot(10, this, &MotionRecorder::next);
        return;
    }
    const int generation = m_generation;
    const QPointer<MotionRecorder> guard(this);
    const double at = double(m_step) * 1000.0 / m_job.fps;
    m_job.seek(at, [guard, generation](const QString &error) {
        if (!guard || guard->m_generation != generation || !guard->m_running)
            return;
        if (!error.isEmpty()) {
            guard->fail(error);
            return;
        }
        guard->m_views.capture(guard->m_job.frame, guard->m_job.longSide, [guard, generation](const QByteArray &jpeg, const QSizeF &, const QString &why) {
            if (!guard || guard->m_generation != generation || !guard->m_running)
                return;
            if (!why.isEmpty()) {
                guard->fail(why);
                return;
            }
            if (const QString failure = guard->m_recorder->add(jpeg); !failure.isEmpty()) {
                guard->fail(failure);
                return;
            }
            ++guard->m_step;
            emit guard->progress(guard->m_step, guard->m_steps);
            QTimer::singleShot(0, guard.data(), &MotionRecorder::next);
        });
    });
}

void MotionRecorder::fail(const QString &error)
{
    // The recorder's own message already says "Couldn't record"; another cause gets the same words.
    const QString message = error.startsWith(QLatin1String("Couldn't record")) ? error : QStringLiteral("Couldn't record: %1").arg(error);
    if (m_recorder)
        m_recorder->abort();
    m_recorder.reset();
    m_running = false;
    emit finished(message, m_job.path, 0);
}

void MotionRecorder::end()
{
    const int generation = m_generation;
    FrameRecorder *recorder = m_recorder.get();
    connect(recorder, &FrameRecorder::finished, this, [this, generation](const QString &error) {
        if (generation != m_generation)
            return;
        const double seconds = double(m_recorder->frames()) / m_job.fps;
        m_running = false;
        emit finished(error, m_job.path, seconds);
    });
    recorder->finish();
}
