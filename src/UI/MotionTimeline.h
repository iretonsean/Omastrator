#pragma once
#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <optional>

class EditorCanvas;
class EditorSession;
class LiveFrames;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class MotionTrackView;

// The timeline docked under the canvas (docs/MOTION.md, section 2): one row per element and property, a playhead that
// scrubs the live page, Play, Loop and Replay. It holds the Browser View's page through Live while it is open, and lets
// it go when it closes. Nothing here is a document edit.
class MotionTimeline : public QWidget {
    Q_OBJECT
public:
    MotionTimeline(EditorSession &session, EditorCanvas &canvas, QWidget *parent = nullptr);
    ~MotionTimeline() override;

    // The document's timeline, made with its editor.
    static MotionTimeline *of(EditorSession &session);
    // Opens the file behind the Code tab with this in place of xdg-open (OMASTRATOR_XDG_OPEN also replaces it); tests set it.
    static void setOpener(std::function<void(const QString &path)> opener);

    // Opens on the frame if it isn't, and plays the new page from 0 once it has loaded: what Animate does when the agent's
    // motion is ready to preview. Returns why it can't open, or empty.
    QString openAndPlay(const QUuid &frame);
    // Window ▸ Timeline: closes it when open, else opens it on the selection and says why it can't.
    void toggle();
    // Opens on the Browser View in Edit Page, else the selected one, else the page's only one. Returns why it can't, or empty.
    QString openForSelection();
    // Opens on this frame: Edit Page begins on it, and the page is held once Live runs there.
    QString open(const QUuid &frame);
    void close();
    bool isOpen() const { return !m_frame.isNull(); }
    QUuid frame() const { return m_frame; }

    const Motion::Timeline &timeline() const { return m_timeline; }
    // What the header says about the page while it is starting or has nothing to show.
    QString status() const { return m_status; }

    // The playhead on time (ms), and on the scroll axis (px).
    double playhead() const { return m_time; }
    double scrollPlayhead() const { return m_scroll; }
    void scrubTo(double ms);
    void scrubScrollTo(double px);
    void play();
    void pause();
    void replay();
    bool isPlaying() const { return m_playing; }
    void setLooping(bool loop);
    bool isLooping() const { return m_loop; }

    // The selected row; picking a row selects its elements on the page (and holds a transition's state).
    QString selectedId() const { return m_selected; }
    const Motion::Track *selectedTrack() const { return m_timeline.find(m_selected); }
    void selectRow(const QString &id);
    // The blocks the Code tab shows, for the selected row (all when none), and whether the tab is showing.
    QList<MotionCode::Block> codeBlocks() const;
    void showTab(bool code);
    bool codeShown() const { return m_code; }
    // Save to code and Discard, while the frame shows the agent's motion as a preview (docs/MOTION.md, section 4).
    bool previewing() const;
    // The line the inspector shows when what the agent wrote breaks the output contract; empty when it holds.
    QString previewNotice() const;
    void saveToCode();
    void discardPreview();
    // Preview reduced: the page plays as for a visitor who asked for less motion, and the rows show what is left.
    void setPreviewReduced(bool on);
    bool previewReduced() const { return m_previewReduced; }
    // What starts the selected row's motion: "load", "scroll", "hover" or "click". It shows on the page where it can (load,
    // scroll) and is an edit the agent finishes.
    void setStarts(const QString &trigger);
    // Asks the agent about the picked elements, in the frame's project. Returns why it can't, or empty.
    QString askAgent(const QString &prompt);
    // Edits (docs/MOTION.md, section 3). Each is shown on the page as a preview while `preview` is true, and is one Live edit,
    // with its undo, when it is not. The inspector's fields call them. A motion token (--duration-*, --ease-*, --stagger-*) is
    // a custom property on :root.
    void setToken(const QString &property, const QString &value, bool preview);
    // The selected row's @keyframes block: one keyframe ("from", "to", "40%") and one of its properties.
    void setKeyframe(const QString &frame, const QString &property, const QString &value);
    // The selected row's duration, delay or easing for motion the code holds in no token; the agent writes it.
    void setTiming(const QString &property, const QString &value);
    // The block's reduced-motion rule, taken out or put back: an edit for the code, and nothing on the page.
    void setReducedMotion(bool on);
    EditorCanvas &canvas() const { return m_canvas; }
    // What a custom property will be once the pending edits are written: the pending edit's value, else `fallback`.
    QString pendingValue(const QString &property, const QString &fallback) const;
    // Whether the selected row's block has its reduced-motion rule once the pending edits are written.
    bool reducedMotionOn() const;
    // The custom properties the selected row takes its duration, easing and stagger from, read from its block.
    MotionCode::Bindings bindings() const;
    // The block the selected row is in, or empty.
    std::optional<MotionCode::Block> blockOfSelection() const;
    // Seeks and answers are in flight (tests wait for them to settle).
    bool busy() const { return m_inFlight || m_wantTime || m_wantScroll; }

signals:
    void opened();
    void closed();
    // The rows or the selection changed.
    void changed();
    // The playhead moved (a scrub, Play, or the page scrolling).
    void playheadChanged();
    // Something to say in the status line.
    void notice(const QString &text);

private:
    friend class MotionTrackView;
    LiveFrames *live() const;
    void onLiveChanged(const QUuid &frame);
    void onEditPage();
    void hold();
    void refresh(const QJsonObject &list);
    void seekNow();
    void pump();
    void sent();
    void release();
    void tick();
    void syncHeader();
    void rebuildCode();
    void scanCode();
    void forceRows();

    EditorSession &m_session;
    EditorCanvas &m_canvas;
    QUuid m_frame;
    QPointer<LiveFrames> m_live;
    Motion::Timeline m_timeline;
    QJsonObject m_lastList;
    QString m_status;
    QString m_selected;
    // The states held on the page for the selected row, by selector.
    QStringList m_forced;

    double m_time = 0;
    double m_scroll = 0;
    bool m_playing = false;
    bool m_loop = false;
    QElapsedTimer m_clock;
    double m_playFrom = 0;
    QTimer m_playTimer;
    // One seek is in flight at a time; the next waits for its answer and the picture, or the timeout.
    bool m_inFlight = false;
    bool m_answered = false;
    bool m_pictured = false;
    QTimer m_gate;
    std::optional<double> m_wantTime;
    std::optional<double> m_wantScroll;
    bool m_holding = false;
    bool m_wantHold = false;
    int m_holdTries = 0;
    // The pending edits when the project's code was last read: the code is read again when they change.
    size_t m_editCount = 0;
    bool m_scrubbing = false;
    bool m_previewReduced = false;
    // Play the new page from 0 when its list arrives from another origin than `m_replayOrigin`.
    bool m_replayWhen = false;
    QString m_replayOrigin;
    bool m_code = false;
    QList<MotionCode::Block> m_allBlocks;
    QString m_project;

    QToolButton *m_playButton = nullptr;
    QToolButton *m_loopButton = nullptr;
    QToolButton *m_replayButton = nullptr;
    QToolButton *m_timelineTab = nullptr;
    QToolButton *m_codeTab = nullptr;
    QToolButton *m_save = nullptr;
    QToolButton *m_discard = nullptr;
    QLabel *m_timeText = nullptr;
    QLabel *m_trigger = nullptr;
    QStackedWidget *m_pages = nullptr;
    MotionTrackView *m_tracks = nullptr;
    QPlainTextEdit *m_codeView = nullptr;
};
