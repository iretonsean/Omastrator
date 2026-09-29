#include "UI/ContextMenus.h"
#include "UI/Menus.h"
#include "UI/ObjectDialogs.h"

// Object ▸ Pages (docs/PAGES.md section 6), beside Object ▸ Artboards.
void Menus::buildPages(QMenu *object)
{
    QMenu *pages = object->addMenu(QStringLiteral("Pages"));
    pages->menuAction()->setObjectName(QStringLiteral("pagesMenu"));
    add(pages, QStringLiteral("newPage"), QStringLiteral("New Page"), QKeySequence(), [this] { session().addPage(); });
    add(pages, QStringLiteral("duplicatePage"), QStringLiteral("Duplicate Page"), QKeySequence(), [this] { session().duplicatePage(session().currentPage()); });
    add(pages, QStringLiteral("renamePage"), QStringLiteral("Rename Page…"), QKeySequence(),
        [this] { ObjectDialogs::renamePage(session(), session().currentPage(), &m_window); });
    add(pages, QStringLiteral("deletePage"), QStringLiteral("Delete Page"), QKeySequence(), [this] { session().deletePage(session().currentPage()); });
    pages->addSeparator();
    // Alt+PageDown and Alt+PageUp are no one-character chords, so they stay off the remapping list, like the artboard keys.
    add(pages, QStringLiteral("nextPage"), QStringLiteral("Next Page"), QKeySequence(Qt::ALT | Qt::Key_PageDown), [this] { session().showPage(true); });
    add(pages, QStringLiteral("previousPage"), QStringLiteral("Previous Page"), QKeySequence(Qt::ALT | Qt::Key_PageUp), [this] { session().showPage(false); });
    pages->addSeparator();
    QMenu *move = pages->addMenu(QStringLiteral("Move to Page"));
    move->menuAction()->setObjectName(QStringLiteral("moveToPageMenu"));
    connect(move, &QMenu::aboutToShow, this, [this, move] { ContextMenus::fillMoveToPage(move, session()); });
}
