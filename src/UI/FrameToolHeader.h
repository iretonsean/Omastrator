#pragma once
#include "UI/ToolHeaderStyle.h"
#include <QPointer>
#include <QUuid>
#include <optional>

class EditorCanvas;
class EditorSession;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

// The island with the Frame tool (docs/WINDOW-LAYOUT.md): a size to drop, the selected frames' clipping and auto layout,
// and the selected frame's Browser View switch; while it's on, its server state and Live controls (docs/BROWSER-VIEW.md, 10).
class FrameToolHeader : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit FrameToolHeader(EditorSession &session, QWidget *parent = nullptr);
    // The canvas the switch and Edit Page go through, as the pill and Object ▸ Browser View do. Without one the switch
    // still works, through BrowserViews.
    void setCanvas(EditorCanvas *canvas);

private:
    void synchronize();
    // The one frame the Browser View controls act on: the single selected frame, or the one in Edit Page.
    std::optional<QUuid> browserFrame() const;
    void flip();
    QPushButton *liveButton(const QString &name, const QString &text, const QString &tip);

    EditorSession &m_session;
    QComboBox *const m_size;
    QCheckBox *const m_clip;
    QPushButton *const m_autoLayout;
    QLabel *const m_hint;
    QCheckBox *const m_browserView;
    // Shown only while the frame's Browser View is on.
    QWidget *const m_live;
    QLabel *m_server = nullptr;
    QPushButton *m_editPage = nullptr;
    QPushButton *m_stopLive = nullptr;
    QPointer<EditorCanvas> m_canvas;
};
