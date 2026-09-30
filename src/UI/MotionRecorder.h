#pragma once
#include "IO/FrameRecorder.h"
#include <QObject>
#include <QUuid>
#include <functional>
#include <memory>

class BrowserViews;

// Record (docs/MOTION.md, section 8): the motion played by seeking, one step per picture at a fixed frame rate, so the file
// is smooth whatever the machine's load. Each step puts the page at its time, takes a picture of the frame and hands it to
// ffmpeg. Nothing is written before start(), and a failure removes the partial file.
class MotionRecorder : public QObject {
    Q_OBJECT
public:
    struct Job {
        QUuid frame;
        // The file (for PNG frames, a new folder), its kind and frame rate.
        QString path;
        FrameRecorder::Format format = FrameRecorder::Format::mp4;
        int fps = 30;
        // The motion's length, in ms; a scroll-driven motion says the range in the same unit its `seek` takes.
        double durationMs = 0;
        // The longer side of a picture is at most this many px.
        int longSide = 2560;
        // Puts the page at `ms` and calls `done` at once, or with why it couldn't. Runs on this thread.
        std::function<void(double ms, std::function<void(const QString &error)> done)> seek;
    };

    explicit MotionRecorder(BrowserViews &views, QObject *parent = nullptr);
    ~MotionRecorder() override;

    // How many pictures a motion of `durationMs` makes at `fps`: 2.0 s at 30 fps is 60.
    static int steps(double durationMs, int fps);

    // Returns why it can't start, or empty. The steps follow on the event loop.
    QString start(const Job &job);
    // Ends the file where it is; finished() follows once the step in flight is done, or after a couple of seconds if it never is.
    void stop();
    // How long stop() waits for a step that doesn't answer (tests shorten it).
    static void setStopWaitMs(int ms);
    // Ends it and removes what was written. No signal.
    void abort();
    bool recording() const { return m_running; }
    int step() const { return m_step; }
    int total() const { return m_steps; }
    const Job &job() const { return m_job; }

signals:
    void progress(int step, int steps);
    // Empty `error`: the file is whole, `seconds` long. Else why not, and nothing is left on disk.
    void finished(const QString &error, const QString &path, double seconds);

private:
    void next();
    void fail(const QString &error);
    void end();

    BrowserViews &m_views;
    Job m_job;
    std::unique_ptr<FrameRecorder> m_recorder;
    bool m_running = false;
    bool m_stopping = false;
    // A step's seek or picture is on its way; and the file is being finished, so a late answer is stale.
    bool m_inFlight = false;
    bool m_ending = false;
    int m_step = 0;
    int m_steps = 0;
    // A new run makes an older one's late answers stale.
    int m_generation = 0;
};
