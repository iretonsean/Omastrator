#pragma once
#include <QPointer>
#include <QWidget>
#include <functional>

class EditorSession;
class QListWidget;
class QListWidgetItem;
class QToolButton;

// Window ▸ Type Styles: the document's paragraph and character styles. A click
// applies one to the selection, a double-click renames it, and the right-click
// menu redefines or deletes it. The style in use is marked, with "+" when overridden.
class TextStylesPanel : public QWidget {
    Q_OBJECT
public:
    // `session` is asked again each time: it follows the front tab.
    explicit TextStylesPanel(std::function<EditorSession *()> session, QWidget *parent = nullptr);
    // Watches the front tab's session; call when it changes.
    void follow();

private:
    void rebuild();
    void applyItem(QListWidgetItem *item);
    void showMenu(const QPoint &at);

    const std::function<EditorSession *()> m_sessionOf;
    QPointer<EditorSession> m_session;
    QMetaObject::Connection m_watch;
    QListWidget *const m_list;
    QToolButton *const m_add;
    QToolButton *const m_delete;
    bool m_rebuilding = false;
};
