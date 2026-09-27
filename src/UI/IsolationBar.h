#pragma once
#include "Document/EditorSession.h"
#include <QWidget>

class QHBoxLayout;

// Isolation mode's bar above the canvas: a back arrow and the path from the
// layer to the group being edited. Each crumb steps back out to it.
class IsolationBar : public QWidget {
    Q_OBJECT
public:
    explicit IsolationBar(EditorSession &session, QWidget *parent = nullptr);

private:
    void rebuild();

    EditorSession &m_session;
    QHBoxLayout *const m_row;
    std::vector<QUuid> m_shown;
};
