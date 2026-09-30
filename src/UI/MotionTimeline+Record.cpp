#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionRecorder.h"
#include "UI/MotionTimeline.h"
#include "UI/MotionTrackView.h"
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>

// Record MP4 in the timeline's header (docs/MOTION.md, section 8): the header's controls, the save dialog, and the steps
// that put the held page at each time and hand a picture of the frame to ffmpeg.

namespace {
MotionTimeline::RecordChooser &chooser()
{
    static MotionTimeline::RecordChooser answer;
    return answer;
}

// How long a recording of a scroll-driven motion takes: the same two seconds Play scrolls its range in.
constexpr double scrollMotionMs = 2000;

QString slugOf(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    return text.toLower().split(separators, Qt::SkipEmptyParts).join(QLatin1Char('-'));
}
}

void MotionTimeline::setRecordChooser(RecordChooser answer)
{
    chooser() = std::move(answer);
}

void MotionTimeline::buildRecord(QWidget *header, QHBoxLayout *row)
{
    m_recordedButton = new QToolButton(header);
    m_recordedButton->setObjectName(QStringLiteral("motionRecorded"));
    m_recordedButton->setAutoRaise(true);
    m_recordedButton->setFocusPolicy(Qt::NoFocus);
    m_recordedButton->hide();
    m_record = new QToolButton(header);
    m_record->setObjectName(QStringLiteral("motionRecord"));
    m_record->setText(tr("Record MP4"));
    m_record->setAutoRaise(true);
    m_record->setFocusPolicy(Qt::NoFocus);
    // The ⋯ keeps what is one step away: a GIF, sixty frames a second, and the frames as pictures.
    m_more = new QToolButton(header);
    m_more->setObjectName(QStringLiteral("motionRecordMore"));
    m_more->setText(QStringLiteral("⋯"));
    m_more->setToolTip(tr("More ways to record"));
    m_more->setAccessibleName(tr("More ways to record"));
    m_more->setAutoRaise(true);
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_moreMenu = new QMenu(m_more);
    const auto item = [this](const QString &text, const QString &name, FrameRecorder::Format format, int fps) {
        QAction *action = m_moreMenu->addAction(text);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, format, fps] {
            const QString failure = record(format, fps);
            if (!failure.isEmpty())
                emit notice(failure);
        });
    };
    item(tr("Record GIF…"), QStringLiteral("motionRecordGif"), FrameRecorder::Format::gif, 30);
    item(tr("Record MP4 at 60 fps…"), QStringLiteral("motionRecord60"), FrameRecorder::Format::mp4, 60);
    m_moreMenu->addSeparator();
    item(tr("Save Frames as PNG…"), QStringLiteral("motionSavePng"), FrameRecorder::Format::pngFrames, 30);
    m_more->setMenu(m_moreMenu);
    // Without ffmpeg the menu shows its ffmpeg items disabled; the answer is read as it opens.
    connect(m_moreMenu, &QMenu::aboutToShow, this, [this] {
        m_canEncode = !FrameRecorder::ffmpeg().isEmpty();
        syncRecord();
    });
    connect(m_record, &QToolButton::clicked, this, [this] {
        if (m_recording) {
            stopRecording();
            return;
        }
        const QString failure = record(FrameRecorder::Format::mp4, 30);
        if (!failure.isEmpty())
            emit notice(failure);
    });
    connect(m_recordedButton, &QToolButton::clicked, this, &MotionTimeline::openRecorded);
    row->addWidget(m_recordedButton);
    row->addWidget(m_record);
    row->addWidget(m_more);
}

