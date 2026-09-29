#include "Document/PathOperations.h"
#include "WidgetCleanup.h"
#include "IO/ProjectStore.h"
#include "UI/ProjectWorkspace.h"
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Tabs and files: open, save, place, export, close.
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
        if (button->text() == text) {
            button->click();
            return;
        }
    }
    QFAIL(qPrintable("no button " + text));
}

void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='80' viewBox='0 0 100 80'>"
                       "<rect x='10' y='10' width='30' height='20' fill='#ff0000'/><circle cx='70' cy='40' r='15' fill='#0000ff'/></svg>";

const QByteArray excalidraw = "{\"type\":\"excalidraw\",\"version\":2,\"elements\":["
                              "{\"id\":\"r1\",\"type\":\"rectangle\",\"x\":0,\"y\":0,\"width\":30,\"height\":20,\"angle\":0,"
                              "\"strokeColor\":\"#1e1e1e\",\"backgroundColor\":\"transparent\",\"strokeWidth\":1,\"opacity\":100,"
                              "\"groupIds\":[],\"locked\":false,\"isDeleted\":false}],\"files\":{}}";

// A document with one box, as a user draws it.
void drawBox(EditorSession &session)
{
    session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
}

int leaves(const VectorDocument &document, ObjectKind kind)
{
    return int(std::count_if(document.objects.begin(), document.objects.end(), [kind](const VectorObject &each) { return each.kind == kind; }));
}
}

class ProjectWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void theFirstEmptyTabIsReused();
    void newTabReusesAWelcomeTab();
    void closingAnUnmodifiedTabRemovesIt();
    void closingAModifiedTabAsks();
    void savingWritesTheDocumentAndClearsTheDot();
    void savingDuringAHeldPreviewWritesTheDesignWidth();
    void openingTheSamePathSelectsItsTab();
    void svgsAndPicturesOpenAsDocuments();
    void excalidrawOpensAndPlaces();
    void aBadFileAlertsAndAddsNoTab();
    void placingAddsAGroupOrAnImage();
    void exportsWriteEachFormat();
    void exportRefusesAnArtboardSetNotToExport();
    void recentFilesAreNewestFirstAndLimited();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

void ProjectWorkspaceTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ProjectWorkspaceTests::init()
{
    ProjectWorkspace::clearRecent();
    m_dir = std::make_unique<QTemporaryDir>();
}

// Alerts left open would answer the next test.
void ProjectWorkspaceTests::cleanup()
{
    deleteTopLevelWidgets([](QWidget *widget) { return qobject_cast<QMessageBox *>(widget) != nullptr; });
}

void ProjectWorkspaceTests::theFirstEmptyTabIsReused()
{
    ProjectWorkspace workspace;
    QCOMPARE(int(workspace.tabs().size()), 1);
    const QUuid first = workspace.current().id;
    QCOMPARE(workspace.current().title(), QString("Untitled"));
    workspace.createDocument(QSizeF(612, 792));
    QCOMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(workspace.current().id, first);
    QCOMPARE(workspace.current().session.document().value().size, QSizeF(612, 792));
    // A document in front: a new artboard gets a tab.
    workspace.createDocument(QSizeF(100, 200));
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(workspace.current().title(), QString("Untitled 2"));
    QCOMPARE(workspace.current().session.document().value().size, QSizeF(100, 200));
    workspace.select(first);
    QCOMPARE(workspace.current().id, first);
    // Sizes of nothing are refused.
    workspace.createDocument(QSizeF(0, 10));
    QCOMPARE(int(workspace.tabs().size()), 2);
}

void ProjectWorkspaceTests::newTabReusesAWelcomeTab()
{
    ProjectWorkspace workspace;
    QSignalSpy changes(&workspace, &ProjectWorkspace::changed);
    workspace.newTab();
    QCOMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(changes.count(), 0);
    workspace.createDocument(QSizeF(50, 50));
    workspace.newTab();
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(!workspace.current().session.hasDocument());
    // A second press while on the welcome adds nothing.
    workspace.newTab();
    QCOMPARE(int(workspace.tabs().size()), 2);
}

