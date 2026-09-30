#include "UI/MotionTimeline.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/AgentBridge.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionRecorder.h"
#include "UI/MotionTrackView.h"
#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// scheme://host:port of a page's address.
QString originOf(const QString &address)
{
    const QUrl url(address);
    return url.isValid() && !url.host().isEmpty() ? QStringLiteral("%1://%2:%3").arg(url.scheme(), url.host()).arg(url.port(-1)) : QString();
}

QHash<EditorSession *, MotionTimeline *> &registry()
{
    static QHash<EditorSession *, MotionTimeline *> all;
    return all;
}

std::function<void(const QString &)> &openerFunction()
{
    static std::function<void(const QString &)> opener;
    return opener;
}

// A seek waits this long for the picture it should show, then the next goes anyway: a seek that changes no pixel sends none.
constexpr int gateMs = 120;
constexpr int headerHeight = 34;

QToolButton *tool(const QString &text, const QString &name, const QString &tip, QWidget *parent, bool checkable = false)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setText(text);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setCheckable(checkable);
    button->setAutoRaise(true);
    // Nothing here grabs the keys: a click never moves the focus off the canvas.
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// A code view that opens the file behind the line that was clicked.
class CodeView : public QPlainTextEdit {
public:
    explicit CodeView(QWidget *parent) : QPlainTextEdit(parent) {}
    // The start line in the view of each block, and the file it came from.
    QList<std::pair<int, QString>> starts;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPlainTextEdit::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton || textCursor().hasSelection())
            return;
        const int line = cursorForPosition(event->position().toPoint()).blockNumber();
        QString path;
        for (const auto &start : std::as_const(starts))
            if (line >= start.first)
                path = start.second;
        if (path.isEmpty())
            return;
        if (openerFunction()) {
            openerFunction()(path);
            return;
        }
        QProcess::startDetached(qEnvironmentVariable("OMASTRATOR_XDG_OPEN", QStringLiteral("xdg-open")), {path});
    }
};
}

MotionTimeline *MotionTimeline::of(EditorSession &session)
{
    return registry().value(&session);
}

void MotionTimeline::setOpener(std::function<void(const QString &)> opener)
{
    openerFunction() = std::move(opener);
}

