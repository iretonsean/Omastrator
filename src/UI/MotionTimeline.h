#pragma once
#include "IO/FrameRecorder.h"
#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class EditorCanvas;
class EditorSession;
class LiveFrames;
class MotionRecorder;
class QHBoxLayout;
class QMenu;
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
    // Seeks and answers are in flight (tests wait for them to settle).
    bool busy() const { return m_inFlight || m_wantTime || m_wantScroll; }

    // Record (docs/MOTION.md, section 8): the motion played by seeking, at a fixed frame rate, into a file the person names.
    // `where` is what the save dialog is asked; the answer is the file (a new folder for PNG frames), or empty to cancel.
    struct RecordAsk {
        FrameRecorder::Format format = FrameRecorder::Format::mp4;
        int fps = 30;
        QString suggested;
    };
    using RecordChooser = std::function<QString(const RecordAsk &where)>;
    // Answers the save dialog in place of it; tests set it. An empty function puts the dialog back.
    static void setRecordChooser(RecordChooser chooser);
    // A name typed without its ending gets it, and the dialog only asked about the name as typed: when the new name is a file that
    // exists, this asks whether to replace it (true does). Tests set it; unset, a question box asks.
    using ReplaceChooser = std::function<bool(const QString &path)>;
    static void setReplaceChooser(ReplaceChooser chooser);
    // "northlight-motion.mp4": the page's site (else the frame's name), then what it is.
    QString suggestedName(FrameRecorder::Format format) const;
    // Asks where, then records. Returns why it can't start, or empty (a cancelled dialog is empty too).
    QString record(FrameRecorder::Format format, int fps = 30);
    // Ends the file where it is. The step in flight finishes first; recorded() follows.
    void stopRecording();
    bool isRecording() const { return m_recording; }
    // What the header says after a recording, "Recorded 2.1 s · northlight-motion.mp4", until the next action; and where.
    QString recordedText() const { return m_recordedText; }
    QString recordedPath() const { return m_recordedPath; }

signals:
    void opened();
    void closed();
    // The rows or the selection changed.
    void changed();
    // The playhead moved (a scrub, Play, or the page scrolling).
    void playheadChanged();
    // Something to say in the status line.
    void notice(const QString &text);
    // A recording is whole (`seconds` long), or ended in the file with this name.
    void recorded(const QString &path, double seconds);

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
    // Record's header controls and steps (MotionTimeline+Record.cpp).
    void buildRecord(QWidget *header, QHBoxLayout *row);
    void syncRecord();
    QString askWhere(const RecordAsk &ask);
    void abortRecording();
    void openRecorded();

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
    bool m_scrubbing = false;
    bool m_code = false;
    QList<MotionCode::Block> m_allBlocks;
    QString m_project;

    std::unique_ptr<MotionRecorder> m_recorder;
    bool m_recording = false;
    bool m_canEncode = false;
    // Where the playhead was before a recording moved it.
    double m_timeBefore = 0;
    double m_scrollBefore = 0;
    QString m_recordedText;
    QString m_recordedPath;
    QToolButton *m_record = nullptr;
    QToolButton *m_more = nullptr;
    QToolButton *m_recordedButton = nullptr;
    QMenu *m_moreMenu = nullptr;

    QToolButton *m_playButton = nullptr;
    QToolButton *m_loopButton = nullptr;
    QToolButton *m_replayButton = nullptr;
    QToolButton *m_timelineTab = nullptr;
    QToolButton *m_codeTab = nullptr;
    QLabel *m_timeText = nullptr;
    QLabel *m_trigger = nullptr;
    QStackedWidget *m_pages = nullptr;
    MotionTrackView *m_tracks = nullptr;
    QPlainTextEdit *m_codeView = nullptr;
};
