#include "Agent/AgentProtocol.h"
#include "Document/PathOperations.h"
#include "Live/Browser.h"
#include "UI/AgentPanels.h"
#include "UI/AgentSheets.h"
#include "UI/ProjectWorkspaceView.h"
#include <QAbstractButton>
#include <QCommandLinkButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalSocket>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// The UI half of the agent bridge: accept bar, paused tools, panels, sheets.
namespace {
const QString square = QStringLiteral(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 50'><rect width='100' height='50' fill='#ff0000'/></svg>");
const QString circle = QStringLiteral(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'><circle cx='20' cy='20' r='20' fill='#0000ff'/></svg>");

// Stands in for `omarchy`: names $FAKE_AGENT as the default and records the prompt.
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\"; exit 0; fi\n"
                                    "exit 2\n";

template<typename T = QWidget>
T *shown(const QString &name)
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            if (T *typed = qobject_cast<T *>(widget))
                return typed;
    }
    return nullptr;
}

QUuid rectangle(EditorSession &session, QRectF rect)
{
    return session.addPath(Shapes::rectangle(rect), QStringLiteral("Rectangle"));
}
}

class AgentUiTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString prompt() const
    {
        QFile file(m_directory.filePath(QStringLiteral("prompt")));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        const QString script = m_directory.filePath(QStringLiteral("omarchy"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fakeOmarchy);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_OMARCHY", script.toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("prompt")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("omastrator.sock")).toUtf8());
    }

    void init()
    {
        qputenv("FAKE_AGENT", "sh");
        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
    }

    void keepingIsOneUndoStepWithTheBarGone()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        const QUuid shape = rectangle(session, {10, 10, 40, 40});
        AgentBridge &bridge = *window.agent();
        bridge.tools().call(QStringLiteral("set_style"), {{"fill", "#00ff00"}});
        bridge.tools().call(QStringLiteral("proposal_finish"), {{"title", "Recolor"}, {"summary", "Made it green."}});
        auto *bar = window.findChild<ProposalBar *>(QStringLiteral("proposalBar"));
        QVERIFY(bar && bar->isVisible());
        QCOMPARE(bar->findChild<QLabel *>(QStringLiteral("proposalText"))->text(),
                 QStringLiteral("<b>AI: Recolor</b>: Enter keeps it, Esc discards it."));
        QCOMPARE(bar->findChild<QLabel *>(QStringLiteral("proposalSummary"))->text(), QStringLiteral("Made it green."));
        QVERIFY(window.content()->canvas().isPaused());

        bar->findChild<QPushButton *>(QStringLiteral("proposalKeep"))->click();
        QVERIFY(!session.isInteracting());
        QCOMPARE(session.undoName(), QStringLiteral("AI: Recolor"));
        QCOMPARE(session.document()->find(shape)->fill, Paint::solid(Qt::green));
        QVERIFY(!bar->isVisible());
        QVERIFY(!window.content()->canvas().isPaused());
        session.undo();
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void enterAndEscapeWorkFromTheCanvas()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        rectangle(session, {10, 10, 40, 40});
        const VectorDocument before = *session.document();
        AgentBridge &bridge = *window.agent();
        EditorCanvas &canvas = window.content()->canvas();

        bridge.tools().call(QStringLiteral("insert_svg"), {{"svg", square}});
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!session.isInteracting());
        QCOMPARE(*session.document(), before);
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));

        bridge.tools().call(QStringLiteral("insert_svg"), {{"svg", square}});
        QTest::keyClick(&canvas, Qt::Key_Return);
        QCOMPARE(session.undoName(), QStringLiteral("AI: Insert SVG"));
        QVERIFY(!window.content()->hasProposal());
    }

    void toolsAndEditsRestDuringAProposal()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        rectangle(session, {10, 10, 40, 40});
        session.selectTool(Tool::rectangle);
        AgentBridge &bridge = *window.agent();
        bridge.tools().call(QStringLiteral("transform"), {{"translate", QJsonArray{5, 0}}});
        const VectorDocument proposed = *session.document();
        EditorCanvas &canvas = window.content()->canvas();

        // A drag with the Rectangle tool draws nothing and keeps the proposal open.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&canvas, QPoint(200, 200));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(200, 200));
        QVERIFY(bridge.hasProposalIn(session));
        QCOMPARE(*session.document(), proposed);
        // Tool keys and rail buttons would commit it.
        QTest::keyClick(&canvas, Qt::Key_V);
        QCOMPARE(session.tool(), Tool::rectangle);
        QVERIFY(!window.findChild<QToolButton *>(QStringLiteral("tool:select"))->isEnabled());

        Menus &menus = *window.menus();
        for (const char *name : {"undo", "save", "group", "bringToFront", "duplicate", "place", "imageTraceMake"})
            QVERIFY2(!menus.action(QString::fromLatin1(name))->isEnabled(), name);
        QVERIFY(menus.action(QStringLiteral("zoomIn"))->isEnabled());
        QVERIFY(!window.content()->acceptsDrop(QMimeData()));
        bridge.discardProposal();
        QVERIFY(menus.action(QStringLiteral("undo"))->isEnabled());
        QVERIFY(window.findChild<QToolButton *>(QStringLiteral("tool:select"))->isEnabled());
    }

    void variationsInsertAsAProposal()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        AgentBridge &bridge = *window.agent();
        bridge.showVariations(QStringLiteral("external"), {{"Square", square, "Blocky"}, {"Circle", circle, QString()}});
        QVERIFY(bridge.variationsPanel().isVisible());
        QToolButton *first = shown<QToolButton>(QStringLiteral("variation:0:0"));
        QVERIFY(first);
        QVERIFY(!first->icon().isNull());
        QCOMPARE(first->toolTip(), QStringLiteral("Square: Blocky"));
        first->click();
        QVERIFY(bridge.hasProposalIn(session));
        QCOMPARE(bridge.proposalTitle(), QStringLiteral("AI: Generate"));
        QCOMPARE(bridge.proposalSummary(), QStringLiteral("Square"));
        const size_t objects = session.document()->objects.size();

        // Another pick swaps the one on show.
        shown<QToolButton>(QStringLiteral("variation:0:1"))->click();
        QCOMPARE(session.document()->objects.size(), objects);
        QCOMPARE(bridge.proposalSummary(), QStringLiteral("Circle"));
        bridge.keepProposal();
        QCOMPARE(session.undoName(), QStringLiteral("AI: Generate"));
        session.undo();
        QVERIFY(!session.canUndo());
        QCOMPARE(bridge.chosen(), std::optional(std::pair(0, 1)));
    }

    void generateAndRefineLaunchTheAgent()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        AgentBridge &bridge = *window.agent();
        QCOMPARE(bridge.generate(QStringLiteral("A fox logo"), 2, false), QString());
        QCOMPARE(bridge.rounds().size(), size_t(1));
        const QString requestId = bridge.rounds().front().requestId;
        QVERIFY(prompt().contains(requestId));
        QVERIFY(prompt().contains(QStringLiteral("Make 2 distinct variations")));
        QCOMPARE(bridge.waitingText(), QStringLiteral("Waiting for sh…"));
        QLabel *status = shown<QLabel>(QStringLiteral("variationsStatus"));
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral("Waiting for sh…"));

        bridge.showVariations(requestId, {{"Fox", square, QString()}, {"Round fox", circle, QString()}});
        QVERIFY(!bridge.waiting());
        QVERIFY(!shown<QLabel>(QStringLiteral("variationsStatus")));
        QCOMPARE(bridge.insertVariation(0, 1), QString());
        bridge.keepProposal();

        auto *field = shown<QLineEdit>(QStringLiteral("refineField"));
        QVERIFY(field);
        field->setText(QStringLiteral("rounder"));
        shown<QPushButton>(QStringLiteral("refineButton"))->click();
        QCOMPARE(bridge.rounds().size(), size_t(2));
        QVERIFY(prompt().contains(QStringLiteral("1. A fox logo")));
        QVERIFY(prompt().contains(circle));
        QVERIFY(prompt().contains(QStringLiteral("rounder")));
        QVERIFY(prompt().contains(bridge.rounds().back().requestId));
        // Cancel stops waiting; the earlier round is still there to go back to.
        shown<QPushButton>(QStringLiteral("variationsCancel"))->click();
        QVERIFY(!bridge.waiting());
        QVERIFY(shown<QToolButton>(QStringLiteral("variation:0:0")));
    }

    void aMissingAgentShowsInTheSheet()
    {
        qputenv("FAKE_AGENT", "");
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        QDialog *sheet = AgentSheets::generate(*window.agent(), &window, QStringLiteral("A fox"));
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QVERIFY(sheet->isVisible());
        QLabel *error = sheet->findChild<QLabel *>(QStringLiteral("launchError"));
        QVERIFY(error->isVisible());
        QCOMPARE(error->text(), QStringLiteral("Choose an agent in Omarchy → Setup → Default → Agent."));
        QVERIFY(window.agent()->rounds().empty());
        QVERIFY(prompt().isEmpty());
        sheet->close();
    }

    void editWithInstructionWaitsInTheBar()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        rectangle(workspace.current().session, {0, 0, 20, 20});
        AgentBridge &bridge = *window.agent();
        QDialog *sheet = AgentSheets::editWithInstruction(bridge, &window);
        QCOMPARE(sheet->findChild<QLabel *>(QStringLiteral("instructionScope"))->text(), QStringLiteral("Applies to the selection."));
        sheet->findChild<QPlainTextEdit *>(QStringLiteral("instructionField"))->setPlainText(QStringLiteral("Make it teal"));
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QVERIFY(prompt().contains(QStringLiteral("Make it teal")));
        QVERIFY(prompt().contains(QStringLiteral("selection_get")));
        auto *bar = window.findChild<ProposalBar *>(QStringLiteral("proposalBar"));
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->findChild<QLabel *>(QStringLiteral("proposalText"))->text(), QStringLiteral("Waiting for sh…"));
        bridge.tools().call(QStringLiteral("set_style"), {{"fill", "#008080"}});
        bridge.tools().call(QStringLiteral("proposal_finish"), {{"title", "Teal"}});
        QVERIFY(!bridge.waiting());
        QVERIFY(bar->findChild<QPushButton *>(QStringLiteral("proposalKeep"))->isVisible());
    }

    void roastShowsTheRoastThenTheFeedbackThenGenerates()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        const QUuid shape = rectangle(session, {10, 10, 40, 40});
        session.deselectAll();
        AgentBridge &bridge = *window.agent();

        auto *button = window.findChild<QToolButton *>(QStringLiteral("roastMyDesign"));
        QVERIFY(button && button->isEnabled());
        QCOMPARE(button->accessibleName(), QStringLiteral("Roast My Design"));
        QCOMPARE(button->toolTip(), QStringLiteral("Roast My Design"));
        button->click();
        QVERIFY(prompt().contains(QStringLiteral("Roast My Design")));
        QVERIFY(prompt().contains(QStringLiteral("whole artboard")));
        QVERIFY(shown<QLabel>(QStringLiteral("roastStatus")));
        const QString requestId = bridge.waiting()->requestId;

        AgentRoast roast{requestId, QStringLiteral("One rectangle. Brave."),
                         {{QStringLiteral("Add hierarchy"), QStringLiteral("Everything is the same size."), {shape}}},
                         QStringLiteral("A layout with a clear focal point")};
        bridge.showRoast(roast);
        QVERIFY(!shown<QLabel>(QStringLiteral("roastStatus")));
        QLabel *text = shown<QLabel>(QStringLiteral("roastText"));
        QVERIFY(text);
        QCOMPARE(text->text(), QStringLiteral("One rectangle. Brave."));
        QVERIFY(shown(QStringLiteral("roastSeparator")));
        // The roast comes first, then the feedback under the separator.
        QVERIFY(text->y() < shown(QStringLiteral("roastSeparator"))->y());
        QVERIFY(shown(QStringLiteral("roastSeparator"))->y() < shown(QStringLiteral("feedback:0"))->y());
        shown<QCommandLinkButton>(QStringLiteral("feedback:0"))->click();
        QCOMPARE(session.selection(), std::vector<QUuid>{shape});

        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        shown<QPushButton>(QStringLiteral("makeVariations"))->click();
        QVERIFY(prompt().contains(QStringLiteral("A layout with a clear focal point")));
        QVERIFY(prompt().contains(QStringLiteral("Make 3 distinct variations")));
        QCOMPARE(bridge.rounds().back().instruction, QStringLiteral("A layout with a clear focal point"));
        QVERIFY(bridge.variationsPanel().isVisible());
    }

    void imageTraceMakeIsAnUndoStep()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        Menus &menus = *window.menus();
        QVERIFY(!menus.action(QStringLiteral("imageTraceMake"))->isEnabled());
        QImage pixels(40, 40, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::white);
        for (int y = 10; y < 30; ++y)
            for (int x = 10; x < 30; ++x)
                pixels.setPixel(x, y, qRgb(0, 0, 0));
        const QUuid image = session.placeImage(pixels, QStringLiteral("Scan"));
        QVERIFY(menus.action(QStringLiteral("imageTraceMake"))->isEnabled());
        QVERIFY(menus.action(QStringLiteral("vectorizeWithAI"))->isEnabled());
        menus.action(QStringLiteral("imageTraceMake"))->trigger();
        QCOMPARE(session.undoName(), QStringLiteral("Image Trace"));
        QVERIFY(!session.document()->find(image));
        const QUuid group = session.selection().front();
        QCOMPARE(session.document()->find(group)->kind, ObjectKind::group);
        QVERIFY(qAbs(session.document()->bounds(group).width() - 20) < 1.5);
        session.undo();
        QVERIFY(session.document()->find(image));
    }

    void vectorizeTracesThenLaunches()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        QImage pixels(40, 40, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::white);
        for (int y = 5; y < 35; ++y)
            for (int x = 5; x < 20; ++x)
                pixels.setPixel(x, y, qRgb(200, 0, 0));
        session.placeImage(pixels, QStringLiteral("Logo"));
        QDialog *sheet = AgentSheets::vectorize(*window.agent(), &window);
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QVERIFY(!sheet->isVisible());
        QVERIFY(window.agent()->hasProposalIn(session));
        QVERIFY(prompt().contains(QStringLiteral("Logo & icon")));
        QVERIFY(prompt().contains(QStringLiteral("replace_objects")));
    }

    void connectAnAgentSaysWhereAndHow()
    {
        qputenv("FAKE_AGENT", "");
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        const QString path = m_directory.filePath(QStringLiteral("connect.sock"));
        QCOMPARE(window.agent()->startServer(path), QString());
        const QString text = AgentSheets::connectText(*window.agent());
        QVERIFY(text.contains(path));
        QVERIFY(text.contains(QStringLiteral("claude mcp add omastrator -- omastrator --mcp")));
        QVERIFY(text.contains(QStringLiteral("insert_svg")));
        QVERIFY(text.contains(QStringLiteral("Default agent: none. Choose an agent in Omarchy → Setup → Default → Agent.")));

        // A second Omastrator says why in the same place, not in an alert at launch.
        ProjectWorkspace other;
        ProjectWorkspaceView second(other);
        QVERIFY(second.agent()->startServer(path).contains(QStringLiteral("already listening")));
        QVERIFY(AgentSheets::connectText(*second.agent()).contains(QStringLiteral("not listening for agents")));

        qputenv("FAKE_AGENT", "sh");
        window.menus()->action(QStringLiteral("connectAgent"))->trigger();
        auto *shownText = shown<QPlainTextEdit>(QStringLiteral("connectText"));
        QVERIFY(shownText);
        QVERIFY(shownText->toPlainText().contains(QStringLiteral("Default agent: sh")));
        shownText->window()->close();
    }

    void statusFollowsTheFrontSessionAndResults()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        const QString path = m_directory.filePath(QStringLiteral("status.sock"));
        QVERIFY(bridge.startServer(path).isEmpty());
        QLocalSocket follower;
        follower.connectToServer(path);
        QVERIFY(follower.waitForConnected(1000));
        follower.write(AgentProtocol::frame(AgentProtocol::request(1, QStringLiteral("status_follow"), {})));
        QByteArray received;
        QJsonObject last;
        auto drain = [&] {
            received += follower.readAll();
            qsizetype end;
            while ((end = received.indexOf('\n')) >= 0) {
                const QJsonObject message = QJsonDocument::fromJson(received.left(end)).object();
                received.remove(0, end + 1);
                last = message.contains("result") ? message["result"].toObject() : message["params"].toObject();
            }
            return last;
        };
        QTRY_VERIFY(drain()["running"].toBool());
        QCOMPARE(last["tool"].toString(), QStringLiteral("select"));

        // The canvas's own tool change reaches the island.
        workspace.current().session.selectTool(Tool::pencil);
        QTRY_COMPARE(drain()["tool"].toString(), QStringLiteral("pencil"));
        // A new front tab is followed too.
        workspace.createDocument(QSizeF(100, 100));
        QTRY_VERIFY(drain()["document"].toBool());
        workspace.current().session.selectTool(Tool::star);
        QTRY_COMPARE(drain()["tool"].toString(), QStringLiteral("star"));

        // Variations arriving are "ready" until the user picks one.
        bridge.tools().call(QStringLiteral("show_variations"),
                            {{"requestId", "r1"}, {"variations", QJsonArray{QJsonObject{{"name", "A"}, {"svg", square}}}}});
        QTRY_VERIFY(drain()["ready"].toBool());
        QCOMPARE(last["variations"].toInt(), 1);
        QCOMPARE(last["variationsId"].toString(), QStringLiteral("r1"));
        QVERIFY(bridge.insertVariation(0, 0).isEmpty());
        // The picked variation is an open proposal: still ready, now for Enter or Esc.
        QTRY_COMPARE(drain()["proposal"].toString(), QStringLiteral("AI: Generate"));
        bridge.keepProposal();
        QTRY_VERIFY(!drain()["ready"].toBool());
    }

    void swatchesPanelAppliesColours()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        EditorSession &session = workspace.current().session;
        const QUuid shape = rectangle(session, {10, 10, 40, 40});
        AgentBridge &bridge = *window.agent();
        bridge.swatches()->removeGroup(QStringLiteral("Test"));
        window.menus()->action(QStringLiteral("showSwatches"))->trigger();
        QVERIFY(bridge.swatchesPanel().isVisible());
        // Adding shows the panel and the new chip.
        bridge.tools().call(QStringLiteral("swatches_add"), {{"group", "Test"}, {"swatches", QJsonArray{QJsonObject{{"color", "#336699"}}}}});
        QAbstractButton *chip = nullptr;
        QTRY_VERIFY((chip = shown<QAbstractButton>(QStringLiteral("swatch"))));
        QVERIFY(chip->toolTip().contains(QLatin1String("#336699")));
        QTest::mouseClick(chip, Qt::LeftButton);
        QCOMPARE(session.document()->find(shape)->fill, Paint::solid(QColor(0x33, 0x66, 0x99)));
        QTRY_VERIFY((chip = shown<QAbstractButton>(QStringLiteral("swatch"))));
        QTest::mouseClick(chip, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(session.document()->find(shape)->stroke.paint, Paint::solid(QColor(0x33, 0x66, 0x99)));
        QCOMPARE(session.undoName(), QStringLiteral("Stroke"));
        bridge.swatches()->removeGroup(QStringLiteral("Test"));
        // A capture with no document opens one of its own.
        const QString png = m_directory.filePath(QStringLiteral("capture.png"));
        QImage image(30, 20, QImage::Format_ARGB32);
        image.fill(Qt::black);
        QVERIFY(image.save(png));
        const qsizetype before = workspace.tabs().size();
        const QJsonObject opened = bridge.tools().call(QStringLiteral("open_capture"), {{"path", png}, {"trace", false}});
        QCOMPARE(workspace.tabs().size(), before + 1);
        QCOMPARE(workspace.current().session.document()->size, QSizeF(30, 20));
        QVERIFY(!opened["imageId"].toString().isEmpty());
    }

    void islandStartsTheFlows()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        workspace.createDocument(QSizeF(200, 200));
        AgentBridge &bridge = *window.agent();
        auto start = [&](const QJsonObject &params) -> QString {
            try {
                bridge.tools().call(QStringLiteral("ai_start"), params);
                return {};
            } catch (const AgentProtocol::Error &failure) {
                return failure.message();
            }
        };

        // With a prompt, Generate launches at once and the status says so.
        QVERIFY(start({{"flow", "generate"}, {"prompt", "a paper plane"}, {"count", 2}}).isEmpty());
        QVERIFY(prompt().contains(QStringLiteral("a paper plane")));
        QCOMPARE(bridge.tools().status()["waiting"].toString(), QStringLiteral("Waiting for sh…"));
        QCOMPARE(bridge.tools().status()["task"].toString(), QStringLiteral("generate"));
        QVERIFY(start({{"flow", "cancel"}}).isEmpty());
        QVERIFY(bridge.tools().status()["waiting"].toString().isEmpty());

        // Without one, the sheet opens to type in.
        QVERIFY(start({{"flow", "generate"}}).isEmpty());
        QDialog *sheet = nullptr;
        QTRY_VERIFY((sheet = shown<QDialog>(QStringLiteral("generateSheet"))));
        sheet->close();
        QVERIFY(start({{"flow", "edit"}}).isEmpty());
        QTRY_VERIFY((sheet = shown<QDialog>(QStringLiteral("editInstructionSheet"))));
        sheet->close();

        // Vectorize needs an image or a traced screenshot.
        QVERIFY(start({{"flow", "vectorize"}}).contains(QLatin1String("Capture mode")));
        const QString png = m_directory.filePath(QStringLiteral("shot.png"));
        QImage image(40, 30, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter(&image).fillRect(QRect(5, 5, 20, 15), Qt::black);
        QVERIFY(image.save(png));
        const QJsonObject traced = bridge.tools().call(QStringLiteral("open_capture"), {{"path", png}});
        QVERIFY(traced["traced"].toBool());
        QCOMPARE(bridge.tools().status()["offer"].toString(), QStringLiteral("vectorize"));
        QVERIFY(start({{"flow", "vectorize"}, {"mode", "sketch"}}).isEmpty());
        QVERIFY(prompt().contains(png));
        QVERIFY(prompt().contains(traced["id"].toString()));
        QCOMPARE(bridge.tools().status()["task"].toString(), QStringLiteral("vectorize"));
        bridge.stopWaiting();

        QVERIFY(start({{"flow", "roast"}}).isEmpty());
        QVERIFY(prompt().contains(QStringLiteral("Roast My Design")));
        QVERIFY(bridge.roastPanel().isVisible());
        QVERIFY(start({{"flow", "fly"}}).contains(QLatin1String("flow")));
    }

    void liveRunsInTheAppAndReportsStatus()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        qputenv("OMASTRATOR_LIVE_HEADLESS", "1");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.show();
        AgentBridge &bridge = *window.agent();
        // With nothing to open, the sheet asks.
        QVERIFY(bridge.tools().call(QStringLiteral("live"), {{"action", "start"}})["sheet"].toBool());
        QDialog *sheet = nullptr;
        QTRY_VERIFY((sheet = shown<QDialog>(QStringLiteral("liveSheet"))));
        sheet->close();

        bridge.tools().call(QStringLiteral("live"), {{"action", "start"}, {"folder", OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/plain"}});
        QTRY_VERIFY_WITH_TIMEOUT(bridge.tools().status()["live"].toObject()["state"].toString() != QLatin1String("starting"), 60'000);
        QCOMPARE(bridge.tools().status()["live"].toObject()["state"].toString(), QStringLiteral("running"));
        bridge.tools().call(QStringLiteral("live"), {{"action", "edit"}, {"selector", "#title"}, {"property", "color"}, {"value", "#e11d48"}});
        const QJsonObject status = bridge.tools().call(QStringLiteral("live"), {{"action", "status"}});
        QCOMPARE(status["edits"].toInt(), 1);
        QCOMPARE(status["editList"].toArray()[0].toObject()["token"].toString(), QStringLiteral("--brand"));
        bridge.tools().call(QStringLiteral("live"), {{"action", "select"}, {"on", false}});
        bridge.tools().call(QStringLiteral("live"), {{"action", "stop"}});
        QCOMPARE(bridge.tools().status()["live"].toObject()["state"].toString(), QStringLiteral("off"));
        bool refused = false;
        try {
            bridge.tools().call(QStringLiteral("live"), {{"action", "edit"}, {"selector", "#title"}, {"property", "color"}, {"value", "red"}});
        } catch (const AgentProtocol::Error &failure) {
            refused = failure.message().contains(QLatin1String("isn't running"));
        }
        QVERIFY(refused);
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CONFIG_HOME");
    }
};

QTEST_MAIN(AgentUiTests)
#include "AgentUiTests.moc"