MotionTimeline::MotionTimeline(EditorSession &session, EditorCanvas &canvas, QWidget *parent) : QWidget(parent), m_session(session), m_canvas(canvas)
{
    setObjectName(QStringLiteral("motionTimeline"));
    setAccessibleName(QStringLiteral("Timeline"));
    registry().insert(&session, this);
    hide();

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto *header = new QWidget(this);
    header->setFixedHeight(headerHeight);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(10, 0, 6, 0);
    row->setSpacing(4);
    m_playButton = tool(QStringLiteral("▶"), QStringLiteral("motionPlay"), tr("Play"), header);
    m_replayButton = tool(QStringLiteral("⏮"), QStringLiteral("motionReplay"), tr("Replay from the start"), header);
    m_loopButton = tool(QStringLiteral("↻"), QStringLiteral("motionLoop"), tr("Loop"), header, true);
    m_timeText = new QLabel(header);
    m_timeText->setObjectName(QStringLiteral("motionTime"));
    m_timeText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_trigger = new QLabel(header);
    m_trigger->setObjectName(QStringLiteral("motionTrigger"));
    m_save = tool(tr("Save to code"), QStringLiteral("motionSaveToCode"), tr("Write this motion into your project's code and commit it"), header);
    m_discard = tool(tr("Discard"), QStringLiteral("motionDiscard"), tr("Drop the motion your agent wrote. Nothing has been written."), header);
    m_save->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_discard->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_save->hide();
    m_discard->hide();
    m_timelineTab = tool(tr("Timeline"), QStringLiteral("motionTabTimeline"), tr("Timeline"), header, true);
    m_codeTab = tool(tr("Code"), QStringLiteral("motionTabCode"), tr("Code"), header, true);
    m_timelineTab->setChecked(true);
    auto *close = tool(QStringLiteral("×"), QStringLiteral("motionClose"), tr("Close the timeline"), header);
    row->addWidget(m_playButton);
    row->addWidget(m_replayButton);
    row->addWidget(m_loopButton);
    row->addSpacing(6);
    row->addWidget(m_timeText);
    row->addWidget(m_trigger);
    row->addStretch(1);
    row->addWidget(m_discard);
    row->addWidget(m_save);
    buildRecord(header, row);
    row->addWidget(m_timelineTab);
    row->addWidget(m_codeTab);
    row->addWidget(close);
    column->addWidget(header);

    m_pages = new QStackedWidget(this);
    m_tracks = new MotionTrackView(*this, this);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("motionScroll"));
    scroll->setWidget(m_tracks);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    auto *code = new CodeView(this);
    code->setObjectName(QStringLiteral("motionCode"));
    code->setReadOnly(true);
    code->setLineWrapMode(QPlainTextEdit::NoWrap);
    code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    code->setFocusPolicy(Qt::ClickFocus);
    code->setFrameShape(QFrame::NoFrame);
    m_codeView = code;
    m_pages->addWidget(scroll);
    m_pages->addWidget(code);
    column->addWidget(m_pages, 1);

    m_playTimer.setInterval(15);
    m_playTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_playTimer, &QTimer::timeout, this, &MotionTimeline::tick);
    m_gate.setSingleShot(true);
    m_gate.setInterval(gateMs);
    connect(&m_gate, &QTimer::timeout, this, [this] {
        m_pictured = true;
        sent();
    });

    connect(m_playButton, &QToolButton::clicked, this, [this] { m_playing ? pause() : play(); });
    connect(m_replayButton, &QToolButton::clicked, this, &MotionTimeline::replay);
    connect(m_loopButton, &QToolButton::toggled, this, &MotionTimeline::setLooping);
    connect(m_timelineTab, &QToolButton::clicked, this, [this] { showTab(false); });
    connect(m_codeTab, &QToolButton::clicked, this, [this] { showTab(true); });
    connect(close, &QToolButton::clicked, this, &MotionTimeline::close);
    connect(m_save, &QToolButton::clicked, this, &MotionTimeline::saveToCode);
    connect(m_discard, &QToolButton::clicked, this, &MotionTimeline::discardPreview);
    connect(BrowserViews::of(m_session), &BrowserViews::previewStateChanged, this, [this](const QUuid &frame) {
        if (frame == m_frame)
            syncHeader();
    });
    connect(&m_canvas, &EditorCanvas::editPageChanged, this, &MotionTimeline::onEditPage);
    connect(BrowserViews::of(m_session), &BrowserViews::pictureArrived, this, [this](const QUuid &frame) {
        if (frame == m_frame && m_inFlight) {
            m_pictured = true;
            sent();
        }
    });
    syncHeader();
}

MotionTimeline::~MotionTimeline()
{
    registry().remove(&m_session);
    // The page plays on when the editor goes away; a Live that has ended has let it go already.
    if (!m_frame.isNull() && m_live && m_live->active(m_frame))
        m_live->run(m_frame, [](LiveSession &session) { return session.motionRelease(); });
}

LiveFrames *MotionTimeline::live() const
{
    return m_live;
}

void MotionTimeline::toggle()
{
    if (isOpen()) {
        close();
        return;
    }
    const QString failure = openForSelection();
    if (!failure.isEmpty())
        emit notice(failure);
}

QString MotionTimeline::openForSelection()
{
    if (!m_session.hasDocument())
        return tr("Open a document first.");
    if (const std::optional<QUuid> editing = m_canvas.editPageFrame())
        return open(*editing);
    const VectorDocument &document = *m_session.document();
    for (const QUuid &id : m_session.selection()) {
        const VectorObject *object = document.find(id);
        if (object && object->browser)
            return open(id);
    }
    QList<QUuid> frames;
    for (const VectorObject &object : document.objects)
        if (object.browser && document.isOnCurrentPage(object.id))
            frames.append(object.id);
    if (frames.size() == 1)
        return open(frames.first());
    return frames.isEmpty() ? tr("There is no Browser View on this page. Add one, then open the timeline.") : tr("Select a Browser View first.");
}