void MotionTimeline::syncRecord()
{
    if (!m_record)
        return;
    const bool rows = !m_timeline.tracks.isEmpty();
    m_record->setText(m_recording ? tr("Stop") : tr("Record MP4"));
    m_record->setEnabled(m_recording || (rows && m_canEncode));
    const QString tip = m_recording ? tr("Stop recording. Esc does the same.")
        : !m_canEncode              ? FrameRecorder::ffmpegHint()
        : !rows                     ? tr("There is no motion to record yet.")
                                    : tr("Record the frame as an MP4, at 30 frames a second");
    m_record->setToolTip(tip);
    m_record->setAccessibleName(tip);
    for (QAction *action : m_moreMenu->actions()) {
        const bool needsFfmpeg = action->objectName() != QLatin1String("motionSavePng");
        action->setEnabled(!m_recording && rows && (!needsFfmpeg || m_canEncode));
        if (action->objectName() == QLatin1String("motionRecordGif") || action->objectName() == QLatin1String("motionRecord60"))
            action->setToolTip(m_canEncode ? QString() : FrameRecorder::ffmpegHint());
    }
    m_more->setEnabled(!m_recording);
    if (m_recording) {
        m_playButton->setEnabled(false);
        m_replayButton->setEnabled(false);
        m_loopButton->setEnabled(false);
    } else {
        m_loopButton->setEnabled(true);
    }
    m_recordedButton->setVisible(!m_recordedText.isEmpty());
    m_recordedButton->setText(m_recordedText);
    m_recordedButton->setToolTip(m_recording ? tr("Recording…") : tr("Show the file in its folder"));
}

QString MotionTimeline::suggestedName(FrameRecorder::Format format) const
{
    QString name;
    if (m_live && !m_frame.isNull()) {
        const QString host = m_live->snapshot(m_frame).url.host();
        const QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        const bool numeric = host.contains(QRegularExpression(QStringLiteral("^[0-9.:\\[\\]]+$")));
        if (!labels.isEmpty() && !numeric && host != QLatin1String("localhost"))
            name = slugOf(labels.first() == QLatin1String("www") && labels.size() > 1 ? labels.at(1) : labels.first());
    }
    if (name.isEmpty()) {
        const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(m_frame) : nullptr;
        name = slugOf(object ? object->name : QString());
    }
    if (name.isEmpty())
        name = QStringLiteral("page");
    switch (format) {
    case FrameRecorder::Format::mp4:
        return name + QStringLiteral("-motion.mp4");
    case FrameRecorder::Format::gif:
        return name + QStringLiteral("-motion.gif");
    case FrameRecorder::Format::pngFrames:
        return name + QStringLiteral("-frames");
    }
    return name;
}

QString MotionTimeline::askWhere(const RecordAsk &ask)
{
    if (chooser())
        return chooser()(ask);
    QSettings settings;
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString fallback = QFileInfo(videos).isDir() ? videos : QDir::homePath();
    const QString folder = settings.value(QStringLiteral("motion/recordFolder"), fallback).toString();
    const bool frames = ask.format == FrameRecorder::Format::pngFrames;
    const QString filter = ask.format == FrameRecorder::Format::mp4 ? tr("MP4 video (*.mp4)") : ask.format == FrameRecorder::Format::gif ? tr("GIF (*.gif)") : QString();
    const QString title = frames ? tr("Save Frames as PNG") : ask.format == FrameRecorder::Format::gif ? tr("Record GIF") : tr("Record MP4");
    QString path = QFileDialog::getSaveFileName(window(), title, QDir(QFileInfo(folder).isDir() ? folder : fallback).filePath(ask.suggested), filter);
    if (path.isEmpty())
        return {};
    settings.setValue(QStringLiteral("motion/recordFolder"), QFileInfo(path).absolutePath());
    // A name typed without its ending gets it.
    const QString ending = ask.format == FrameRecorder::Format::mp4 ? QStringLiteral(".mp4") : ask.format == FrameRecorder::Format::gif ? QStringLiteral(".gif") : QString();
    if (!ending.isEmpty() && !path.endsWith(ending, Qt::CaseInsensitive))
        path += ending;
    return path;
}

