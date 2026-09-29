#include "UI/PagesList.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QListWidget>
#include <QMenu>
#include <QToolButton>

namespace {
constexpr int idRole = Qt::UserRole;
}

PagesList::PagesList(EditorSession &session, QWidget *parent)
    : PanelSection(QStringLiteral("Pages"), QStringLiteral("pages"), parent, false, QStringLiteral("layers/collapsed/pages")),
      m_session(session), m_rows(new QListWidget(this)), m_add(new QToolButton(this))
{
    setObjectName(QStringLiteral("pagesList"));
    layout()->setContentsMargins(12, 8, 12, 8);
    m_add->setObjectName(QStringLiteral("newPage"));
    m_add->setText(QStringLiteral("+"));
    m_add->setAccessibleName(QStringLiteral("New Page"));
    m_add->setToolTip(QStringLiteral("New Page"));
    m_add->setAutoRaise(true);
    m_add->setFixedSize(24, 24);
    trailing->addWidget(m_add);
    connect(m_add, &QToolButton::clicked, this, [this] { m_session.addPage(); });

    m_rows->setObjectName(QStringLiteral("pagesRows"));
    m_rows->setAccessibleName(QStringLiteral("Pages"));
    m_rows->setFrameShape(QFrame::NoFrame);
    m_rows->setUniformItemSizes(true);
    m_rows->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_rows->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rows->setDragDropMode(QAbstractItemView::InternalMove);
    m_rows->setDefaultDropAction(Qt::MoveAction);
    m_rows->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_rows->setContextMenuPolicy(Qt::CustomContextMenu);
    body->addWidget(m_rows);

    // A press switches, which is also how a drag starts from the page it grabs.
    connect(m_rows, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        if (!m_syncing && (QApplication::mouseButtons() & Qt::LeftButton))
            m_session.setCurrentPage(pageOf(item));
    });
    // The arrow keys switch too; a refused switch (a proposal) puts the row back on the next sync.
    connect(m_rows, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!m_syncing && item)
            m_session.setCurrentPage(pageOf(item));
    });
    connect(m_rows, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (editable())
            beginRename(pageOf(item));
    });
    connect(m_rows, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_syncing)
            m_session.renamePage(pageOf(item), item->text());
        synchronize();
    });
    connect(m_rows->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex &, int start, int, const QModelIndex &, int row) {
        if (m_syncing)
            return;
        const int to = row > start ? row - 1 : row;
        m_session.movePage(pageOf(m_rows->item(to)), to);
    });
    connect(m_rows, &QWidget::customContextMenuRequested, this, &PagesList::showMenu);
    connect(&m_session, &EditorSession::changed, this, &PagesList::synchronize);
    summary = [this] {
        const std::optional<VectorDocument> &document = m_session.document();
        if (!document)
            return QString();
        return document->allPages()[size_t(std::max(0, document->pageIndex(document->currentPageId())))].name;
    };
    synchronize();
}

bool PagesList::editable() const
{
    return m_session.document() && !m_session.isDocumentLocked() && !m_session.isProposalOpen();
}

QUuid PagesList::pageOf(const QListWidgetItem *item) const
{
    return item ? item->data(idRole).toUuid() : QUuid();
}

void PagesList::synchronize()
{
    const std::optional<VectorDocument> &document = m_session.document();
    setVisible(document.has_value());
    m_add->setEnabled(editable());
    if (!document)
        return;
    // A locked document or a pending proposal keeps the list read-only: no reorder, no rename.
    m_rows->setDragDropMode(editable() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    const std::vector<Page> pages = document->allPages();
    const QUuid current = document->currentPageId();
    const QSignalBlocker blocker(m_rows);
    m_syncing = true;
    // Rows are kept when the pages match, so a drag or an open editor isn't disturbed.
    bool same = m_rows->count() == int(pages.size());
    for (int index = 0; same && index < int(pages.size()); ++index)
        same = pageOf(m_rows->item(index)) == pages[size_t(index)].id;
    if (!same) {
        m_rows->clear();
        for (const Page &page : pages) {
            auto *item = new QListWidgetItem(page.name, m_rows);
            item->setData(idRole, page.id);
            item->setSizeHint(QSize(0, rowHeight));
        }
    }
    for (int index = 0; index < int(pages.size()); ++index) {
        QListWidgetItem *item = m_rows->item(index);
        item->setFlags(editable() ? item->flags() | Qt::ItemIsEditable : item->flags() & ~Qt::ItemFlags(Qt::ItemIsEditable));
        if (item->text() != pages[size_t(index)].name)
            item->setText(pages[size_t(index)].name);
        item->setToolTip(pages[size_t(index)].name);
        if (pages[size_t(index)].id == current) {
            m_rows->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    m_rows->setFixedHeight(std::min(int(pages.size()), visibleRows) * rowHeight + 2);
    m_syncing = false;
    refreshSummary();
}

void PagesList::beginRename(const QUuid &page)
{
    for (int index = 0; index < m_rows->count(); ++index) {
        if (pageOf(m_rows->item(index)) == page) {
            m_rows->editItem(m_rows->item(index));
            return;
        }
    }
}

QMenu *PagesList::menuFor(const QUuid &page)
{
    const QUuid target = page.isNull() ? m_session.currentPage() : page;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const bool open = editable();
    QAction *add = menu->addAction(QStringLiteral("New Page"), this, [this] { m_session.addPage(); });
    add->setObjectName(QStringLiteral("newPage"));
    add->setEnabled(open);
    QAction *duplicate = menu->addAction(QStringLiteral("Duplicate Page"), this, [this, target] { m_session.duplicatePage(target); });
    duplicate->setObjectName(QStringLiteral("duplicatePage"));
    duplicate->setEnabled(open);
    QAction *rename = menu->addAction(QStringLiteral("Rename…"), this, [this, target] { beginRename(target); });
    rename->setObjectName(QStringLiteral("renamePage"));
    rename->setEnabled(open);
    menu->addSeparator();
    QAction *remove = menu->addAction(QStringLiteral("Delete Page"), this, [this, target] { m_session.deletePage(target); });
    remove->setObjectName(QStringLiteral("deletePage"));
    remove->setEnabled(open && m_session.document()->pageCount() > 1);
    return menu;
}

void PagesList::showMenu(const QPoint &at)
{
    const QListWidgetItem *item = m_rows->itemAt(at);
    if (!item)
        return;
    menuFor(pageOf(item))->popup(m_rows->viewport()->mapToGlobal(at));
}