QString MotionTimeline::open(const QUuid &frame)
{
    if (isOpen() && frame == m_frame)
        return {};
    if (isOpen())
        close();
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return tr("That isn't a Browser View.");
    // The frame is set first: Edit Page beginning tells this timeline, which follows only its own frame.
    m_frame = frame;
    if (!m_canvas.enterEditPage(frame)) {
        m_frame = {};
        return tr("The page can't be edited yet, so it has no timeline.");
    }
    m_live = LiveFrames::of(m_session);
    connect(m_live, &LiveFrames::changed, this, &MotionTimeline::onLiveChanged, Qt::UniqueConnection);
    BrowserViews::of(m_session)->setScrubbed(frame, true);
    m_canEncode = !FrameRecorder::ffmpeg().isEmpty();
    m_recordedText.clear();
    m_wantHold = true;
    m_holdTries = 0;
    m_timeline = {};
    m_lastList = {};
    m_selected.clear();
    m_time = 0;
    m_scroll = 0;
    m_status = tr("Waiting for the page…");
    show();
    syncHeader();
    onLiveChanged(frame);
    emit opened();
    return {};
}

void MotionTimeline::close()
{
    if (m_frame.isNull())
        return;
    abortRecording();
    pause();
    const QUuid frame = std::exchange(m_frame, QUuid());
    m_wantHold = false;
    m_holding = false;
    m_inFlight = false;
    m_wantTime.reset();
    m_wantScroll.reset();
    m_gate.stop();
    m_forced.clear();
    m_scrubbing = false;
    m_previewReduced = false;
    m_replayWhen = false;
    m_selectedBar = -1;
    if (m_live && m_live->active(frame))
        m_live->run(frame, [](LiveSession &session) { return session.motionRelease(); });
    BrowserViews::of(m_session)->setScrubbed(frame, false);
    m_timeline = {};
    m_lastList = {};
    m_selected.clear();
    hide();
    emit closed();
}

// Edit Page ended: the timeline goes with it. It moved to another frame: the timeline follows.
void MotionTimeline::onEditPage()
{
    if (!isOpen())
        return;
    const std::optional<QUuid> now = m_canvas.editPageFrame();
    if (!now)
        close();
    else if (*now != m_frame)
        open(*now);
}

void MotionTimeline::onLiveChanged(const QUuid &frame)
{
    if (frame != m_frame || !m_live)
        return;
    if (!m_live->active(frame)) {
        close();
        return;
    }
    const LiveFrames::Snapshot snapshot = m_live->snapshot(frame);
    if (snapshot.state == LiveSession::State::running)
        m_status = m_previewReduced ? tr("No motion when reduced.") : tr("No motion on this page yet.");
    else
        m_status = snapshot.message.isEmpty() ? tr("Waiting for the page…") : snapshot.message;
    if (snapshot.state == LiveSession::State::running && !snapshot.motionHeld && m_wantHold && !m_holding)
        hold();
    if (snapshot.motion != m_lastList) {
        m_lastList = snapshot.motion;
        refresh(snapshot.motion);
    }
    // A Save writes the pending edits into the code: the marked blocks are read again when they change.
    if (snapshot.edits.size() != m_editCount) {
        m_editCount = snapshot.edits.size();
        scanCode();
        if (m_code)
            rebuildCode();
        emit changed();
    }
    // The page scrolled (the wheel over the frame): the scroll playhead follows, unless it is being dragged.
    const double scrolled = snapshot.geometry["scroll"].toObject()["y"].toDouble(m_scroll);
    if (!m_scrubbing && !m_inFlight && !m_wantScroll && scrolled != m_scroll) {
        m_scroll = scrolled;
        m_tracks->update();
        emit playheadChanged();
    }
    m_tracks->update();
    syncHeader();
}

