#pragma once
#include "Canvas/EditorCanvas.h"
#include "UI/FloatingPanel.h"
#include "UI/ProjectWorkspace.h"
#include <QAction>
#include <QLineEdit>
#include <QMenuBar>
#include <QPointer>

class AgentBridge;

// The menu bar: Illustrator's commands whose session functions exist.
class Menus : public QObject {
    Q_OBJECT
public:
    // Without a bridge the AI entries stay disabled.
    Menus(ProjectWorkspace &workspace, QMenuBar &bar, QWidget &window, AgentBridge *agent = nullptr);
    ~Menus() override;

    QAction *action(const QString &name) const;
    // A new front editor: follow its session and canvas.
    void watchFront(EditorCanvas *canvas);

signals:
    void layersToggled(bool shown);
    void propertiesToggled(bool shown);

private:
    void buildFile(QMenuBar &bar);
    void buildEdit(QMenuBar &bar);
    void buildObject(QMenuBar &bar);
    void buildSelect(QMenuBar &bar);
    void buildTypeKeys(QMenu &type);
    void buildViewAndWindow(QMenuBar &bar);
    void synchronize();
    void focusMoved(QWidget *from, QWidget *to);
    void fillRecent();
    EditorSession &session() const { return m_workspace.current().session; }
    QAction *add(QMenu *menu, const QString &name, const QString &text, const QKeySequence &shortcut, const std::function<void()> &run);
    // Each entry's own key, remapped from ShortcutSettings.
    void remap();
    // A second, fixed key beside the remappable one.
    void alias(QAction *entry, const QKeySequence &second);

    ProjectWorkspace &m_workspace;
    QWidget &m_window;
    AgentBridge *const m_agent;
    QMenu *m_recent = nullptr;
    FloatingPanel m_shortcutsPanel{QStringLiteral("keyboardShortcuts"), m_window};
    QMetaObject::Connection m_sessionWatch;
    QMetaObject::Connection m_canvasWatch;
    QMetaObject::Connection m_menuWatch;
    // The field whose undo the Edit entries drive.
    QPointer<QLineEdit> m_field;
    QPointer<EditorCanvas> m_canvas;
};
