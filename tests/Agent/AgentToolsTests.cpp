#include "Agent/AgentProtocol.h"
#include "Agent/AgentTools.h"
#include "FakeAgentHost.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
const QString square = QStringLiteral(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 50'><rect x='0' y='0' width='100' height='50' fill='#ff0000'/></svg>");

QUuid id(const QJsonValue &value)
{
    return QUuid::fromString(value.toString());
}
}

class AgentToolsTests : public QObject {
    Q_OBJECT

private:
    static QUuid rectangle(EditorSession &session, QRectF rect)
    {
        return session.addPath(Shapes::rectangle(rect), QStringLiteral("Rectangle"));
    }

    // Runs a method that should fail, and returns the error's code.
    static int failure(AgentTools &tools, const QString &method, const QJsonObject &params, QString *message = nullptr)
    {
        try {
            tools.call(method, params);
        } catch (const AgentProtocol::Error &error) {
            if (message)
                *message = error.message();
            return error.code;
        }
        return 0;
    }

private slots:
    void insertSvgLandsAsAProposalThatCancelRestores()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const VectorDocument before = *host.editor.document();
        AgentTools tools(host);
        QSignalSpy proposal(&tools, &AgentTools::proposalChanged);
        const QJsonObject result = tools.call(QStringLiteral("insert_svg"), {{"svg", square}, {"name", "Logo"}});
        const QUuid group = id(result["id"]);
        QVERIFY(host.editor.isInteracting());
        QVERIFY(tools.hasProposal());
        QCOMPARE(tools.proposalTitle(), QStringLiteral("AI: Insert SVG"));
        QCOMPARE(proposal.count(), 1);
        const VectorObject *object = host.editor.document()->find(group);
        QVERIFY(object);
        QCOMPARE(object->kind, ObjectKind::group);
        QCOMPARE(object->name, QStringLiteral("Logo"));
        QCOMPARE(object->parentID, host.editor.activeLayer());
        QCOMPARE(host.editor.document()->children(group).size(), size_t(1));
        QCOMPARE(host.editor.selection(), std::vector<QUuid>{group});
        // The SVG's own coordinates, when neither at nor fit is given.
        QCOMPARE(host.editor.document()->bounds(group), QRectF(0, 0, 100, 50));

