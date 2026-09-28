#include "Document/Components.h"
#include "IO/PenpotImporter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {

void appendLE16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void appendLE32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
}

QByteArray buildZip(const QList<std::pair<QString, QByteArray>> &entries)
{
    QByteArray out;
    struct Written {
        QString name;
        quint32 offset, size;
    };
    QList<Written> written;
    for (const auto &[name, data] : entries) {
        const quint32 offset = quint32(out.size());
        const QByteArray nameUtf8 = name.toUtf8();
        appendLE32(out, 0x04034b50);
        appendLE16(out, 20);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, 0);
        appendLE32(out, quint32(data.size()));
        appendLE32(out, quint32(data.size()));
        appendLE16(out, quint16(nameUtf8.size()));
        appendLE16(out, 0);
        out.append(nameUtf8);
        out.append(data);
        written.append({name, offset, quint32(data.size())});
    }
    const quint32 cdStart = quint32(out.size());
    for (const Written &entry : written) {
        const QByteArray nameUtf8 = entry.name.toUtf8();
        appendLE32(out, 0x02014b50);
        appendLE16(out, 20);
        appendLE16(out, 20);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, 0);
        appendLE32(out, entry.size);
        appendLE32(out, entry.size);
        appendLE16(out, quint16(nameUtf8.size()));
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, 0);
        appendLE32(out, entry.offset);
        out.append(nameUtf8);
    }
    const quint32 cdSize = quint32(out.size()) - cdStart;
    appendLE32(out, 0x06054b50);
    appendLE16(out, 0);
    appendLE16(out, 0);
    appendLE16(out, quint16(written.size()));
    appendLE16(out, quint16(written.size()));
    appendLE32(out, cdSize);
    appendLE32(out, cdStart);
    appendLE16(out, 0);
    return out;
}

QString newID()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject baseShape(const QString &id, const QString &type, double x, double y, double w, double h)
{
    return {{"id", id},  {"type", type}, {"name", type}, {"x", x},       {"y", y},
            {"width", w}, {"height", h},  {"rotation", 0}, {"hidden", false}, {"locked", false},
            {"opacity", 1}};
}

QJsonObject solidFill(const QString &hex)
{
    return QJsonObject{{"fillColor", hex}, {"fillOpacity", 1}};
}

// A whole .penpot file: manifest + one file record + one page (with an
// implicit zero-uuid root listing `topLevelIDs`) + each shape's own entry.
QByteArray penpotFile(const QList<QJsonObject> &shapes, const QStringList &topLevelIDs)
{
    const QString fileID = newID();
    const QString pageID = newID();
    const QString zeroID = QStringLiteral("00000000-0000-0000-0000-000000000000");

    QList<std::pair<QString, QByteArray>> entries;
    const QJsonObject manifest{{"type", "penpot/export-files"},
                                {"files", QJsonArray{QJsonObject{{"id", fileID}, {"name", "Test"}}}}};
    entries.append({QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson(QJsonDocument::Compact)});
    entries.append({QStringLiteral("files/%1.json").arg(fileID), QJsonDocument(QJsonObject{{"id", fileID}, {"name", "Test"}}).toJson()});

    const QJsonObject page{{"id", pageID}, {"name", "Page 1"}, {"index", 0}};
    entries.append({QStringLiteral("files/%1/pages/%2.json").arg(fileID, pageID), QJsonDocument(page).toJson(QJsonDocument::Compact)});

    QJsonArray topLevel;
    for (const QString &id : topLevelIDs)
        topLevel.append(id);
    const QJsonObject root{{"id", zeroID}, {"type", "frame"}, {"shapes", topLevel}};
    entries.append({QStringLiteral("files/%1/pages/%2/%3.json").arg(fileID, pageID, zeroID), QJsonDocument(root).toJson(QJsonDocument::Compact)});

    for (const QJsonObject &shape : shapes) {
        const QString id = shape.value(QStringLiteral("id")).toString();
        entries.append({QStringLiteral("files/%1/pages/%2/%3.json").arg(fileID, pageID, id), QJsonDocument(shape).toJson(QJsonDocument::Compact)});
    }
    return buildZip(entries);
}

const VectorObject *named(const VectorDocument &document, const QString &name)
{
    for (const VectorObject &object : document.objects) {
        if (object.name == name)
            return &object;
    }
    return nullptr;
}

}

