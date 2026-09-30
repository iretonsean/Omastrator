#include "Document/PathOperations.h"
#include "UI/ProjectWorkspaceView.h"
#include "../TemporaryConfig.h"
#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// The window: tab strip, title, editor swaps, toolbar, closing.
namespace {
QMessageBox *alert(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return qobject_cast<QMessageBox *>(widget);
    }
    return nullptr;
}

void click(QMessageBox &box, const QString &text)
{
    for (QAbstractButton *button : box.buttons()) {
        if (button->text() == text)
            button->click();
    }
}

QStringList titles(ProjectWorkspaceView &window)
{
    QStringList result;
    for (ProjectTabButton *button : window.tabs()->buttons())
        result << button->text();
    return result;
}
}

class ProjectWorkspaceViewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void theStripFollowsTheTabsWithTheirDots();
    void theTitleNamesTheFrontDocument();
    void aNewFrontTabGetsANewEditor();
    void zoomsNeedADocument();
    void closingAsksAboutChanges();
};

void ProjectWorkspaceViewTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
}

void ProjectWorkspaceViewTests::theStripFollowsTheTabsWithTheirDots()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    QCOMPARE(titles(window), QStringList{"Untitled"});
    workspace.createDocument(QSizeF(100, 100));
    workspace.createDocument(QSizeF(50, 50));
    QCOMPARE(titles(window), (QStringList{"Untitled", "Untitled 2"}));
    QVERIFY(window.tabs()->buttons().back()->isActive());
    QVERIFY(!window.tabs()->buttons().front()->isActive());
    // An edit puts the dot before its title.
    workspace.current().session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    QCOMPARE(titles(window), (QStringList{"Untitled", "● Untitled 2"}));
    // Buttons are kept for the tabs that stay.
    ProjectTabButton *first = window.tabs()->buttons().front();
    workspace.select(workspace.tabs().front()->id);
    QCOMPARE(window.tabs()->buttons().front(), first);
    QVERIFY(first->isActive());
    bool closed = false;
    workspace.close(workspace.tabs().front()->id, [&closed] { closed = true; });
    QTRY_VERIFY(closed);
    QCOMPARE(titles(window), QStringList{"● Untitled 2"});
}

void ProjectWorkspaceViewTests::theTitleNamesTheFrontDocument()
{
    QTemporaryDir directory;
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    QCOMPARE(window.windowTitle(), QString("Untitled[*] — Omastrator"));
    QVERIFY(!window.isWindowModified());
    workspace.createDocument(QSizeF(100, 100));
    workspace.current().session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    QVERIFY(window.isWindowModified());
    const QString path = directory.filePath("Flyer.omai");
    QVERIFY(workspace.saveTo(workspace.current(), path));
    QCOMPARE(window.windowTitle(), QString("Flyer[*] — Omastrator"));
    QVERIFY(!window.isWindowModified());
    QCOMPARE(window.windowFilePath(), path);
}

void ProjectWorkspaceViewTests::aNewFrontTabGetsANewEditor()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    ContentView *first = window.content();
    QVERIFY(first);
    QCOMPARE(&first->canvas().session(), &workspace.current().session);
    // A document in the reused tab keeps its editor.
    workspace.createDocument(QSizeF(100, 100));
    QCOMPARE(window.content(), first);
    workspace.createDocument(QSizeF(10, 10));
    QVERIFY(window.content() != first);
    QCOMPARE(&window.content()->canvas().session(), &workspace.current().session);
    QCOMPARE(window.centralWidget(), window.content());
    // Managing disables the editor until the question ends.
    bool closed = false;
    workspace.current().session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    workspace.close(workspace.current().id, [&closed] { closed = true; });
    QVERIFY(!window.content()->isEnabled());
    click(*alert("saveChangesAlert"), QStringLiteral("Cancel"));
    QTRY_VERIFY(closed);
    QVERIFY(window.content()->isEnabled());
}

void ProjectWorkspaceViewTests::zoomsNeedADocument()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    const QStringList zooms{"fitToolbar", "actualSizeToolbar", "zoomInToolbar", "zoomOutToolbar"};
    for (const QString &name : zooms)
        QVERIFY(!window.findChild<QAction *>(name)->isEnabled());
    workspace.createDocument(QSizeF(400, 400));
    for (const QString &name : zooms)
        QVERIFY(window.findChild<QAction *>(name)->isEnabled());
    EditorSession &session = workspace.current().session;
    const double fitted = session.viewport.zoom();
    window.findChild<QAction *>("zoomInToolbar")->trigger();
    QCOMPARE(session.viewport.zoom(), fitted * 2);
    window.findChild<QAction *>("actualSizeToolbar")->trigger();
    QCOMPARE(session.viewport.zoom(), 1.0);
    // The plus opens a welcome tab.
    window.findChild<QAction *>("newTabToolbar")->trigger();
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(!workspace.current().session.hasDocument());
}

void ProjectWorkspaceViewTests::closingAsksAboutChanges()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    workspace.createDocument(QSizeF(100, 100));
    workspace.current().session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    // The window stays until the workspace hears an answer.
    QVERIFY(!window.close());
    QVERIFY(window.isVisible());
    QVERIFY(alert("saveChangesAlert"));
    click(*alert("saveChangesAlert"), QStringLiteral("Cancel"));
    QTest::qWait(10);
    QVERIFY(window.isVisible());
    QVERIFY(!window.close());
    click(*alert("saveChangesAlert"), QStringLiteral("Don’t Save"));
    QTRY_VERIFY(!window.isVisible());
}

QTEST_MAIN(ProjectWorkspaceViewTests)
#include "ProjectWorkspaceViewTests.moc"
