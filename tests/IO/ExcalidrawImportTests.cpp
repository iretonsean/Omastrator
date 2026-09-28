#include "IO/ExcalidrawImporter.h"
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace {

QJsonObject baseElement(const QString &id, const QString &type, double x, double y, double w, double h)
{
    QJsonObject o;
    o["id"] = id;
    o["type"] = type;
    o["x"] = x;
    o["y"] = y;
    o["width"] = w;
    o["height"] = h;
    o["angle"] = 0;
    o["strokeColor"] = QStringLiteral("#1e1e1e");
    o["backgroundColor"] = QStringLiteral("transparent");
    o["fillStyle"] = QStringLiteral("solid");
    o["strokeWidth"] = 2;
    o["strokeStyle"] = QStringLiteral("solid");
    o["opacity"] = 100;
    o["groupIds"] = QJsonArray();
    o["locked"] = false;
    o["isDeleted"] = false;
    return o;
}

}

class ExcalidrawImportTests : public QObject {
    Q_OBJECT

private:
    static const VectorObject *named(const VectorDocument &document, const QString &name)
    {
        for (const VectorObject &object : document.objects) {
            if (object.name == name)
                return &object;
        }
        return nullptr;
    }

    static std::vector<const VectorObject *> children(const VectorDocument &document, const QUuid &parent)
    {
        std::vector<const VectorObject *> result;
        for (const QUuid &id : document.children(parent))
            result.push_back(document.find(id));
        return result;
    }

private slots:
    void rectangleWithRoundnessBecomesALiveRectangle()
    {
        QJsonObject rect = baseElement(QStringLiteral("r1"), QStringLiteral("rectangle"), 10, 20, 100, 50);
        rect["backgroundColor"] = QStringLiteral("#ffc9c9");
        rect["roundness"] = QJsonObject{{"type", 3}};
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"version", 2}, {"elements", QJsonArray{rect}}, {"files", QJsonObject()}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *object = named(document, QStringLiteral("Rectangle"));
        QVERIFY(object);
        QCOMPARE(object->kind, ObjectKind::path);
        QVERIFY(object->shape.has_value());
        QCOMPARE(object->shape->rect, QRectF(0, 0, 100, 50));
        QVERIFY(object->shape->radii[0] > 0);
        QCOMPARE(object->fill.kind, PaintKind::solid);
        QCOMPARE(object->fill.color, QColor(QStringLiteral("#ffc9c9")));
        // Placed with the document's margin shift, at (10, 20) plus that shift.
        const QRectF bounds = object->path.bounds();
        QCOMPARE(bounds.width(), 100.0);
        QCOMPARE(bounds.height(), 50.0);
    }

    void ellipseAndDiamondBecomePaths()
    {
        QJsonObject ellipse = baseElement(QStringLiteral("e1"), QStringLiteral("ellipse"), 0, 0, 40, 40);
        QJsonObject diamond = baseElement(QStringLiteral("d1"), QStringLiteral("diamond"), 50, 0, 40, 40);
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{ellipse, diamond}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *e = named(document, QStringLiteral("Ellipse"));
        const VectorObject *d = named(document, QStringLiteral("Diamond"));
        QVERIFY(e && d);
        QCOMPARE(e->kind, ObjectKind::path);
        QCOMPARE(d->kind, ObjectKind::path);
        QVERIFY(!d->path.isEmpty());
        QCOMPARE(d->path.nodeCount(), 4);
    }

    void arrowGetsItsArrowheads()
    {
        QJsonObject arrow = baseElement(QStringLiteral("a1"), QStringLiteral("arrow"), 0, 0, 100, 0);
        arrow["points"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{100, 0}};
        arrow["startArrowhead"] = QJsonValue();
        arrow["endArrowhead"] = QStringLiteral("triangle");
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{arrow}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *object = named(document, QStringLiteral("Arrow"));
        QVERIFY(object);
        QCOMPARE(object->stroke.startArrow, Arrowhead::none);
        QCOMPARE(object->stroke.endArrow, Arrowhead::triangle);
        QCOMPARE(object->fill.kind, PaintKind::none);
    }

    void freedrawBecomesASmoothedPath()
    {
        QJsonObject draw = baseElement(QStringLiteral("f1"), QStringLiteral("freedraw"), 0, 0, 30, 30);
        draw["points"] = QJsonArray{QJsonArray{0, 0}, QJsonArray{10, 5}, QJsonArray{20, 20}, QJsonArray{30, 30}};
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{draw}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *object = named(document, QStringLiteral("Drawing"));
        QVERIFY(object);
        QVERIFY(!object->path.isEmpty());
    }

    void textCarriesFontAndAlignment()
    {
        QJsonObject text = baseElement(QStringLiteral("t1"), QStringLiteral("text"), 5, 5, 80, 25);
        text["text"] = QStringLiteral("Hello");
        text["fontSize"] = 18;
        text["fontFamily"] = 5;
        text["textAlign"] = QStringLiteral("center");
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{text}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *object = named(document, QStringLiteral("Hello"));
        QVERIFY(object);
        QCOMPARE(object->kind, ObjectKind::text);
        QCOMPARE(object->text.text, QStringLiteral("Hello"));
        QCOMPARE(object->text.family, QStringLiteral("Excalifont"));
        QCOMPARE(object->text.size, 18.0);
        QCOMPARE(object->text.alignment, TextAlignment::center);
    }

    void imageIsDecodedFromTheFilesMap()
    {
        QImage source(3, 2, QImage::Format_RGB32);
        source.fill(QColor(20, 100, 200));
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "PNG");
        const QString dataUrl = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());

        QJsonObject image = baseElement(QStringLiteral("i1"), QStringLiteral("image"), 0, 0, 30, 20);
        image["fileId"] = QStringLiteral("file1");
        QJsonObject files{{"file1", QJsonObject{{"mimeType", QStringLiteral("image/png")}, {"dataURL", dataUrl}}}};
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{image}}, {"files", files}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *object = named(document, QStringLiteral("Image"));
        QVERIFY(object);
        QCOMPARE(object->kind, ObjectKind::image);
        QCOMPARE(object->image.size(), QSize(3, 2));
    }

    void groupIdsNestGroups()
    {
        QJsonObject a = baseElement(QStringLiteral("a"), QStringLiteral("rectangle"), 0, 0, 10, 10);
        a["groupIds"] = QJsonArray{QStringLiteral("inner"), QStringLiteral("outer")};
        QJsonObject b = baseElement(QStringLiteral("b"), QStringLiteral("rectangle"), 20, 0, 10, 10);
        b["groupIds"] = QJsonArray{QStringLiteral("inner"), QStringLiteral("outer")};
        QJsonObject c = baseElement(QStringLiteral("c"), QStringLiteral("rectangle"), 40, 0, 10, 10);
        c["groupIds"] = QJsonArray{QStringLiteral("outer")};
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{a, b, c}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const std::vector<QUuid> layers = document.layers();
        QCOMPARE(layers.size(), size_t(1));
        const auto topChildren = children(document, layers.front());
        // One outer group holding an inner group (with a and b) and c directly.
        QCOMPARE(topChildren.size(), size_t(1));
        QCOMPARE(topChildren.front()->kind, ObjectKind::group);
        const auto outerChildren = children(document, topChildren.front()->id);
        QCOMPARE(outerChildren.size(), size_t(2));
        QCOMPARE(outerChildren[0]->kind, ObjectKind::group);
        QCOMPARE(children(document, outerChildren[0]->id).size(), size_t(2));
        QCOMPARE(outerChildren[1]->kind, ObjectKind::path);
    }

    void frameHoldsItsChildren()
    {
        QJsonObject frame = baseElement(QStringLiteral("fr1"), QStringLiteral("frame"), 0, 0, 200, 200);
        frame["name"] = QStringLiteral("My Frame");
        QJsonObject rect = baseElement(QStringLiteral("r1"), QStringLiteral("rectangle"), 10, 10, 20, 20);
        rect["frameId"] = QStringLiteral("fr1");
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{frame, rect}}});
        const VectorDocument document = ExcalidrawImporter::parse(doc.toJson());
        const VectorObject *frameObject = named(document, QStringLiteral("My Frame"));
        QVERIFY(frameObject);
        QCOMPARE(frameObject->kind, ObjectKind::frame);
        const auto inside = children(document, frameObject->id);
        QCOMPARE(inside.size(), size_t(1));
        QCOMPARE(inside.front()->name, QStringLiteral("Rectangle"));
    }

    void sketchyFillTextureWarnsOnce()
    {
        QJsonObject rect = baseElement(QStringLiteral("r1"), QStringLiteral("rectangle"), 0, 0, 10, 10);
        rect["backgroundColor"] = QStringLiteral("#ffc9c9");
        rect["fillStyle"] = QStringLiteral("hachure");
        QJsonObject rect2 = baseElement(QStringLiteral("r2"), QStringLiteral("rectangle"), 20, 0, 10, 10);
        rect2["backgroundColor"] = QStringLiteral("#ffc9c9");
        rect2["fillStyle"] = QStringLiteral("cross-hatch");
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{rect, rect2}}});
        QStringList warnings;
        ExcalidrawImporter::parse(doc.toJson(), &warnings);
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.front().contains(QStringLiteral("solid fills")));
    }

    void canReadDistinguishesTheFormat()
    {
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray()}});
        QVERIFY(ExcalidrawImporter::canRead(doc.toJson()));
        QVERIFY(!ExcalidrawImporter::canRead(QByteArrayLiteral("{\"type\":\"figma\"}")));
        QVERIFY(!ExcalidrawImporter::canRead(QByteArrayLiteral("not json")));
    }

    void readsFromDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("My Drawing.excalidraw"));
        QJsonObject rect = baseElement(QStringLiteral("r1"), QStringLiteral("rectangle"), 0, 0, 10, 10);
        QJsonDocument doc(QJsonObject{{"type", QStringLiteral("excalidraw")}, {"elements", QJsonArray{rect}}});
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(doc.toJson());
        file.close();
        const VectorDocument document = ExcalidrawImporter::read(path);
        QCOMPARE(document.find(document.layers().front())->name, QStringLiteral("My Drawing"));
    }

    void emptyOrInvalidJsonIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, ExcalidrawImporter::parse("not json"));
        QVERIFY_THROWS_EXCEPTION(FileError, ExcalidrawImporter::parse("{\"type\":\"excalidraw\",\"elements\":[]}"));
    }
};

QTEST_MAIN(ExcalidrawImportTests)
#include "ExcalidrawImportTests.moc"