class PenpotImporterTests : public QObject {
    Q_OBJECT

private:
    static std::vector<const VectorObject *> children(const VectorDocument &document, const QUuid &parent)
    {
        std::vector<const VectorObject *> result;
        for (const QUuid &id : document.children(parent))
            result.push_back(document.find(id));
        return result;
    }

private slots:
    void canReadDetectsAZipWithManifestJson()
    {
        QVERIFY(PenpotImporter::canRead(penpotFile({}, {})));
        QVERIFY(!PenpotImporter::canRead(QByteArrayLiteral("not a zip")));
        QVERIFY(!PenpotImporter::canRead(buildZip({{QStringLiteral("hello.txt"), QByteArrayLiteral("hi")}})));
    }

    void boardBecomesAFrameAndAnArtboard()
    {
        const QString id = newID();
        QJsonObject board = baseShape(id, "frame", 0, 0, 400, 300);
        board["name"] = "Board 1";
        board["fills"] = QJsonArray{solidFill("#1a2b3c")};
        board["shapes"] = QJsonArray();
        const VectorDocument document = PenpotImporter::parse(penpotFile({board}, {id}));

        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().name, QString("Board 1"));
        QCOMPARE(document.artboards.front().rect, QRectF(0, 0, 400, 300));

        const VectorObject *frame = named(document, QStringLiteral("Board 1"));
        QVERIFY(frame);
        QCOMPARE(frame->kind, ObjectKind::frame);
        QVERIFY(frame->shape.has_value());
        QCOMPARE(frame->fill.kind, PaintKind::solid);
        QCOMPARE(frame->fill.color, QColor(QStringLiteral("#1a2b3c")));
    }

    void rectHasCornerRadiiAndAGradientFill()
    {
        const QString id = newID();
        QJsonObject rect = baseShape(id, "rect", 20, 20, 100, 60);
        rect["name"] = "Box";
        rect["r1"] = 4;
        rect["r2"] = 8;
        rect["r3"] = 12;
        rect["r4"] = 16;
        const QJsonObject gradient{{"type", "linear"},
                                    {"startX", 20},
                                    {"startY", 50},
                                    {"endX", 120},
                                    {"endY", 50},
                                    {"width", 1},
                                    {"stops", QJsonArray{QJsonObject{{"color", "#ff0000"}, {"offset", 0}},
                                                          QJsonObject{{"color", "#0000ff"}, {"offset", 1}}}}};
        rect["fills"] = QJsonArray{QJsonObject{{"fillColorGradient", gradient}}};
        const VectorDocument document = PenpotImporter::parse(penpotFile({rect}, {id}));

        const VectorObject *box = named(document, QStringLiteral("Box"));
        QVERIFY(box);
        QVERIFY(box->shape.has_value());
        QCOMPARE(box->shape->radii[0], 4.0);
        QCOMPARE(box->shape->radii[3], 16.0);
        QCOMPARE(box->fill.kind, PaintKind::linearGradient);
        QCOMPARE(box->fill.stops.size(), size_t(2));
        // Gradient runs across the whole shape width: fractional 0 -> 1.
        QVERIFY(qFuzzyCompare(box->fill.start.x(), 0.0));
        QVERIFY(qFuzzyCompare(box->fill.end.x(), 1.0));
    }

    void pathContentBecomesBezierNodes()
    {
        const QString id = newID();
        QJsonObject path = baseShape(id, "path", 0, 0, 100, 100);
        path["name"] = "Blob";
        const QJsonArray content{
            QJsonObject{{"command", "move-to"}, {"params", QJsonObject{{"x", 0}, {"y", 50}}}},
            QJsonObject{{"command", "curve-to"},
                        {"params", QJsonObject{{"x", 100}, {"y", 50}, {"c1x", 0}, {"c1y", 0}, {"c2x", 100}, {"c2y", 0}}}},
            QJsonObject{{"command", "line-to"}, {"params", QJsonObject{{"x", 0}, {"y", 50}}}},
            QJsonObject{{"command", "close-path"}, {"params", QJsonObject()}}};
        path["content"] = content;
        path["fills"] = QJsonArray{solidFill("#00ff00")};
        const VectorDocument document = PenpotImporter::parse(penpotFile({path}, {id}));

        const VectorObject *blob = named(document, QStringLiteral("Blob"));
        QVERIFY(blob);
        QCOMPARE(blob->kind, ObjectKind::path);
        QCOMPARE(blob->path.contours.size(), size_t(1));
        QVERIFY(blob->path.contours.front().closed);
        QCOMPARE(blob->path.nodeCount(), 3);
    }

    void pathContentIsAlreadyAbsoluteNotRelativeToTheShapesOwnXY()
    {
        // The shape's own x/y (340,30) must NOT be added again on top of the
        // content's own absolute points, which already include that offset.
        const QString id = newID();
        QJsonObject path = baseShape(id, "path", 340, 30, 90, 90);
        path["name"] = "Triangle";
        path["content"] = QJsonArray{QJsonObject{{"command", "move-to"}, {"params", QJsonObject{{"x", 385}, {"y", 30}}}},
                                      QJsonObject{{"command", "line-to"}, {"params", QJsonObject{{"x", 430}, {"y", 120}}}},
                                      QJsonObject{{"command", "line-to"}, {"params", QJsonObject{{"x", 340}, {"y", 120}}}},
                                      QJsonObject{{"command", "close-path"}, {"params", QJsonObject()}}};
        path["fills"] = QJsonArray{solidFill("#f59f00")};
        const VectorDocument document = PenpotImporter::parse(penpotFile({path}, {id}));
        const VectorObject *triangle = named(document, QStringLiteral("Triangle"));
        QVERIFY(triangle);
        QCOMPARE(triangle->path.bounds(), QRectF(340, 30, 90, 90));
    }

    void nestedShapeKeepsItsOwnAbsolutePosition()
    {
        // A board's child carries its own absolute page position too, not one
        // relative to the board -- the same "no parent composition" rule.
        const QString boardID = newID(), childID = newID();
        QJsonObject board = baseShape(boardID, "frame", 100, 100, 200, 200);
        board["name"] = "Board";
        board["shapes"] = QJsonArray{childID};
        QJsonObject child = baseShape(childID, "rect", 150, 160, 40, 40);
        child["name"] = "Child";
        child["parentId"] = boardID;
        child["frameId"] = boardID;

        const VectorDocument document = PenpotImporter::parse(penpotFile({board, child}, {boardID}));
        const VectorObject *node = named(document, QStringLiteral("Child"));
        QVERIFY(node);
        QCOMPARE(node->path.bounds(), QRectF(150, 160, 40, 40));
    }

    void boolShapeUsesItsOwnContentNotItsChildren()
    {
        const QString boolID = newID(), childID = newID();
        QJsonObject boolShape = baseShape(boolID, "bool", 0, 0, 50, 50);
        boolShape["name"] = "Combined";
        boolShape["boolType"] = "union";
        boolShape["shapes"] = QJsonArray{childID};
        boolShape["content"] = QJsonArray{QJsonObject{{"command", "move-to"}, {"params", QJsonObject{{"x", 0}, {"y", 0}}}},
                                           QJsonObject{{"command", "line-to"}, {"params", QJsonObject{{"x", 50}, {"y", 0}}}},
                                           QJsonObject{{"command", "line-to"}, {"params", QJsonObject{{"x", 0}, {"y", 50}}}},
                                           QJsonObject{{"command", "close-path"}, {"params", QJsonObject()}}};
        QJsonObject child = baseShape(childID, "rect", 0, 0, 50, 50);
        child["name"] = "ShouldNotAppear";

        const VectorDocument document = PenpotImporter::parse(penpotFile({boolShape, child}, {boolID}));
        const VectorObject *combined = named(document, QStringLiteral("Combined"));
        QVERIFY(combined);
        QCOMPARE(combined->kind, ObjectKind::path);
        QCOMPARE(combined->path.nodeCount(), 3);
        QVERIFY(!named(document, QStringLiteral("ShouldNotAppear")));
    }

    void frameWithFlexLayoutBecomesAutoLayout()
    {
        const QString id = newID();
        QJsonObject frame = baseShape(id, "frame", 0, 0, 300, 100);
        frame["name"] = "Row";
        frame["layout"] = "flex";
        frame["layoutFlexDir"] = "row";
        frame["layoutGap"] = QJsonObject{{"rowGap", 12}, {"columnGap", 12}};
        frame["layoutPadding"] = QJsonObject{{"p1", 1}, {"p2", 2}, {"p3", 3}, {"p4", 4}};
        frame["layoutJustifyContent"] = "center";
        frame["shapes"] = QJsonArray();
        const VectorDocument document = PenpotImporter::parse(penpotFile({frame}, {id}));
        const VectorObject *row = named(document, QStringLiteral("Row"));
        QVERIFY(row);
        QVERIFY(row->autoLayout.has_value());
        QCOMPARE(row->autoLayout->direction, LayoutDirection::horizontal);
        QCOMPARE(row->autoLayout->gap, 12.0);
        QCOMPARE(row->autoLayout->primary, LayoutAlign::center);
    }

    void gridLayoutWarnsAndKeepsAbsolutePosition()
    {
        const QString id = newID();
        QJsonObject frame = baseShape(id, "frame", 0, 0, 300, 100);
        frame["name"] = "Grid";
        frame["layout"] = "grid";
        frame["shapes"] = QJsonArray();
        QStringList warnings;
        const VectorDocument document = PenpotImporter::parse(penpotFile({frame}, {id}), &warnings);
        const VectorObject *grid = named(document, QStringLiteral("Grid"));
        QVERIFY(grid);
        QVERIFY(!grid->autoLayout.has_value());
        QVERIFY(warnings.join(' ').contains(QStringLiteral("Grid layout")));
    }

    void textCarriesFontAndAlignment()
    {
        const QString id = newID();
        QJsonObject text = baseShape(id, "text", 10, 10, 200, 30);
        text["name"] = "Label";
        const QJsonObject span{{"text", "Hello Penpot"},
                                {"fontFamily", "Work Sans"},
                                {"fontSize", "22"},
                                {"fontWeight", "700"},
                                {"fills", QJsonArray{solidFill("#101010")}}};
        const QJsonObject paragraph{{"type", "paragraph"}, {"textAlign", "center"}, {"children", QJsonArray{span}}};
        const QJsonObject paragraphSet{{"type", "paragraph-set"}, {"children", QJsonArray{paragraph}}};
        text["content"] = QJsonObject{{"type", "root"}, {"verticalAlign", "top"}, {"children", QJsonArray{paragraphSet}}};
        const VectorDocument document = PenpotImporter::parse(penpotFile({text}, {id}));

        const VectorObject *label = named(document, QStringLiteral("Label"));
        QVERIFY(label);
        QCOMPARE(label->kind, ObjectKind::text);
        QCOMPARE(label->text.text, QString("Hello Penpot"));
        QCOMPARE(label->text.family, QString("Work Sans"));
        QCOMPARE(label->text.size, 22.0);
        QCOMPARE(label->text.style, QString("Bold"));
        QCOMPARE(label->text.alignment, TextAlignment::center);
    }

    void componentRootAndInstanceKeepTheirOwnGeometry()
    {
        const QString mainID = newID(), instanceID = newID();
        QJsonObject main = baseShape(mainID, "frame", 0, 0, 80, 40);
        main["name"] = "Card";
        main["componentRoot"] = true;
        main["mainInstance"] = true;
        main["componentId"] = mainID;
        main["shapes"] = QJsonArray();

        QJsonObject instance = baseShape(instanceID, "frame", 200, 0, 80, 40);
        instance["name"] = "Card Copy";
        instance["componentId"] = mainID;
        instance["shapes"] = QJsonArray();

        const VectorDocument document = PenpotImporter::parse(penpotFile({main, instance}, {mainID, instanceID}));
        const VectorObject *card = named(document, QStringLiteral("Card"));
        const VectorObject *copy = named(document, QStringLiteral("Card Copy"));
        QVERIFY(card && copy);
        QVERIFY(card->component.has_value());
        QVERIFY(copy->instance.has_value());
        QCOMPARE(copy->instance->master, card->id);
        // Each keeps the geometry the file gave it (no Components::sync() rebuild).
        QCOMPARE(card->path.bounds(), QRectF(0, 0, 80, 40));
        QCOMPARE(copy->path.bounds(), QRectF(200, 0, 80, 40));
    }

    void instanceBeforeItsComponentStillResolves()
    {
        // The copy is listed, and so built, before the main component that
        // defines it -- plausible in a real file (z-order, or a component on
        // a later page), unlike componentRootAndInstanceKeepTheirOwnGeometry
        // above where the master happens to build first.
        const QString mainID = newID(), instanceID = newID();
        QJsonObject main = baseShape(mainID, "frame", 0, 0, 80, 40);
        main["name"] = "Card";
        main["componentRoot"] = true;
        main["mainInstance"] = true;
        main["componentId"] = mainID;
        main["shapes"] = QJsonArray();

        QJsonObject instance = baseShape(instanceID, "frame", 200, 0, 80, 40);
        instance["name"] = "Card Copy";
        instance["componentId"] = mainID;
        instance["shapes"] = QJsonArray();

        const VectorDocument document = PenpotImporter::parse(penpotFile({main, instance}, {instanceID, mainID}));
        const VectorObject *card = named(document, QStringLiteral("Card"));
        const VectorObject *copy = named(document, QStringLiteral("Card Copy"));
        QVERIFY(card && copy);
        QVERIFY(copy->instance.has_value());
        QCOMPARE(copy->instance->master, card->id);
    }

    // A real export's copy carries the component's id (not the main shape's) and a
    // shapeRef; syncing on open must keep the copy where it is and its own text.
    void copyOfARealComponentKeepsItsPositionAndTextAfterSync()
    {
        const QString componentID = newID(), mainID = newID(), mainLabelID = newID();
        const QString copyID = newID(), copyLabelID = newID();
        auto label = [&](const QString &id, const QString &parent, double x, const QString &text) {
            QJsonObject shape = baseShape(id, "text", x, 5, 60, 20);
            shape["name"] = "Label";
            shape["parentId"] = parent;
            shape["content"] = QJsonObject{{"type", "root"}, {"children", QJsonArray{QJsonObject{{"type", "paragraph-set"},
                {"children", QJsonArray{QJsonObject{{"type", "paragraph"}, {"children", QJsonArray{QJsonObject{{"text", text}}}}}}}}}}};
            return shape;
        };
        QJsonObject main = baseShape(mainID, "frame", 0, 0, 80, 40);
        main["name"] = "Card";
        main["componentRoot"] = true;
        main["mainInstance"] = true;
        main["componentId"] = componentID;
        main["shapes"] = QJsonArray{mainLabelID};
        QJsonObject copy = baseShape(copyID, "frame", 200, 100, 80, 40);
        copy["name"] = "Card Copy";
        copy["componentRoot"] = true;
        copy["componentId"] = componentID;
        copy["shapeRef"] = mainID;
        copy["shapes"] = QJsonArray{copyLabelID};

        VectorDocument document = PenpotImporter::parse(penpotFile(
            {main, label(mainLabelID, mainID, 10, "Title"), copy, label(copyLabelID, copyID, 210, "Changed")}, {mainID, copyID}));
        Components::sync(document);
        const VectorObject *card = named(document, QStringLiteral("Card"));
        const VectorObject *copied = named(document, QStringLiteral("Card Copy"));
        QVERIFY(card && copied && copied->instance);
        QCOMPARE(copied->instance->master, card->id);
        QCOMPARE(copied->path.bounds(), QRectF(200, 100, 80, 40));
        const auto kids = children(document, copied->id);
        QCOMPARE(kids.size(), size_t(1));
        QCOMPARE(kids.front()->text.text, QStringLiteral("Changed"));
        // The main's label sits at x=10; the copy's, moved by the copy's offset.
        QCOMPARE(kids.front()->transform.map(QPointF(0, 0)).x() - children(document, card->id).front()->transform.map(QPointF(0, 0)).x(), 200.0);
    }

    void aFrameThatListsItselfDoesNotRecurseForever()
    {
        const QString a = newID(), b = newID();
        QJsonObject first = baseShape(a, "frame", 0, 0, 50, 50);
        first["shapes"] = QJsonArray{a, b};
        QJsonObject second = baseShape(b, "group", 0, 0, 10, 10);
        second["shapes"] = QJsonArray{a, b};
        QStringList warnings;
        const VectorDocument document = PenpotImporter::parse(penpotFile({first, second}, {a, b}), &warnings);
        QCOMPARE(document.objects.size(), size_t(3)); // the page layer, the frame, the group
        QVERIFY(!warnings.isEmpty());
    }

    void theRootIdListedAsAChildIsSkipped()
    {
        const QString a = newID();
        QJsonObject frame = baseShape(a, "frame", 0, 0, 50, 50);
        frame["shapes"] = QJsonArray{QStringLiteral("00000000-0000-0000-0000-000000000000")};
        const VectorDocument document = PenpotImporter::parse(penpotFile({frame}, {a}));
        QVERIFY(named(document, QStringLiteral("frame")));
    }

    void missingManifestIsAFileError() { QVERIFY_THROWS_EXCEPTION(FileError, PenpotImporter::parse(QByteArrayLiteral("not a zip"))); }

    void readsFromDisk()
    {
        const QString id = newID();
        QJsonObject rect = baseShape(id, "rect", 0, 0, 10, 10);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Design.penpot"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(penpotFile({rect}, {id}));
        file.close();
        const VectorDocument document = PenpotImporter::read(path);
        QCOMPARE(document.find(document.layers().front())->name, QString("Page 1"));
    }
};

QTEST_MAIN(PenpotImporterTests)
#include "PenpotImporterTests.moc"
