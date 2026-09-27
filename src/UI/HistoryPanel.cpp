#include "UI/HistoryPanel.h"
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

HistoryPanel::HistoryPanel(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(&session), m_list(new QListWidget(this))
{
    setObjectName(QStringLiteral("historyPanel"));
    setMinimumSize(240, 280);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(8);
    m_list->setObjectName(QStringLiteral("historyList"));
    m_list->setAccessibleName(QStringLiteral("History"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    column->addWidget(m_list, 1);
    auto *note = new QLabel(QStringLiteral("Click a step to go back to it. A new edit clears the steps after it."), this);
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(note);
    // A row's index is how many steps are done at it: 0 is the document as opened.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!m_session)
            return;
        m_session->stepHistory(m_list->row(item) - int(m_session->undoNames().size()));
    });
    connect(&session, &EditorSession::changed, this, &HistoryPanel::rebuild);
    rebuild();
}

void HistoryPanel::rebuild()
{
    if (!m_session)
        return;
    const std::vector<QString> done = m_session->undoNames(), undone = m_session->redoNames();
    const int rows = 1 + int(done.size() + undone.size());
    const QSignalBlocker quiet(m_list);
    // Rows are refilled only when the steps changed, so a click keeps its place.
    QStringList names{QStringLiteral("Open")};
    for (const QString &name : done)
        names << name;
    for (const QString &name : undone)
        names << name;
    bool same = m_list->count() == rows;
    for (int row = 0; same && row < rows; ++row)
        same = m_list->item(row)->text() == names.at(row);
    if (!same) {
        m_list->clear();
        m_list->addItems(names);
    }
    QColor dim = palette().color(QPalette::PlaceholderText);
    for (int row = 0; row < rows; ++row) {
        QListWidgetItem *item = m_list->item(row);
        const bool ahead = row > int(done.size());
        item->setForeground(ahead ? QBrush(dim) : QBrush());
        QFont font = item->font();
        font.setItalic(ahead);
        item->setFont(font);
    }
    m_list->setCurrentRow(int(done.size()));
    m_list->scrollToItem(m_list->currentItem());
}