void ProjectWorkspaceTests::closingAnUnmodifiedTabRemovesIt()
{
    ProjectWorkspace workspace;
    workspace.createDocument(QSizeF(50, 50));
    workspace.createDocument(QSizeF(60, 60));
    const QUuid second = workspace.current().id;
    bool done = false;
    workspace.close(second, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 1);
    QCOMPARE(workspace.current().session.document().value().size, QSizeF(50, 50));
    // The last tab closed leaves a fresh one.
    const QUuid last = workspace.current().id;
    done = false;
    workspace.close(last, [&done] { done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(int(workspace.tabs().size()), 1);
    QVERIFY(workspace.current().id != last);
    QVERIFY(!workspace.current().session.hasDocument());
    QCOMPARE(workspace.current().title(), QString("Untitled 3"));
}

void ProjectWorkspaceTests::closingAModifiedTabAsks()
{
    ProjectWorkspace workspace;
    workspace.createDocument(QSizeF(50, 50));
    drawBox(workspace.current().session);
    QVERIFY(workspace.current().session.isModified());
    const QUuid id = workspace.current().id;
    bool done = false;
    workspace.close(id, [&done] { done = true; });
    QVERIFY(workspace.isManaging());
    QMessageBox *asked = alert("saveChangesAlert");
    QVERIFY(asked);
    QCOMPARE(asked->text(), QString("Save changes to Untitled?"));
    // Cancel keeps the tab and its changes.
    click(*asked, QStringLiteral("Cancel"));
    QTRY_VERIFY(done);
    QVERIFY(!workspace.isManaging());
    QCOMPARE(workspace.current().id, id);
    QVERIFY(workspace.current().session.isModified());
    done = false;
    workspace.close(id, [&done] { done = true; });
    QVERIFY(alert("saveChangesAlert"));
    click(*alert("saveChangesAlert"), QStringLiteral("Don’t Save"));
    QTRY_VERIFY(done);
    QVERIFY(workspace.current().id != id);
    QVERIFY(!workspace.current().session.hasDocument());
}

void ProjectWorkspaceTests::savingWritesTheDocumentAndClearsTheDot()
{
    const QString path = m_dir->filePath("Poster.omai");
    {
        ProjectWorkspace workspace;
        workspace.createDocument(QSizeF(300, 400));
        drawBox(workspace.current().session);
        QVERIFY(workspace.saveTo(workspace.current(), path));
        QVERIFY(!workspace.current().session.isModified());
        QCOMPARE(workspace.current().path.value(), path);
        QCOMPARE(workspace.current().title(), QString("Poster"));
        QVERIFY(QFileInfo(path).size() > 0);
        QCOMPARE(ProjectWorkspace::recentFiles(), QStringList{path});
        // Save with a path writes there without a dialog.
        drawBox(workspace.current().session);
        bool saved = false;
        workspace.save(workspace.current().id, false, [&saved](bool ok) { saved = ok; });
        QTRY_VERIFY(saved);
        QVERIFY(!workspace.current().session.isModified());
    }
    ProjectWorkspace reopened;
    QVERIFY(reopened.openFile(path));
    QCOMPARE(int(reopened.tabs().size()), 1);
    const VectorDocument &document = reopened.current().session.document().value();
    QCOMPARE(document.size, QSizeF(300, 400));
    QCOMPARE(leaves(document, ObjectKind::path), 2);
    QCOMPARE(reopened.current().title(), QString("Poster"));
    QVERIFY(!reopened.current().session.isModified());
}

void ProjectWorkspaceTests::savingDuringAHeldPreviewWritesTheDesignWidth()
{
    const QString path = m_dir->filePath("Site.omai");
    ProjectWorkspace workspace;
    workspace.createDocument(QSizeF(1000, 800));
    EditorSession &session = workspace.current().session;
    VectorDocument document = VectorDocument::blank({1000, 800});
    VectorObject view = VectorObject::frame({0, 0, 400, 300}, QStringLiteral("Site"));
    view.browser = BrowserView{QUrl(QStringLiteral("http://localhost/")), {}, {}};
    const QUuid frame = view.id;
    document.insert(view, document.layers().front());
    VectorObject note = VectorObject::frame({320, 10, 60, 30}, QStringLiteral("Badge"));
    note.layout.horizontal = LayoutConstraint::end;
    note.layout.previewRule = PreviewRule::fixed;
    const QUuid badge = note.id;
    document.insert(note, frame);
    session.loadDocument(document);
    session.beginPreview(QStringLiteral("Preview Width"));
    session.previewFrameBox(frame, QRectF(0, 0, 390, 300));
    QVERIFY(session.isPreviewOnly());
    QVERIFY(workspace.saveTo(workspace.current(), path));
    // The held width stays on screen; the file has the design.
    QVERIFY(session.isPreviewOnly());
    QCOMPARE(session.document()->bounds(frame).width(), 390.0);
    const VectorDocument saved = ProjectStore::read(path);
    QCOMPARE(saved.bounds(frame).width(), 400.0);
    QVERIFY(!saved.find(badge)->layout.absolute);
    QCOMPARE(saved.find(badge)->layout.previewRule, PreviewRule::fixed);
}

void ProjectWorkspaceTests::openingTheSamePathSelectsItsTab()
{
    const QString path = m_dir->filePath("Card.omai");
    ProjectWorkspace workspace;
    workspace.createDocument(QSizeF(100, 100));
    QVERIFY(workspace.saveTo(workspace.current(), path));
    const QUuid saved = workspace.current().id;
    workspace.createDocument(QSizeF(20, 20));
    QCOMPARE(int(workspace.tabs().size()), 2);
    QVERIFY(workspace.openFile(path));
    QCOMPARE(int(workspace.tabs().size()), 2);
    QCOMPARE(workspace.current().id, saved);
}

void ProjectWorkspaceTests::svgsAndPicturesOpenAsDocuments()
{
    const QString drawing = m_dir->filePath("Logo.svg"), picture = m_dir->filePath("Photo.png");
    write(drawing, svg);
    QImage image(40, 30, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(picture));
    ProjectWorkspace workspace;
    workspace.receive({drawing, picture});
    QCOMPARE(int(workspace.tabs().size()), 2);
    const ProjectTab &logo = *workspace.tabs().front();
    QCOMPARE(logo.title(), QString("Logo"));
    // Not native: Save asks for a name.
    QVERIFY(!logo.path);
    QCOMPARE(logo.session.document().value().size, QSizeF(100, 80));
    QCOMPARE(leaves(logo.session.document().value(), ObjectKind::path), 2);
    const ProjectTab &photo = workspace.current();
    QCOMPARE(photo.title(), QString("Photo"));
    const VectorDocument &document = photo.session.document().value();
    QCOMPARE(document.size, QSizeF(40, 30));
    QCOMPARE(leaves(document, ObjectKind::image), 1);
    const auto placed = std::find_if(document.objects.begin(), document.objects.end(), [](const VectorObject &each) { return each.kind == ObjectKind::image; });
    QCOMPARE(placed->image.size(), QSize(40, 30));
    QCOMPARE(document.bounds(placed->id), QRectF(0, 0, 40, 30));
    QVERIFY(!photo.session.isModified());
    QCOMPARE(ProjectWorkspace::recentFiles(), (QStringList{picture, drawing}));
}

void ProjectWorkspaceTests::excalidrawOpensAndPlaces()
{
    const QString drawing = m_dir->filePath("Sketch.excalidraw");
    write(drawing, excalidraw);
    ProjectWorkspace workspace;
    workspace.receive({drawing});
    QCOMPARE(int(workspace.tabs().size()), 1);
    const ProjectTab &opened = workspace.current();
    QCOMPARE(opened.title(), QString("Sketch"));
    QCOMPARE(leaves(opened.session.document().value(), ObjectKind::path), 1);

    workspace.createDocument(QSizeF(400, 300));
    EditorSession &session = workspace.current().session;
    const size_t before = session.document().value().objects.size();
    QVERIFY(workspace.placeFile(drawing));
    const VectorDocument &document = session.document().value();
    QCOMPARE(document.objects.size(), before + 2);
    const VectorObject &group = *document.find(session.selection().front());
    QCOMPARE(group.kind, ObjectKind::group);
    QCOMPARE(group.name, QString("Sketch.excalidraw"));
}

void ProjectWorkspaceTests::aBadFileAlertsAndAddsNoTab()
{
    const QString broken = m_dir->filePath("Broken.omai");
    write(broken, "not json at all");
    ProjectWorkspace workspace;
    workspace.createDocument(QSizeF(10, 10));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Couldn’t open “Broken.omai”.*"));
    QVERIFY(!workspace.openFile(broken));
    QCOMPARE(int(workspace.tabs().size()), 1);
    QMessageBox *shown = alert("fileAlert");
    QVERIFY(shown);
    QCOMPARE(shown->text(), QString("Couldn’t open “Broken.omai”"));
    QVERIFY(!shown->informativeText().isEmpty());
    QVERIFY(ProjectWorkspace::recentFiles().isEmpty());
}

void ProjectWorkspaceTests::placingAddsAGroupOrAnImage()
{
    const QString drawing = m_dir->filePath("Badge.svg"), picture = m_dir->filePath("Stamp.png");
    write(drawing, svg);
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(picture));
    ProjectWorkspace workspace;
    QVERIFY(!workspace.placeFile(drawing));
    workspace.createDocument(QSizeF(400, 300));
    EditorSession &session = workspace.current().session;
    const size_t before = session.document().value().objects.size();
    QVERIFY(workspace.placeFile(drawing));
    const VectorDocument &document = session.document().value();
    QCOMPARE(document.objects.size(), before + 3);
    QCOMPARE(int(session.selection().size()), 1);
    const VectorObject &group = *document.find(session.selection().front());
    QCOMPARE(group.kind, ObjectKind::group);
    QCOMPARE(group.name, QString("Badge.svg"));
    QCOMPARE(int(document.children(group.id).size()), 2);
    // Centred on the artboard, one step to undo.
    QCOMPARE(document.bounds(group.id).center(), QPointF(200, 150));
    QCOMPARE(session.undoName(), QString("Place"));
    session.undo();
    QCOMPARE(session.document().value().objects.size(), before);
    QVERIFY(!session.canUndo());
    QVERIFY(workspace.placeFile(picture));
    QCOMPARE(leaves(session.document().value(), ObjectKind::image), 1);
    QCOMPARE(session.document().value().find(session.selection().front())->name, QString("Stamp.png"));
}

void ProjectWorkspaceTests::exportsWriteEachFormat()
{
    ProjectWorkspace workspace;
    QVERIFY(!workspace.exportTo(m_dir->filePath("none.png"), DocumentExporter::Format::png));
    workspace.createDocument(QSizeF(100, 50));
    drawBox(workspace.current().session);
    const std::vector<std::pair<QString, DocumentExporter::Format>> formats{
        {"art.png", DocumentExporter::Format::png}, {"art.jpg", DocumentExporter::Format::jpeg},
        {"art.svg", DocumentExporter::Format::svg}, {"art.pdf", DocumentExporter::Format::pdf}};
    for (const auto &[name, format] : formats) {
        QVERIFY(workspace.exportTo(m_dir->filePath(name), format, RasterOptions{.scale = 2, .quality = 80, .transparent = false}));
        QVERIFY2(QFileInfo(m_dir->filePath(name)).size() > 0, qPrintable(name));
    }
    // PNG and JPEG take the scale: two pixels a point.
    QCOMPARE(QImage(m_dir->filePath("art.png")).size(), QSize(200, 100));
    QCOMPARE(QImage(m_dir->filePath("art.jpg")).size(), QSize(200, 100));
    QVERIFY(QFile(m_dir->filePath("art.svg")).open(QIODevice::ReadOnly));
}

void ProjectWorkspaceTests::exportRefusesAnArtboardSetNotToExport()
{
    ProjectWorkspace workspace;
    QString title, message;
    workspace.errorHandler = [&](const QString &t, const QString &m) { title = t; message = m; };
    workspace.createDocument(QSizeF(100, 50));
    EditorSession &session = workspace.current().session;
    drawBox(session);
    session.renameArtboard(0, QStringLiteral("Scratch"));
    session.setArtboardExported(0, false);
    const QString path = m_dir->filePath("refused.png");
    QVERIFY(!workspace.exportTo(path, DocumentExporter::Format::png));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY2(message.contains(QStringLiteral("“Scratch” is set not to export")), qPrintable(message));
    QVERIFY(title.contains(QLatin1String("refused.png")));
    // Turned back on, the same call writes the file.
    session.setArtboardExported(0, true);
    QVERIFY(workspace.exportTo(path, DocumentExporter::Format::png));
    QVERIFY(QFileInfo::exists(path));
}

void ProjectWorkspaceTests::recentFilesAreNewestFirstAndLimited()
{
    for (int index = 0; index < 12; ++index)
        ProjectWorkspace::noteRecent(m_dir->filePath(QStringLiteral("file%1.omai").arg(index)));
    QStringList recent = ProjectWorkspace::recentFiles();
    QCOMPARE(recent.size(), 10);
    QCOMPARE(recent.front(), m_dir->filePath("file11.omai"));
    QCOMPARE(recent.back(), m_dir->filePath("file2.omai"));
    // Noting one again moves it to the front, once.
    ProjectWorkspace::noteRecent(m_dir->filePath("file5.omai"));
    recent = ProjectWorkspace::recentFiles();
    QCOMPARE(recent.size(), 10);
    QCOMPARE(recent.front(), m_dir->filePath("file5.omai"));
    QCOMPARE(recent.count(m_dir->filePath("file5.omai")), 1);
    ProjectWorkspace::clearRecent();
    QVERIFY(ProjectWorkspace::recentFiles().isEmpty());
}

QTEST_MAIN(ProjectWorkspaceTests)
#include "ProjectWorkspaceTests.moc"
