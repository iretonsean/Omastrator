#pragma once
#include "Document/EditorSession.h"
#include "UI/PanelSection.h"

class QListWidget;
class QListWidgetItem;
class QToolButton;

// The Pages list at the top of Layers (docs/PAGES.md section 6): a folding heading with a New Page
// button, then one 24 px row per page, five rows before it scrolls.
class PagesList : public PanelSection {
    Q_OBJECT
public:
    explicit PagesList(EditorSession &session, QWidget *parent = nullptr);
    QListWidget &rows() const { return *m_rows; }
    // The row's menu, built for that page (or the current one when null).
    QMenu *menuFor(const QUuid &page);
    static constexpr int rowHeight = 24;
    static constexpr int visibleRows = 5;

private:
    void synchronize();
    void showMenu(const QPoint &at);
    bool editable() const;
    QUuid pageOf(const QListWidgetItem *item) const;
    void beginRename(const QUuid &page);

    EditorSession &m_session;
    QListWidget *m_rows;
    QToolButton *m_add;
    bool m_syncing = false;
};
