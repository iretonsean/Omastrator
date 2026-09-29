#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include "UI/PagesList.h"
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// The Pages list in Layers: rows, switching, renaming, reordering, the menu, folding.
class PagesListTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup() { QSettings().clear(); }
    void oneRowWithOnePage();
    void plusAddsAPage();
    void clickingARowSwitchesWithoutAStep();
    void renamingCommitsThroughTheSession();
    void anEmptyRenameSnapsBack();
    void draggingReordersPages();
    void menuActs();
    void deleteIsDisabledOnTheLastPage();
    void scrollsAfterFiveRows();
    void foldsAndRemembersItsState();
    void sitsAtTheTopOfLayers();
    void aLockedDocumentOrAProposalMakesItReadOnly();
    void arrowKeysSwitchPagesAndF2Renames();
};

void PagesListTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

namespace {
QStringList names(const PagesList &list)
{
    QStringList result;
    for (int index = 0; index < list.rows().count(); ++index)
        result << list.rows().item(index)->text();
    return result;
}

void press(PagesList &list, int row)
{
    QListWidget &rows = list.rows();
    QTest::mouseClick(rows.viewport(), Qt::LeftButton, {}, rows.visualItemRect(rows.item(row)).center());
}
}

void PagesListTests::oneRowWithOnePage()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    PagesList list(session);
    list.show();
    QCOMPARE(names(list), QStringList{"Page 1"});
    QCOMPARE(list.rows().item(0)->sizeHint().height(), 24);
    QCOMPARE(list.rows().currentRow(), 0);
    QVERIFY(list.findChild<QToolButton *>("newPage")->size() == QSize(24, 24));
}

void PagesListTests::plusAddsAPage()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    PagesList list(session);
    list.findChild<QToolButton *>("newPage")->click();
    QCOMPARE(names(list), (QStringList{"Page 1", "Page 2"}));
    QCOMPARE(list.rows().currentRow(), 1);
    QVERIFY(session.canUndo());
    QCOMPARE(session.undoName(), QString("New Page"));
    session.undo();
    QCOMPARE(names(list), QStringList{"Page 1"});
}

void PagesListTests::clickingARowSwitchesWithoutAStep()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    const QUuid first = session.currentPage();
    session.addPage();
    session.markSaved();
    PagesList list(session);
    list.show();
    press(list, 0);
    QCOMPARE(session.currentPage(), first);
    QCOMPARE(list.rows().currentRow(), 0);
    QCOMPARE(session.undoName(), QString("New Page"));
    QVERIFY(!session.isModified());
}

void PagesListTests::renamingCommitsThroughTheSession()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage();
    PagesList list(session);
    list.show();
    QListWidgetItem *item = list.rows().item(1);
    item->setText(QStringLiteral("Cover"));
    QCOMPARE(session.document()->allPages()[1].name, QString("Cover"));
    QCOMPARE(session.undoName(), QString("Rename Page"));
    // A name taken already gets a number, and the row shows it.
    list.rows().item(0)->setText(QStringLiteral("Cover"));
    QCOMPARE(names(list), (QStringList{"Cover 2", "Cover"}));
}

void PagesListTests::anEmptyRenameSnapsBack()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    PagesList list(session);
    list.rows().item(0)->setText(QStringLiteral("  "));
    QCOMPARE(names(list), QStringList{"Page 1"});
    QVERIFY(!session.canUndo());
}

void PagesListTests::draggingReordersPages()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage();
    session.addPage();
    PagesList list(session);
    const QUuid third = session.currentPage();
    // What a drop does to the model: the third row moves to the front.
    QVERIFY(list.rows().model()->moveRow({}, 2, {}, 0));
    QCOMPARE(session.document()->allPages().front().id, third);
    QCOMPARE(session.undoName(), QString("Reorder Pages"));
    QCOMPARE(names(list), (QStringList{"Page 3", "Page 1", "Page 2"}));
    QCOMPARE(session.currentPage(), third);
}

void PagesListTests::menuActs()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage();
    PagesList list(session);
    const QUuid first = session.document()->allPages().front().id;
    std::unique_ptr<QMenu> menu(list.menuFor(first));
    menu->setAttribute(Qt::WA_DeleteOnClose, false);
    QCOMPARE(menu->actions().size(), 5);
    menu->findChild<QAction *>("duplicatePage")->trigger();
    QCOMPARE(names(list), (QStringList{"Page 1", "Page 1 Copy", "Page 2"}));
    QCOMPARE(session.undoName(), QString("Duplicate Page"));
    std::unique_ptr<QMenu> again(list.menuFor(first));
    again->setAttribute(Qt::WA_DeleteOnClose, false);
    again->findChild<QAction *>("deletePage")->trigger();
    QCOMPARE(names(list), (QStringList{"Page 1 Copy", "Page 2"}));
    std::unique_ptr<QMenu> more(list.menuFor({}));
    more->setAttribute(Qt::WA_DeleteOnClose, false);
    more->findChild<QAction *>("newPage")->trigger();
    QCOMPARE(names(list).size(), 3);
}

