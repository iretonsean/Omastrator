#include "IO/SketchImporter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

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

// A minimal stored-only zip: enough for ZipReader to read back what was put in.
QByteArray buildZip(const QList<std::pair<QString, QByteArray>> &entries)
{
    QByteArray out;
    struct Written {
        QString name;
        quint32 offset, crc, size;
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
        written.append({name, offset, 0, quint32(data.size())});
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

QJsonObject colorJson(double r, double g, double b, double a = 1)
{
    return {{"red", r}, {"green", g}, {"blue", b}, {"alpha", a}};
}

QJsonObject frameJson(double x, double y, double w, double h)
{
    return {{"_class", "rect"}, {"x", x}, {"y", y}, {"width", w}, {"height", h}};
}

QJsonObject fillStyle(const QJsonObject &color)
{
    return {{"fills", QJsonArray{QJsonObject{{"isEnabled", true}, {"fillType", 0}, {"color", color},
                                              {"contextSettings", QJsonObject{{"opacity", 1}, {"blendMode", 0}}}}}},
            {"contextSettings", QJsonObject{{"opacity", 1}, {"blendMode", 0}}}};
}

QJsonObject textLayer(const QString &id, const QString &name, const QJsonObject &frame, const QString &text, double fontSize,
                       const QString &fontName, int alignment = 0)
{
    QJsonObject run{{"location", 0},
                     {"length", text.size()},
                     {"attributes", QJsonObject{{"MSAttributedStringFontAttribute",
                                                  QJsonObject{{"attributes", QJsonObject{{"name", fontName}, {"size", fontSize}}}}},
                                                 {"paragraphStyle", QJsonObject{{"alignment", alignment}}}}}};
    return {{"_class", "text"}, {"do_objectID", id}, {"name", name}, {"frame", frame}, {"rotation", 0}, {"isVisible", true},
            {"isLocked", false}, {"textBehaviour", 0},
            {"attributedString", QJsonObject{{"string", text}, {"attributes", QJsonArray{run}}}}};
}

// A whole .sketch file: document.json plus one page per {name, layers}.
QByteArray sketchFile(const QJsonArray &colorAssets, const QList<std::pair<QString, QJsonArray>> &pages)
{
    QJsonArray pageRefs;
    QList<std::pair<QString, QByteArray>> entries;
    int index = 0;
    for (const auto &[name, layers] : pages) {
        const QString ref = QStringLiteral("pages/PAGE%1").arg(index++);
        pageRefs.append(QJsonObject{{"_class", "MSJSONFileReference"}, {"_ref_class", "MSImmutablePage"}, {"_ref", ref}});
        const QJsonObject page{{"_class", "page"}, {"do_objectID", ref}, {"name", name}, {"layers", layers}};
        entries.append({ref + QStringLiteral(".json"), QJsonDocument(page).toJson(QJsonDocument::Compact)});
    }
    const QJsonObject document{{"_class", "document"}, {"pages", pageRefs},
                                {"assets", QJsonObject{{"colors", colorAssets}}}};
    entries.prepend({QStringLiteral("document.json"), QJsonDocument(document).toJson(QJsonDocument::Compact)});
    return buildZip(entries);
}

}

class SketchImporterTests : public QObject {
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
    void canReadDetectsAZipWithDocumentJson()
    {
        QVERIFY(SketchImporter::canRead(sketchFile({}, {{"Page 1", QJsonArray{}}})));
        QVERIFY(!SketchImporter::canRead(QByteArrayLiteral("not a zip")));
        QVERIFY(!SketchImporter::canRead(buildZip({{QStringLiteral("hello.txt"), QByteArrayLiteral("hi")}})));
    }

    void artboardBecomesAnArtboardWithABackground()
    {
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard 1"},
                                    {"frame", frameJson(0, 0, 400, 300)}, {"hasBackgroundColor", true},
                                    {"backgroundColor", colorJson(0.1, 0.2, 0.3)}, {"layers", QJsonArray()}};
        const VectorDocument document = SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}}));
        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().name, QString("Artboard 1"));
        QCOMPARE(document.artboards.front().rect, QRectF(0, 0, 400, 300));
        QCOMPARE(document.artboards.front().background, QColor::fromRgbF(0.1f, 0.2f, 0.3f));
        QCOMPARE(document.layers().size(), size_t(1));
        QCOMPARE(document.find(document.layers().front())->name, QString("Page 1"));
    }

    void rectangleWithFixedRadiusBecomesALiveRectangle()
    {
        QJsonObject rect{{"_class", "rectangle"}, {"do_objectID", "R1"}, {"name", "Box"}, {"frame", frameJson(10, 10, 100, 60)},
                          {"rotation", 0}, {"isVisible", true}, {"isLocked", false}, {"fixedRadius", 8},
                          {"style", fillStyle(colorJson(1, 0.2, 0.2))}};
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard 1"},
                                    {"frame", frameJson(0, 0, 400, 300)}, {"layers", QJsonArray{rect}}};
        const VectorDocument document = SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}}));
        const VectorObject *box = named(document, QStringLiteral("Box"));
        QVERIFY(box);
        QCOMPARE(box->kind, ObjectKind::path);
        QVERIFY(box->shape.has_value());
        QCOMPARE(box->shape->radii[0], 8.0);
        QCOMPARE(box->fill.kind, PaintKind::solid);
        QCOMPARE(box->path.bounds(), QRectF(10, 10, 100, 60));
    }

    void perPointCornerRadiusOverridesFixedRadius()
    {
        const QJsonArray points{QJsonObject{{"cornerRadius", 1}}, QJsonObject{{"cornerRadius", 2}},
                                 QJsonObject{{"cornerRadius", 3}}, QJsonObject{{"cornerRadius", 4}}};
        QJsonObject rect{{"_class", "rectangle"}, {"do_objectID", "R1"}, {"name", "Box"}, {"frame", frameJson(0, 0, 50, 50)},
                          {"fixedRadius", 99}, {"points", points}, {"style", fillStyle(colorJson(1, 1, 1))}};
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard"},
                                    {"frame", frameJson(0, 0, 200, 200)}, {"layers", QJsonArray{rect}}};
        const VectorDocument document = SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}}));
        const VectorObject *box = named(document, QStringLiteral("Box"));
        QVERIFY(box);
        QCOMPARE(box->shape->radii[0], 1.0);
        QCOMPARE(box->shape->radii[2], 3.0);
    }

    void shapeGroupCombinesItsChildrenByBooleanOperation()
    {
        const QJsonObject a{{"_class", "rectangle"}, {"frame", frameJson(0, 0, 60, 60)}, {"isVisible", true}};
        const QJsonObject b{{"_class", "rectangle"}, {"frame", frameJson(30, 30, 60, 60)}, {"isVisible", true}, {"booleanOperation", 0}};
        const QJsonObject group{{"_class", "shapeGroup"}, {"do_objectID", "SG1"}, {"name", "Union"}, {"frame", frameJson(0, 0, 90, 90)},
                                 {"layers", QJsonArray{a, b}}, {"style", fillStyle(colorJson(0, 0, 0))}};
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard"},
                                    {"frame", frameJson(0, 0, 200, 200)}, {"layers", QJsonArray{group}}};
        const VectorDocument document = SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}}));
        const VectorObject *combined = named(document, QStringLiteral("Union"));
        QVERIFY(combined);
        QCOMPARE(combined->kind, ObjectKind::path);
        QVERIFY(!combined->path.isEmpty());
        // The union of two overlapping 60x60 squares offset by 30,30 is bigger than either alone.
        QVERIFY(combined->path.bounds().width() > 60);
    }

    void textCarriesFontSizeAndAlignment()
    {
        const QJsonObject text = textLayer("T1", "Label", frameJson(20, 20, 150, 30), "Hello Sketch", 18, "Helvetica-Bold", 2);
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard"},
                                    {"frame", frameJson(0, 0, 300, 300)}, {"layers", QJsonArray{text}}};
        const VectorDocument document = SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}}));
        const VectorObject *label = named(document, QStringLiteral("Label"));
        QVERIFY(label);
        QCOMPARE(label->kind, ObjectKind::text);
        QCOMPARE(label->text.text, QString("Hello Sketch"));
        QCOMPARE(label->text.family, QString("Helvetica"));
        QCOMPARE(label->text.style, QString("Bold"));
        QCOMPARE(label->text.size, 18.0);
        QCOMPARE(label->text.alignment, TextAlignment::center);
    }

    void colorAssetsBecomeTokens()
    {
        const QJsonArray colors{QJsonObject{{"name", "Brand/Red"}, {"color", colorJson(1, 0, 0)}}};
        const VectorDocument document = SketchImporter::parse(sketchFile(colors, {{"Page 1", QJsonArray{}}}));
        QCOMPARE(document.tokens.size(), size_t(1));
        QCOMPARE(document.tokens.front().name, QString("Brand/Red"));
    }

    void symbolInstanceGetsTheMastersChildrenWithItsOverride()
    {
        const QJsonObject labelInMaster = textLayer("LABEL", "Label", frameJson(10, 10, 80, 20), "Default", 14, "Arial");
        const QJsonObject master{{"_class", "symbolMaster"}, {"do_objectID", "SM1"}, {"symbolID", "SYM-1"}, {"name", "Card"},
                                  {"frame", frameJson(0, 0, 100, 50)}, {"layers", QJsonArray{labelInMaster}}};

        const QJsonObject override_{{"overrideName", "LABEL_stringValue"}, {"value", "Overridden!"}};
        const QJsonObject instance{{"_class", "symbolInstance"}, {"do_objectID", "INST1"}, {"name", "Card Instance"},
                                    {"frame", frameJson(0, 0, 100, 50)}, {"rotation", 0}, {"isVisible", true}, {"isLocked", false},
                                    {"symbolID", "SYM-1"}, {"overrideValues", QJsonArray{override_}}};
        const QJsonObject artboard{{"_class", "artboard"}, {"do_objectID", "AB1"}, {"name", "Artboard"},
                                    {"frame", frameJson(0, 0, 300, 300)}, {"layers", QJsonArray{instance}}};

        // The master's page is listed second, ahead of the artboard that uses it.
        const VectorDocument document =
            SketchImporter::parse(sketchFile({}, {{"Page 1", QJsonArray{artboard}}, {"Symbols", QJsonArray{master}}}));

        const VectorObject *card = named(document, QStringLiteral("Card"));
        QVERIFY(card);
        QVERIFY(card->component.has_value());
        QCOMPARE(card->component->set, QString("Card"));
        const auto masterChildren = children(document, card->id);
        QCOMPARE(masterChildren.size(), size_t(1));
        QCOMPARE(masterChildren.front()->text.text, QString("Default"));

        const VectorObject *cardInstance = named(document, QStringLiteral("Card Instance"));
        QVERIFY(cardInstance);
        QVERIFY(cardInstance->instance.has_value());
        const auto instanceChildren = children(document, cardInstance->id);
        QCOMPARE(instanceChildren.size(), size_t(1));
        QCOMPARE(instanceChildren.front()->text.text, QString("Overridden!"));

        // Two real artboards: the page's own, and the symbol master's, on their own pages.
        QCOMPARE(document.artboards.size(), size_t(2));
        QCOMPARE(document.pageCount(), 2);
        QCOMPARE(document.allPages()[1].name, QString("Symbols"));
        QCOMPARE(document.pageOf(card->id), document.allPages()[1].id);
        QCOMPARE(document.pageOf(cardInstance->id), document.allPages()[0].id);
    }

    void sketchPagesBecomePagesFromTheOrigin()
    {
        const auto artboard = [](const QString &id, const QString &name, double width) {
            return QJsonObject{{"_class", "artboard"}, {"do_objectID", id}, {"name", name},
                               {"frame", frameJson(0, 0, width, 100)}, {"layers", QJsonArray{}}};
        };
        const VectorDocument document = SketchImporter::parse(sketchFile(
            {}, {{"Home", QJsonArray{artboard("A", "Wide", 400), artboard("B", "Narrow", 200)}}, {"Settings", QJsonArray{artboard("C", "Only", 300)}}}));
        QCOMPARE(document.pageCount(), 2);
        QCOMPARE(document.allPages()[0].name, QString("Home"));
        QCOMPARE(document.currentPageId(), document.allPages()[0].id);
        QCOMPARE(document.artboardsOn(document.allPages()[0].id).size(), size_t(2));
        QCOMPARE(document.artboardsOn(document.allPages()[0].id)[1].name, QString("Narrow"));
        // The second page starts at the origin, not beside the first.
        QCOMPARE(document.artboardsOn(document.allPages()[1].id).front().rect, QRectF(0, 0, 300, 100));
        QCOMPARE(document.layers().size(), size_t(1));
        QCOMPARE(document.find(document.layersOn(document.allPages()[1].id).front())->name, QString("Settings"));
        QCOMPARE(document.size, QSizeF(400, 100));
    }

    void missingDocumentJsonIsAFileError() { QVERIFY_THROWS_EXCEPTION(FileError, SketchImporter::parse(QByteArrayLiteral("not a zip"))); }

    void readsFromDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Design.sketch"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(sketchFile({}, {{"Page 1", QJsonArray{}}}));
        file.close();
        const VectorDocument document = SketchImporter::read(path);
        QCOMPARE(document.find(document.layers().front())->name, QString("Page 1"));
    }
};

QTEST_MAIN(SketchImporterTests)
#include "SketchImporterTests.moc"