void MotionTimeline::hold()
{
    m_holding = true;
    const QUuid frame = m_frame;
    m_live->run(frame, [](LiveSession &session) { return session.motionHold(); }, [this, frame](const QString &error) {
        if (frame != m_frame)
            return;
        m_holding = false;
        // The overlay may not be in the page yet (Live runs a moment before the page has loaded): ask again shortly.
        if (!error.isEmpty()) {
            m_status = error;
            if (m_wantHold && m_holdTries++ < 200)
                QTimer::singleShot(150, this, [this, frame] {
                    if (frame == m_frame && m_wantHold && !m_holding && m_live && m_live->active(frame) && !m_live->snapshot(frame).motionHeld)
                        hold();
                });
        }
        syncHeader();
    });
}

void MotionTimeline::refresh(const QJsonObject &list)
{
    const bool wasEmpty = m_timeline.tracks.isEmpty();
    m_timeline = Motion::parse(list);
    // The first list puts the playhead where the page had reached when it was held.
    if (wasEmpty) {
        m_time = std::clamp(m_timeline.time, 0.0, m_timeline.duration);
        m_scroll = m_timeline.scrollY;
        scanCode();
    } else {
        m_time = std::clamp(m_time, 0.0, m_timeline.duration);
    }
    fit();
    m_tracks->update();
    if (m_code)
        rebuildCode();
    // Animate's preview loads on a server of its own: once its page is here, the motion plays from the start.
    if (m_replayWhen && m_timeline.hasTime() && originOf(m_timeline.url) != m_replayOrigin) {
        m_replayWhen = false;
        QTimer::singleShot(0, this, &MotionTimeline::replay);
    }
    emit changed();
}

// The dock is as tall as its rows, between two and about eight of them.
void MotionTimeline::fit()
{
    const int body = std::clamp(m_tracks->contentHeight(), MotionTrackView::rulerHeight + MotionTrackView::rowHeight * 2, 260);
    setFixedHeight(headerHeight + body);
    m_tracks->updateGeometry();
}

void MotionTimeline::syncHeader()
{
    const bool rows = !m_timeline.tracks.isEmpty();
    if (rows && m_timeline.hasTime())
        m_timeText->setText(QStringLiteral("%1 / %2").arg(Motion::seconds(m_time), Motion::seconds(m_timeline.duration)));
    else if (rows)
        m_timeText->setText(QStringLiteral("%1 px / %2 px").arg(qRound(m_scroll)).arg(qRound(m_timeline.scrollMax)));
    else
        m_timeText->setText(QString());
    const QString trigger = m_timeline.trigger();
    m_trigger->setText(trigger.isEmpty() ? QString() : QStringLiteral("· ") + trigger);
    m_playButton->setText(m_playing ? QStringLiteral("⏸") : QStringLiteral("▶"));
    m_playButton->setToolTip(m_playing ? tr("Pause") : tr("Play"));
    m_playButton->setAccessibleName(m_playing ? tr("Pause") : tr("Play"));
    m_playButton->setEnabled(rows);
    m_replayButton->setEnabled(rows);
    const bool showing = previewing();
    m_save->setVisible(showing);
    m_discard->setVisible(showing);
    syncRecord();
}

void MotionTimeline::scrubTo(double ms)
{
    // A recording holds the clock.
    if (m_recording)
        return;
    // Dragging the playhead is the user taking the clock.
    if (m_playing)
        pause();
    m_time = std::clamp(ms, 0.0, std::max(0.0, m_timeline.duration));
    m_wantTime = m_time;
    m_tracks->update();
    syncHeader();
    pump();
    emit playheadChanged();
}

