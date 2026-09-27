#pragma once
#include "Document/EditorSession.h"
#include <QPointer>
#include <QWidget>

class QListWidget;

// Window ▸ History: every undo step by name, oldest first. Clicking a row
// undoes or redoes to just after it; the steps after the current one show dimmed.
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(EditorSession &session, QWidget *parent = nullptr);
    QListWidget *list() const { return m_list; }

private:
    void rebuild();

    QPointer<EditorSession> m_session;
    QListWidget *const m_list;
};