        host.editor.cancelInteraction();
        QVERIFY(!tools.hasProposal());
        QCOMPARE(*host.editor.document(), before);
        QVERIFY(!host.editor.canUndo());
    }

    void insertSvgPlacesAndFits()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        AgentTools tools(host);
        const QUuid fitted = id(tools.call(QStringLiteral("insert_svg"), {{"svg", square}, {"at", QJsonArray{10, 10}}, {"fit", QJsonArray{50, 50}}})["id"]);
        QCOMPARE(host.editor.document()->bounds(fitted), QRectF(10, 22.5, 50, 25));
        const QUuid moved = id(tools.call(QStringLiteral("insert_svg"), {{"svg", square}, {"at", QJsonArray{30, 40}}})["id"]);
        QCOMPARE(host.editor.document()->bounds(moved), QRectF(30, 40, 100, 50));
        const QUuid centred = id(tools.call(QStringLiteral("insert_svg"), {{"svg", square}, {"center", true}})["id"]);
        QCOMPARE(host.editor.document()->bounds(centred), QRectF(50, 75, 100, 50));
        // Both went into the one proposal.
        QVERIFY(host.editor.document()->find(fitted));
        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("AI: Insert SVG"));
        host.editor.undo();
        QVERIFY(!host.editor.document()->find(fitted));
        QVERIFY(!host.editor.document()->find(moved));
        QVERIFY(!host.editor.canUndo());
    }

    void badSvgAndParamsSayWhy()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        AgentTools tools(host);
        QString message;
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {}, &message), int(AgentProtocol::invalidParams));
        QVERIFY(message.contains(QLatin1String("svg")));
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {{"svg", "hello"}}, &message), int(AgentProtocol::invalidParams));
        QVERIFY(message.contains(QLatin1String("SVG")));
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {{"svg", square}, {"fit", QJsonArray{1}}}), int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("no_such_method"), {}), int(AgentProtocol::methodNotFound));
        QCOMPARE(failure(tools, QStringLiteral("set_style"), {{"fill", "#f00"}}, &message), int(AgentProtocol::invalidParams));
        QVERIFY(message.contains(QLatin1String("Nothing is selected")));
        QCOMPARE(failure(tools, QStringLiteral("set_style"), {{"ids", QJsonArray{QUuid::createUuid().toString()}}, {"fill", "#f00"}}, &message),
                 int(AgentProtocol::invalidParams));
        QVERIFY(message.contains(QLatin1String("No object has the id")));
        QVERIFY(!host.editor.isInteracting());
    }

    void noDocumentIsItsOwnError()
    {
        FakeAgentHost host;
        AgentTools tools(host);
        QCOMPARE(failure(tools, QStringLiteral("document_get"), {}), int(AgentProtocol::noDocument));
        host.hasSession = false;
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {{"svg", square}}), int(AgentProtocol::noDocument));
    }

    void aUsersDragIsNeverWrittenOver()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid shape = rectangle(host.editor, {0, 0, 10, 10});
        AgentTools tools(host);
        host.editor.beginInteraction(QStringLiteral("Move"));
        host.editor.previewTransform(QTransform::fromTranslate(5, 5));
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {{"svg", square}}), int(AgentProtocol::busy));
        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("Move"));
        QCOMPARE(host.editor.document()->bounds(shape), QRectF(5, 5, 10, 10));
    }

    void aDragBegunAfterKeepingIsTheUsers()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        AgentTools tools(host);
        tools.call(QStringLiteral("insert_svg"), {{"svg", square}});
        host.editor.commitInteraction();
        // Pressed but not moved yet: the document still matches the last proposal.
        host.editor.beginInteraction(QStringLiteral("Move"));
        QVERIFY(!tools.hasProposal());
        QCOMPARE(failure(tools, QStringLiteral("insert_svg"), {{"svg", square}}), int(AgentProtocol::busy));
        QCOMPARE(host.editor.interactionName(), QStringLiteral("Move"));
    }

    void proposalFinishRenamesTheUndoStep()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid shape = rectangle(host.editor, {0, 0, 10, 10});
        AgentTools tools(host);
        tools.call(QStringLiteral("set_style"), {{"fill", "#00ff00"}});
        tools.call(QStringLiteral("transform"), {{"translate", QJsonArray{20, 0}}});
        const QJsonObject result = tools.call(QStringLiteral("proposal_finish"), {{"title", "Recolor"}, {"summary", "Made it green."}});
        QVERIFY(result["proposal"].toBool());
        QCOMPARE(host.finishedTitle, QStringLiteral("Recolor"));
        QCOMPARE(host.finishedSummary, QStringLiteral("Made it green."));
        // Still waiting for the user; the preview survived the rename.
        QVERIFY(host.editor.isInteracting());
        QCOMPARE(host.editor.document()->find(shape)->fill, Paint::solid(QColor(0, 255, 0)));
        QCOMPARE(host.editor.document()->bounds(shape), QRectF(20, 0, 10, 10));
        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("AI: Recolor"));
        host.editor.undo();
        QCOMPARE(host.editor.undoName(), QStringLiteral("Draw Rectangle"));
        QCOMPARE(host.editor.document()->bounds(shape), QRectF(0, 0, 10, 10));
        QCOMPARE(host.editor.document()->find(shape)->fill, Paint::solid(Qt::white));

        // After the user answers, the next edit opens a new proposal.
        host.editor.redo();
        tools.call(QStringLiteral("transform"), {{"ids", QJsonArray{shape.toString()}}, {"translate", QJsonArray{0, 5}}});
        QVERIFY(tools.hasProposal());
        QCOMPARE(tools.proposalTitle(), QStringLiteral("AI: Transform"));
    }

    void setStyleReachesPathsInsideGroups()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid a = rectangle(host.editor, {0, 0, 10, 10});
        const QUuid b = rectangle(host.editor, {20, 0, 10, 10});
        host.editor.select({a, b});
        host.editor.groupSelection();
        const QUuid group = host.editor.selection().front();
        AgentTools tools(host);
        tools.call(QStringLiteral("set_style"), {{"ids", QJsonArray{group.toString()}},
                                                  {"fill", QJsonObject{{"kind", "solid"}, {"color", "#ff0000ff"}}},
                                                  {"stroke", QJsonObject{{"color", "#0000ff"}, {"width", 3}, {"cap", "round"}}},
                                                  {"opacity", 0.5}, {"blendMode", "multiply"}});
        const VectorDocument &document = *host.editor.document();
        for (const QUuid &leaf : {a, b}) {
            QCOMPARE(document.find(leaf)->fill.color, QColor(0, 0, 255));
            QCOMPARE(document.find(leaf)->stroke.paint, Paint::solid(QColor(0, 0, 255)));
            QCOMPARE(document.find(leaf)->stroke.width, 3.0);
            QCOMPARE(document.find(leaf)->stroke.cap, Qt::RoundCap);
            // The join was left alone.
            QCOMPARE(document.find(leaf)->stroke.join, Qt::MiterJoin);
        }
        QCOMPARE(document.find(group)->opacity, 0.5);
        QCOMPARE(document.find(group)->blendMode, LayerBlendMode::multiply);
        QCOMPARE(failure(tools, QStringLiteral("set_style"), {{"fill", "not a colour"}}), int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("set_style"), {{"stroke", QJsonObject{{"glow", 1}}}}), int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("set_style"), {{"ids", QJsonArray{group.toString()}}}), int(AgentProtocol::invalidParams));
    }

    void transformTakesAMatrixOrParts()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid shape = rectangle(host.editor, {0, 0, 10, 10});
        AgentTools tools(host);
        tools.call(QStringLiteral("transform"), {{"scale", 2}});
        QCOMPARE(host.editor.document()->bounds(shape), QRectF(-5, -5, 20, 20));
        tools.call(QStringLiteral("transform"), {{"matrix", QJsonArray{1, 0, 0, 1, 5, 5}}});
        QCOMPARE(host.editor.document()->bounds(shape), QRectF(0, 0, 20, 20));
        tools.call(QStringLiteral("transform"), {{"rotate", 90}, {"origin", QJsonArray{0, 0}}, {"translate", QJsonArray{0, 1}}});
        const QRectF rotated = host.editor.document()->bounds(shape);
        QVERIFY(qAbs(rotated.left() + 20) < 1e-6 && qAbs(rotated.top() - 1) < 1e-6);
        QCOMPARE(failure(tools, QStringLiteral("transform"), {}), int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("transform"), {{"matrix", QJsonArray{0, 0, 0, 0, 0, 0}}}), int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("transform"), {{"matrix", QJsonArray{1, 0, 0, 1, 0, 0}}, {"rotate", 5}}), int(AgentProtocol::invalidParams));
        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("AI: Transform"));
    }

    void replaceObjectsKeepsThePlaceInTheStack()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid below = rectangle(host.editor, {0, 0, 5, 5});
        const QUuid a = rectangle(host.editor, {10, 10, 20, 20});
        const QUuid b = rectangle(host.editor, {40, 10, 20, 20});
        const QUuid above = rectangle(host.editor, {0, 0, 5, 5});
        AgentTools tools(host);
        const QJsonObject result = tools.call(QStringLiteral("replace_objects"), {{"ids", QJsonArray{a.toString(), b.toString()}}, {"svg", square}});
        const QUuid art = id(result["id"]);
        const VectorDocument &document = *host.editor.document();
        QVERIFY(!document.find(a) && !document.find(b));
        const auto children = document.children(*host.editor.activeLayer());
        QCOMPARE(children, (std::vector<QUuid>{below, art, above}));
        // 100 × 50 art fitted into the 50 × 20 box they covered, centred.
        QCOMPARE(document.bounds(art), QRectF(15, 10, 40, 20));
        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("AI: Replace"));
    }

    void pathfinderGroupArrangeAlignAndDelete()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid a = rectangle(host.editor, {0, 0, 20, 20});
        const QUuid b = rectangle(host.editor, {10, 0, 20, 20});
        const QUuid c = rectangle(host.editor, {100, 100, 10, 10});
        AgentTools tools(host);

        const QUuid united = id(tools.call(QStringLiteral("pathfinder"), {{"ids", QJsonArray{a.toString(), b.toString()}}, {"operation", "unite"}})["id"]);
        QVERIFY(!host.editor.document()->find(a));
        QCOMPARE(host.editor.document()->bounds(united), QRectF(0, 0, 30, 20));

        tools.call(QStringLiteral("arrange"), {{"ids", QJsonArray{c.toString()}}, {"order", "sendToBack"}});
        QCOMPARE(host.editor.document()->children(*host.editor.activeLayer()).front(), c);

        tools.call(QStringLiteral("align"), {{"ids", QJsonArray{united.toString(), c.toString()}}, {"edge", "left"}});
        QCOMPARE(host.editor.document()->bounds(c).left(), 0.0);

        const QUuid group = id(tools.call(QStringLiteral("group"), {{"ids", QJsonArray{united.toString(), c.toString()}}, {"name", "Pair"}})["id"]);
        QCOMPARE(host.editor.document()->children(group), (std::vector<QUuid>{c, united}));
        QCOMPARE(host.editor.document()->find(group)->name, QStringLiteral("Pair"));
        const QJsonArray released = tools.call(QStringLiteral("ungroup"), {{"ids", QJsonArray{group.toString()}}})["ids"].toArray();
        QCOMPARE(released.size(), 2);
        QVERIFY(!host.editor.document()->find(group));

        tools.call(QStringLiteral("delete"), {{"ids", QJsonArray{c.toString()}}});
        QVERIFY(!host.editor.document()->find(c));
        QCOMPARE(failure(tools, QStringLiteral("distribute"), {{"ids", QJsonArray{united.toString()}}, {"axis", "horizontal"}}),
                 int(AgentProtocol::invalidParams));
        QCOMPARE(failure(tools, QStringLiteral("arrange"), {{"order", "sideways"}}), int(AgentProtocol::invalidParams));

        host.editor.commitInteraction();
        QCOMPARE(host.editor.undoName(), QStringLiteral("AI: Pathfinder"));
        host.editor.undo();
        QVERIFY(host.editor.document()->find(a) && host.editor.document()->find(b) && host.editor.document()->find(c));
    }

    void selectAndUpdateObject()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        const QUuid shape = rectangle(host.editor, {0, 0, 10, 10});
        host.editor.deselectAll();
        AgentTools tools(host);
        tools.call(QStringLiteral("select"), {{"ids", QJsonArray{shape.toString()}}});
        QCOMPARE(host.editor.selection(), std::vector<QUuid>{shape});
        QVERIFY(!host.editor.isInteracting());
        const QJsonObject selected = tools.call(QStringLiteral("selection_get"), {});
        QCOMPARE(selected["objects"].toArray().size(), 1);
        QJsonObject json = selected["objects"].toArray().first().toObject();
        json["name"] = "Renamed";
        json.remove("parent");
        tools.call(QStringLiteral("update_object"), {{"object", json}});
        QCOMPARE(host.editor.document()->find(shape)->name, QStringLiteral("Renamed"));
        QCOMPARE(host.editor.document()->find(shape)->parentID, host.editor.activeLayer());
        json["kind"] = "group";
        QCOMPARE(failure(tools, QStringLiteral("update_object"), {{"object", json}}), int(AgentProtocol::invalidParams));
        tools.call(QStringLiteral("select"), {{"ids", QJsonArray{}}});
        QVERIFY(host.editor.selection().empty());
    }

    void renderWritesAPng()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 100});
        rectangle(host.editor, {10, 10, 30, 20});
        AgentTools tools(host);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("board.png"));
        const QJsonObject board = tools.call(QStringLiteral("render"), {{"path", path}, {"scale", 2}});
        QCOMPARE(board["path"].toString(), path);
        QImage image(path);
        QCOMPARE(image.size(), QSize(400, 200));
        const QJsonObject crop = tools.call(QStringLiteral("render"), {{"selectionOnly", true}});
        const QImage cropped(crop["path"].toString());
        QVERIFY(!cropped.isNull());
        // The 30 × 20 rectangle, its 1 pt stroke and a 2 pt margin.
        QVERIFY(qAbs(cropped.width() - 35) <= 1 && qAbs(cropped.height() - 25) <= 1);
        QFile::remove(crop["path"].toString());
        QCOMPARE(failure(tools, QStringLiteral("render"), {{"scale", 1000}}), int(AgentProtocol::invalidParams));
    }

    void documentGetSummarisesImages()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        QImage pixels(8, 4, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::black);
        host.editor.placeImage(pixels, QStringLiteral("Photo"));
        AgentTools tools(host);
        const QJsonObject document = tools.call(QStringLiteral("document_get"), {});
        QCOMPARE(document["width"].toDouble(), 200.0);
        QVERIFY(document.contains("selection") && document.contains("activeLayer"));
        QVERIFY(!document["proposal"].toObject()["open"].toBool());
        bool found = false;
        for (const QJsonValue &each : document["objects"].toArray()) {
            if (each["kind"].toString() == QLatin1String("image")) {
                found = true;
                QVERIFY(each["image"].toString().startsWith(QLatin1String("(omitted")));
                QCOMPARE(each["imageSize"].toArray(), (QJsonArray{8, 4}));
            }
        }
        QVERIFY(found);
    }

    void traceImageBecomesAProposal()
    {
        FakeAgentHost host;
        host.editor.createDocument({200, 200});
        QImage pixels(40, 40, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::white);
        for (int y = 10; y < 30; ++y) {
            for (int x = 10; x < 30; ++x)
                pixels.setPixel(x, y, qRgb(0, 0, 0));
        }
        const QUuid image = host.editor.placeImage(pixels, QStringLiteral("Scan"));
        AgentTools tools(host);
        const QJsonObject result = tools.call(QStringLiteral("trace_image"), {{"mode", "blackAndWhite"}});
        const QUuid group = id(result["id"]);
        QVERIFY(!host.editor.document()->find(image));
        QVERIFY(host.editor.document()->find(group));
        QVERIFY(!result["ids"].toArray().isEmpty());
        // Placed centred on the artboard: the square sits at 90..110.
        const QRectF bounds = host.editor.document()->bounds(group);
        QVERIFY2(qAbs(bounds.left() - 90) < 1.5 && qAbs(bounds.width() - 20) < 1.5, qPrintable(QStringLiteral("%1 %2").arg(bounds.left()).arg(bounds.width())));
        QCOMPARE(QImage(result["imagePath"].toString()).size(), QSize(40, 40));
        QFile::remove(result["imagePath"].toString());
        host.editor.cancelInteraction();
        QVERIFY(host.editor.document()->find(image));
        QCOMPARE(failure(tools, QStringLiteral("trace_image"), {{"mode", "blackAndWhite"}, {"id", group.toString()}}), int(AgentProtocol::invalidParams));
    }

    void panelsReachTheHost()
    {
        FakeAgentHost host;
        AgentTools tools(host);
        // Panels work with no document open.
        tools.call(QStringLiteral("show_roast"), {{"requestId", "r1"}, {"roast", "Four drop shadows. Bold."},
                                                  {"feedback", QJsonArray{QJsonObject{{"title", "Shadows"}, {"detail", "Keep one."},
                                                                                     {"objectIds", QJsonArray{QUuid::createUuid().toString()}}}}},
                                                  {"suggestedPrompt", "Flatter, one shadow"}});
        QVERIFY(host.roast);
        QCOMPARE(host.roast->requestId, QStringLiteral("r1"));
        QCOMPARE(host.roast->feedback.size(), size_t(1));
        QCOMPARE(host.roast->feedback.front().objectIds.size(), size_t(1));
        QCOMPARE(host.roast->suggestedPrompt, QStringLiteral("Flatter, one shadow"));
        QCOMPARE(failure(tools, QStringLiteral("show_roast"), {{"requestId", "r1"}, {"roast", "x"}, {"feedback", QJsonArray{}}, {"suggestedPrompt", "y"}}),
                 int(AgentProtocol::invalidParams));
        // Short parts only: a long speech or a list of fixes is sent back.
        const QJsonArray fix{QJsonObject{{"title", "Fix"}, {"detail", "Do it."}}};
        QString tooLong;
        for (int word = 0; word < 80; ++word)
            tooLong += QStringLiteral("word ");
        QString why;
        QCOMPARE(failure(tools, QStringLiteral("show_roast"), {{"requestId", "r1"}, {"roast", tooLong}, {"feedback", fix}, {"suggestedPrompt", "y"}}, &why),
                 int(AgentProtocol::invalidParams));
        QVERIFY(why.contains(QStringLiteral("60 words")));
        QCOMPARE(failure(tools, QStringLiteral("show_roast"), {{"requestId", "r1"}, {"roast", "Short."}, {"feedback", QJsonArray{fix[0], fix[0], fix[0], fix[0], fix[0]}}, {"suggestedPrompt", "y"}}, &why),
                 int(AgentProtocol::invalidParams));
        QVERIFY(why.contains(QStringLiteral("the 3 with the most impact")));

        tools.call(QStringLiteral("show_variations"), {{"requestId", "g1"}, {"variations", QJsonArray{QJsonObject{{"name", "Bold"}, {"svg", square}}}}});
        QCOMPARE(host.variationsRequest, QStringLiteral("g1"));
        QCOMPARE(host.variations.size(), size_t(1));
        QString message;
        QCOMPARE(failure(tools, QStringLiteral("show_variations"),
                         {{"requestId", "g1"}, {"variations", QJsonArray{QJsonObject{{"name", "Broken"}, {"svg", "<svg"}}}}}, &message),
                 int(AgentProtocol::invalidParams));
        QVERIFY(message.contains(QLatin1String("Broken")));

        tools.call(QStringLiteral("proposal_finish"), {{"summary", "Nothing changed."}});
        QCOMPARE(host.finishedCount, 1);
    }

    void filesGoThroughTheHostOrTheExporters()
    {
        FakeAgentHost host;
        host.editor.createDocument({100, 100});
        rectangle(host.editor, {10, 10, 30, 20});
        AgentTools tools(host);
        QTemporaryDir directory;
        tools.call(QStringLiteral("open"), {{"path", "/tmp/a.omai"}});
        QCOMPARE(host.opened, QStringList{"/tmp/a.omai"});
        host.failure = QStringLiteral("“a.omai” could not be opened.");
        QCOMPARE(failure(tools, QStringLiteral("open"), {{"path", "/tmp/a.omai"}}), int(AgentProtocol::fileError));
        host.failure.clear();

        for (const char *name : {"out.png", "out.svg", "out.pdf", "out.jpg"}) {
            const QString path = directory.filePath(QString::fromLatin1(name));
            tools.call(QStringLiteral("export"), {{"path", path}});
            QVERIFY2(QFileInfo(path).size() > 0, name);
        }
        QCOMPARE(failure(tools, QStringLiteral("export"), {{"path", directory.filePath("out.doc")}}), int(AgentProtocol::fileError));

        const QString svg = directory.filePath(QStringLiteral("art.svg"));
        QFile file(svg);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(square.toUtf8());
        file.close();
        const QUuid placed = id(tools.call(QStringLiteral("place"), {{"path", svg}})["id"]);
        QCOMPARE(host.editor.document()->bounds(placed).center(), QPointF(50, 50));
        QVERIFY(tools.hasProposal());
        // Saving waits for the user's answer.
        QCOMPARE(failure(tools, QStringLiteral("save"), {}), int(AgentProtocol::busy));
        host.editor.commitInteraction();
        tools.call(QStringLiteral("save"), {});
        QCOMPARE(host.saved, QStringList{QString()});
    }
};

QTEST_MAIN(AgentToolsTests)
#include "AgentToolsTests.moc"