void MotionTimeline::scrubScrollTo(double px)
{
    if (m_recording)
        return;
    if (m_playing)
        pause();
    m_scroll = std::clamp(px, 0.0, std::max(0.0, m_timeline.scrollMax));
    m_wantScroll = m_scroll;
    m_tracks->update();
    syncHeader();
    pump();
    emit playheadChanged();
}

void MotionTimeline::pump()
{
    if (m_recording || m_inFlight || m_frame.isNull() || !m_live || !m_live->active(m_frame))
        return;
    std::function<QString(LiveSession &)> command;
    if (m_wantTime) {
        const double ms = *m_wantTime;
        m_wantTime.reset();
        command = [ms](LiveSession &session) { return session.motionSeek(ms); };
    } else if (m_wantScroll) {
        const double px = *m_wantScroll;
        m_wantScroll.reset();
        command = [px](LiveSession &session) { return session.motionSeekScroll(px); };
    } else {
        return;
    }
    m_inFlight = true;
    m_answered = false;
    m_pictured = false;
    m_gate.start();
    const QUuid frame = m_frame;
    m_live->run(frame, std::move(command), [this, frame](const QString &error) {
        if (frame != m_frame)
            return;
        m_answered = true;
        if (!error.isEmpty())
            emit notice(error);
        sent();
    });
}

// The seek was answered and its picture arrived (or the wait ran out): the next one may go.
void MotionTimeline::sent()
{
    if (!m_inFlight || !m_answered || !m_pictured)
        return;
    m_inFlight = false;
    m_gate.stop();
    pump();
}

void MotionTimeline::play()
{
    if (m_timeline.tracks.isEmpty() || m_recording)
        return;
    if (m_timeline.hasTime() && m_time >= m_timeline.duration - 1)
        m_time = 0;
    m_playFrom = m_timeline.hasTime() ? m_time : m_scroll;
    m_clock.start();
    m_playing = true;
    m_playTimer.start();
    syncHeader();
}

void MotionTimeline::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_playTimer.stop();
    syncHeader();
}

void MotionTimeline::replay()
{
    if (m_recording)
        return;
    pause();
    if (m_timeline.hasTime()) {
        m_time = 0;
        m_wantTime = 0.0;
    } else {
        m_scroll = 0;
        m_wantScroll = 0.0;
    }
    pump();
    play();
}

void MotionTimeline::setLooping(bool loop)
{
    m_loop = loop;
    if (m_loopButton->isChecked() != loop)
        m_loopButton->setChecked(loop);
}

// Play advances the playhead itself, at the picture's pace: each tick asks for the time since play began, and a seek
// still in flight makes the newer one replace the older that waits.
void MotionTimeline::tick()
{
    if (!m_playing)
        return;
    const double elapsed = double(m_clock.elapsed());
    if (m_timeline.hasTime()) {
        const double end = m_timeline.duration;
        double t = m_playFrom + elapsed;
        if (end <= 0) {
            pause();
            return;
        }
        if (t >= end) {
            if (m_loop) {
                m_playFrom = 0;
                m_clock.restart();
                t = 0;
            } else {
                t = end;
                m_time = t;
                m_wantTime = t;
                pause();
                pump();
                m_tracks->update();
                emit playheadChanged();
                return;
            }
        }
        m_time = t;
        m_wantTime = t;
    } else {
        // Only scroll-driven rows: Play scrolls the page down its range over two seconds.
        const double end = std::max(1.0, m_timeline.scrollMax);
        double at = m_playFrom + elapsed / 2000.0 * end;
        if (at >= end) {
            if (m_loop) {
                m_playFrom = 0;
                m_clock.restart();
                at = 0;
            } else {
                at = end;
                m_scroll = at;
                m_wantScroll = at;
                pause();
                pump();
                m_tracks->update();
                emit playheadChanged();
                return;
            }
        }
        m_scroll = at;
        m_wantScroll = at;
    }
    m_tracks->update();
    syncHeader();
    pump();
    emit playheadChanged();
}

void MotionTimeline::selectRow(const QString &id)
{
    selectBar(id, -1);
}