QString MotionTimeline::record(FrameRecorder::Format format, int fps)
{
    if (m_recording)
        return tr("A recording is already running.");
    if (!isOpen() || !m_live || !m_live->active(m_frame))
        return tr("Open the timeline on a page first.");
    if (m_timeline.tracks.isEmpty())
        return tr("There is no motion to record yet.");
    if (!m_live->snapshot(m_frame).motionHeld)
        return tr("The page isn't held yet. Try again in a moment.");
    if (format != FrameRecorder::Format::pngFrames && FrameRecorder::ffmpeg().isEmpty()) {
        m_canEncode = false;
        syncRecord();
        return FrameRecorder::ffmpegHint();
    }
    // Nothing is written until a file is named.
    const QString path = askWhere({format, fps, suggestedName(format)});
    if (path.isEmpty())
        return {};
    if (!m_recorder)
        m_recorder = std::make_unique<MotionRecorder>(*BrowserViews::of(m_session));
    pause();
    const bool onTime = m_timeline.hasTime();
    const double scrollMax = std::max(1.0, m_timeline.scrollMax);
    m_timeBefore = m_time;
    m_scrollBefore = m_scroll;
    // Whatever seek was waiting is stale: the recording sets the page.
    m_wantTime.reset();
    m_wantScroll.reset();
    const QUuid frame = m_frame;
    MotionRecorder::Job job;
    job.frame = frame;
    job.path = path;
    job.format = format;
    job.fps = fps;
    job.durationMs = onTime ? m_timeline.duration : scrollMotionMs;
    job.seek = [this, frame, onTime, scrollMax](double ms, std::function<void(const QString &)> done) {
        if (frame != m_frame || !m_live || !m_live->active(frame)) {
            QMetaObject::invokeMethod(this, [done] { done(tr("The page isn't held any more.")); }, Qt::QueuedConnection);
            return;
        }
        std::function<QString(LiveSession &)> command;
        if (onTime) {
            m_time = ms;
            command = [ms](LiveSession &session) { return session.motionSeek(ms); };
        } else {
            const double px = scrollMax * ms / scrollMotionMs;
            m_scroll = px;
            command = [px](LiveSession &session) { return session.motionSeekScroll(px); };
        }
        m_tracks->update();
        emit playheadChanged();
        m_live->run(frame, std::move(command), std::move(done));
    };
    disconnect(m_recorder.get(), nullptr, this, nullptr);
    connect(m_recorder.get(), &MotionRecorder::progress, this, [this](int step, int steps) {
        const double total = double(steps) * 1000.0 / m_recorder->job().fps;
        m_recordedText = tr("Recording… %1 / %2").arg(Motion::seconds(double(step) * 1000.0 / m_recorder->job().fps), Motion::seconds(total));
        syncRecord();
    });
    connect(m_recorder.get(), &MotionRecorder::finished, this, [this](const QString &error, const QString &file, double seconds) {
        m_recording = false;
        m_canvas.setEscapeHook({});
        // The playhead goes back to where the person had it.
        if (isOpen()) {
            if (m_timeline.hasTime()) {
                m_time = m_timeBefore;
                m_wantTime = m_timeBefore;
            } else {
                m_scroll = m_scrollBefore;
                m_wantScroll = m_scrollBefore;
            }
        }
        if (error.isEmpty()) {
            m_recordedPath = file;
            m_recordedText = tr("Recorded %1 s · %2").arg(QString::number(seconds, 'f', 1), QFileInfo(file).fileName());
        } else {
            m_recordedPath.clear();
            m_recordedText.clear();
        }
        syncHeader();
        if (isOpen())
            pump();
        m_tracks->update();
        emit playheadChanged();
        if (error.isEmpty()) {
            emit notice(m_recordedText);
            emit recorded(file, seconds);
        } else {
            emit notice(error);
        }
    });
    if (const QString failure = m_recorder->start(job); !failure.isEmpty())
        return failure;
    m_recording = true;
    m_recordedText = tr("Recording… %1 / %2").arg(Motion::seconds(0), Motion::seconds(double(m_recorder->total()) * 1000.0 / fps));
    m_recordedPath.clear();
    // Esc stops the recording before it leaves Edit Page.
    m_canvas.setEscapeHook([guard = QPointer<MotionTimeline>(this)] {
        if (!guard || !guard->m_recording)
            return false;
        guard->stopRecording();
        return true;
    });
    syncHeader();
    return {};
}

void MotionTimeline::stopRecording()
{
    if (m_recording && m_recorder)
        m_recorder->stop();
}

// The timeline closed, or the page went: the file is thrown away, since it was never whole.
void MotionTimeline::abortRecording()
{
    if (!m_recording)
        return;
    m_recording = false;
    if (m_recorder)
        m_recorder->abort();
    m_canvas.setEscapeHook({});
    m_recordedText.clear();
    m_recordedPath.clear();
}

void MotionTimeline::openRecorded()
{
    if (m_recording || m_recordedPath.isEmpty())
        return;
    const QFileInfo info(m_recordedPath);
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QProcess::startDetached(qEnvironmentVariable("OMASTRATOR_XDG_OPEN", QStringLiteral("xdg-open")), {folder});
}