void PagesListTests::deleteIsDisabledOnTheLastPage()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    PagesList list(session);
    std::unique_ptr<QMenu> menu(list.menuFor({}));
    menu->setAttribute(Qt::WA_DeleteOnClose, false);
    QVERIFY(!menu->findChild<QAction *>("deletePage")->isEnabled());
    session.addPage();
    std::unique_ptr<QMenu> two(list.menuFor({}));
    two->setAttribute(Qt::WA_DeleteOnClose, false);
    QVERIFY(two->findChild<QAction *>("deletePage")->isEnabled());
}

void PagesListTests::scrollsAfterFiveRows()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    PagesList list(session);
    for (int page = 0; page < 3; ++page)
        session.addPage();
    const int four = list.rows().height();
    session.addPage();
    QCOMPARE(list.rows().height(), four + PagesList::rowHeight);
    const int five = list.rows().height();
    session.addPage();
    session.addPage();
    QCOMPARE(list.rows().count(), 7);
    QCOMPARE(list.rows().height(), five);
}

void PagesListTests::foldsAndRemembersItsState()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage(QStringLiteral("Cover"));
    {
        PagesList list(session);
        list.show();
        QVERIFY(!list.isCollapsed());
        list.toggle()->click();
        QVERIFY(list.isCollapsed());
        QVERIFY(QSettings().value("layers/collapsed/pages").toBool());
        QVERIFY(!QSettings().contains("properties/collapsed/pages"));
        // Folded, the heading says which page you're on.
        QCOMPARE(list.summaryText(), QString("Cover"));
    }
    PagesList again(session);
    QVERIFY(again.isCollapsed());
}

void PagesListTests::sitsAtTheTopOfLayers()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    LayersPanel panel(session);
    panel.show();
    QVERIFY(panel.pages().isVisible());
    QVERIFY(panel.pages().geometry().bottom() < panel.list().geometry().top());
    QCOMPARE(panel.pages().rows().count(), 1);
}

QTEST_MAIN(PagesListTests)
#include "PagesListTests.moc"

void PagesListTests::aLockedDocumentOrAProposalMakesItReadOnly()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage();
    PagesList list(session);
    list.show();
    QToolButton *add = list.findChild<QToolButton *>("newPage");
    QVERIFY(add->isEnabled());
    session.setDocumentLocked(true);
    QVERIFY(!add->isEnabled());
    QCOMPARE(list.rows().dragDropMode(), QAbstractItemView::NoDragDrop);
    for (const char *name : {"newPage", "duplicatePage", "renamePage", "deletePage"}) {
        std::unique_ptr<QMenu> menu(list.menuFor({}));
        menu->setAttribute(Qt::WA_DeleteOnClose, false);
        QVERIFY2(!menu->findChild<QAction *>(name)->isEnabled(), name);
    }
    // Switching pages is only looking, so it still works.
    press(list, 0);
    QCOMPARE(session.currentPage(), session.document()->allPages()[0].id);
    session.setDocumentLocked(false);
    QVERIFY(add->isEnabled());
    session.beginInteraction(EditorSession::proposalPrefix() + QStringLiteral("Test"));
    std::unique_ptr<QMenu> menu(list.menuFor({}));
    menu->setAttribute(Qt::WA_DeleteOnClose, false);
    QVERIFY(!menu->findChild<QAction *>("newPage")->isEnabled());
    press(list, 1);
    QCOMPARE(session.currentPage(), session.document()->allPages()[0].id);
    session.cancelInteraction();
}

void PagesListTests::arrowKeysSwitchPagesAndF2Renames()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 100));
    session.addPage();
    PagesList list(session);
    list.show();
    press(list, 1);
    const size_t steps = session.undoNames().size();
    QTest::keyClick(list.rows().viewport(), Qt::Key_Up);
    QTest::keyClick(&list.rows(), Qt::Key_Up);
    QCOMPARE(session.currentPage(), session.document()->allPages()[0].id);
    QCOMPARE(session.undoNames().size(), steps);
    QTest::keyClick(&list.rows(), Qt::Key_F2);
    QVERIFY(list.rows().findChild<QLineEdit *>());
}