void MotionTimeline::selectBar(const QString &id, int bar)
{
    const Motion::Track *track = m_timeline.find(id);
    if (!track || !m_live || m_frame.isNull() || m_recording)
        return;
    if (bar >= track->bars.size())
        bar = -1;
    m_selected = id;
    m_selectedBar = bar;
    QStringList selectors;
    if (bar >= 0) {
        // One element of a group: only it is picked on the page.
        if (!track->bars[bar].selector.isEmpty())
            selectors << track->bars[bar].selector;
    } else {
        for (const QString &selector : track->selectors)
            if (!selector.isEmpty())
                selectors << selector;
    }
    const QUuid frame = m_frame;
    // A transition that a pointer starts is held in its state, so it can be seen and scrubbed; any other row lets go.
    const QString state = track->state();
    const QStringList wanted = state.isEmpty() ? QStringList() : selectors;
    const QStringList letGo = [&] {
        QStringList out;
        for (const QString &selector : std::as_const(m_forced))
            if (!wanted.contains(selector))
                out << selector;
        return out;
    }();
    QStringList hold;
    for (const QString &selector : wanted)
        if (!m_forced.contains(selector))
            hold << selector;
    m_forced = wanted;
    m_live->run(frame, [selectors, letGo, hold, state](LiveSession &session) {
        QString failure;
        for (const QString &selector : letGo)
            session.motionForce(selector, {});
        for (const QString &selector : hold) {
            const QString error = session.motionForce(selector, state);
            if (failure.isEmpty())
                failure = error;
        }
        if (!selectors.isEmpty()) {
            const QString error = session.selectElements(selectors);
            if (failure.isEmpty())
                failure = error;
        }
        return failure;
    }, [this, frame](const QString &error) {
        if (frame == m_frame && !error.isEmpty())
            emit notice(error);
    });
    m_tracks->update();
    if (m_code)
        rebuildCode();
    emit changed();
}

// The project's marked blocks that concern the selected row (all of them when none is selected).
QList<MotionCode::Block> MotionTimeline::codeBlocks() const
{
    const Motion::Track *track = selectedTrack();
    return track ? MotionCode::relevant(m_allBlocks, {track->name}, {}) : m_allBlocks;
}

void MotionTimeline::scanCode()
{
    QString project;
    if (m_live && !m_frame.isNull())
        project = m_live->snapshot(m_frame).project;
    m_project = project;
    m_allBlocks = project.isEmpty() ? QList<MotionCode::Block>() : MotionCode::blocks(project);
}

void MotionTimeline::showTab(bool code)
{
    m_code = code;
    m_timelineTab->setChecked(!code);
    m_codeTab->setChecked(code);
    m_pages->setCurrentIndex(code ? 1 : 0);
    if (code)
        rebuildCode();
}

void MotionTimeline::rebuildCode()
{
    auto *view = static_cast<CodeView *>(m_codeView);
    view->starts.clear();
    scanCode();
    const QList<MotionCode::Block> blocks = codeBlocks();
    if (m_project.isEmpty()) {
        view->setPlainText(tr("This page isn't one of your sites, so its code isn't here."));
        return;
    }
    if (blocks.isEmpty()) {
        view->setPlainText(m_allBlocks.isEmpty() ? tr("No motion is marked in this project's code yet.")
                                                 : tr("This motion isn't in a block Omastrator wrote. Ask to move it into one, or edit it in code."));
        return;
    }
    QStringList lines;
    lines << tr("Live code. Edits on the timeline and in the inspector rewrite these lines.") << QString();
    for (const MotionCode::Block &block : blocks) {
        view->starts.append({int(lines.size()), block.path});
        lines << QStringLiteral("%1  ·  lines %2–%3").arg(block.file).arg(block.firstLine).arg(block.lastLine);
        lines << block.text.split(QLatin1Char('\n')) << QString();
    }
    view->setPlainText(lines.join(QLatin1Char('\n')));
}
