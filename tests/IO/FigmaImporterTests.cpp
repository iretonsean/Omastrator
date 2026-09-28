#include "Document/Components.h"
#include "FigmaKiwiFixtures.h"
#include "IO/FigmaImporter.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
void writeGuid(KiwiWriter &w, quint32 session, quint32 local)
{
    w.writeVarUint(session);
    w.writeVarUint(local);
}

void writeParentIndex(KiwiWriter &w, quint32 session, quint32 local, const QString &position)
{
    writeGuid(w, session, local);
    w.writeString(position);
}

void writeColor(KiwiWriter &w, float r, float g, float b, float a)
{
    w.writeVarFloat(r);
    w.writeVarFloat(g);
    w.writeVarFloat(b);
    w.writeVarFloat(a);
}

void writeSolidPaint(KiwiWriter &w, float r, float g, float b, float a = 1.0f)
{
    w.writeString(QStringLiteral("SOLID"));
    w.writeBool(true); // visible
    w.writeVarFloat(1); // opacity
    writeColor(w, r, g, b, a);
}

void writeVec2(KiwiWriter &w, float x, float y)
{
    w.writeVarFloat(x);
    w.writeVarFloat(y);
}

// Identity, translated.
void writeMatrix(KiwiWriter &w, float tx, float ty)
{
    w.writeVarFloat(1);
    w.writeVarFloat(0);
    w.writeVarFloat(tx);
    w.writeVarFloat(0);
    w.writeVarFloat(1);
    w.writeVarFloat(ty);
}

// Little-endian packing for the vector network blob, which isn't schema-driven.
void appendU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 24) & 0xff));
}

void appendFloat(QByteArray &out, float v)
{
    quint32 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    appendU32(out, bits);
}

// A flat triangle: three straight-line segments, one nonzero-wound region.
QByteArray triangleBlob()
{
    QByteArray blob;
    appendU32(blob, 3); // vertices
    appendU32(blob, 3); // segments
    appendU32(blob, 1); // regions
    const QPointF vertices[3] = {{0, 0}, {20, 0}, {10, 20}};
    for (const QPointF &v : vertices) {
        appendU32(blob, 0); // styleID
        appendFloat(blob, float(v.x()));
        appendFloat(blob, float(v.y()));
    }
    const int segments[3][2] = {{0, 1}, {1, 2}, {2, 0}};
    for (const auto &s : segments) {
        appendU32(blob, 0); // styleID
        appendU32(blob, quint32(s[0])); // start
        appendFloat(blob, 0); // tangentStart
        appendFloat(blob, 0);
        appendU32(blob, quint32(s[1])); // end
        appendFloat(blob, 0); // tangentEnd
        appendFloat(blob, 0);
    }
    appendU32(blob, 1); // flags: nonzero winding
    appendU32(blob, 1); // one loop
    appendU32(blob, 3); // three segment indices
    appendU32(blob, 0);
    appendU32(blob, 1);
    appendU32(blob, 2);
    return blob;
}

// Schema indices, declared in KiwiSchemaBuilder order below.
enum SchemaIndex { Vec2, Guid, Matrix, ParentIndex, Color, Paint, VectorData, FontName, StyleOverrideEntry, TextData, GuidPath, OverrideEntry, SymbolData, Blob, Node, Message };

QByteArray buildSchema()
{
    KiwiSchemaBuilder s;
    s.addStruct(QStringLiteral("Vec2"));
    s.field(Vec2, QStringLiteral("x"), QStringLiteral("float"), 1);
    s.field(Vec2, QStringLiteral("y"), QStringLiteral("float"), 2);

    s.addStruct(QStringLiteral("GUID"));
    s.field(Guid, QStringLiteral("sessionID"), QStringLiteral("uint"), 1);
    s.field(Guid, QStringLiteral("localID"), QStringLiteral("uint"), 2);

    s.addStruct(QStringLiteral("Matrix"));
    for (const char *name : {"m00", "m01", "m02", "m10", "m11", "m12"})
        s.field(Matrix, QString::fromLatin1(name), QStringLiteral("float"), 1);

    s.addStruct(QStringLiteral("ParentIndex"));
    s.fieldOfType(ParentIndex, QStringLiteral("guid"), Guid, 1);
    s.field(ParentIndex, QStringLiteral("position"), QStringLiteral("string"), 2);

    s.addStruct(QStringLiteral("Color"));
    for (const char *name : {"r", "g", "b", "a"})
        s.field(Color, QString::fromLatin1(name), QStringLiteral("float"), 1);

    s.addStruct(QStringLiteral("Paint"));
    s.field(Paint, QStringLiteral("type"), QStringLiteral("string"), 1);
    s.field(Paint, QStringLiteral("visible"), QStringLiteral("bool"), 2);
    s.field(Paint, QStringLiteral("opacity"), QStringLiteral("float"), 3);
    s.fieldOfType(Paint, QStringLiteral("color"), Color, 4);

    s.addStruct(QStringLiteral("VectorData"));
    s.field(VectorData, QStringLiteral("vectorNetworkBlob"), QStringLiteral("int"), 1);
    s.fieldOfType(VectorData, QStringLiteral("normalizedSize"), Vec2, 2);

    s.addStruct(QStringLiteral("FontName"));
    s.field(FontName, QStringLiteral("family"), QStringLiteral("string"), 1);
    s.field(FontName, QStringLiteral("style"), QStringLiteral("string"), 2);

    s.addStruct(QStringLiteral("StyleOverrideEntry"));
    s.field(StyleOverrideEntry, QStringLiteral("styleID"), QStringLiteral("uint"), 1);
    s.fieldOfType(StyleOverrideEntry, QStringLiteral("fontName"), FontName, 2);
    s.field(StyleOverrideEntry, QStringLiteral("fontSize"), QStringLiteral("float"), 3);

    s.addStruct(QStringLiteral("TextData"));
    s.field(TextData, QStringLiteral("characters"), QStringLiteral("string"), 1);
    s.field(TextData, QStringLiteral("characterStyleIDs"), QStringLiteral("uint"), 2, true);
    s.fieldOfType(TextData, QStringLiteral("styleOverrideTable"), StyleOverrideEntry, 3, true);

    s.addStruct(QStringLiteral("GuidPath"));
    s.fieldOfType(GuidPath, QStringLiteral("guids"), Guid, 1, true);

    s.addStruct(QStringLiteral("OverrideEntry"));
    s.fieldOfType(OverrideEntry, QStringLiteral("guidPath"), GuidPath, 1);
    s.fieldOfType(OverrideEntry, QStringLiteral("fillPaints"), Paint, 2, true);

    s.addStruct(QStringLiteral("SymbolData"));
    s.fieldOfType(SymbolData, QStringLiteral("symbolID"), Guid, 1);
    s.fieldOfType(SymbolData, QStringLiteral("symbolOverrides"), OverrideEntry, 2, true);

    s.addStruct(QStringLiteral("Blob"));
    s.field(Blob, QStringLiteral("bytes"), QStringLiteral("byte"), 1, true);

    s.addMessage(QStringLiteral("Node"));
    s.fieldOfType(Node, QStringLiteral("guid"), Guid, 1);
    s.fieldOfType(Node, QStringLiteral("parentIndex"), ParentIndex, 2);
    s.field(Node, QStringLiteral("type"), QStringLiteral("string"), 3);
    s.field(Node, QStringLiteral("name"), QStringLiteral("string"), 4);
    s.field(Node, QStringLiteral("visible"), QStringLiteral("bool"), 5);
    s.field(Node, QStringLiteral("opacity"), QStringLiteral("float"), 6);
    s.fieldOfType(Node, QStringLiteral("size"), Vec2, 7);
    s.fieldOfType(Node, QStringLiteral("transform"), Matrix, 8);
    s.fieldOfType(Node, QStringLiteral("fillPaints"), Paint, 9, true);
    s.field(Node, QStringLiteral("cornerRadius"), QStringLiteral("float"), 10);
    s.field(Node, QStringLiteral("stackMode"), QStringLiteral("string"), 11);
    s.field(Node, QStringLiteral("stackSpacing"), QStringLiteral("float"), 12);
    s.fieldOfType(Node, QStringLiteral("vectorData"), VectorData, 13);
    s.fieldOfType(Node, QStringLiteral("textData"), TextData, 14);
    s.fieldOfType(Node, QStringLiteral("symbolData"), SymbolData, 15);

    s.addMessage(QStringLiteral("Message"));
    s.fieldOfType(Message, QStringLiteral("nodeChanges"), Node, 1, true);
    s.fieldOfType(Message, QStringLiteral("blobs"), Blob, 2, true);

    return s.encode();
}

// A small document: a page with an auto-layout frame (a rectangle, styled text
// and a vector triangle), a component and an instance overriding it.
QByteArray buildMessage()
{
    KiwiWriter node;
    QByteArray message;
    const auto flushNode = [&] {
        node.writeVarUint(0); // ends the tagged-field loop
        message.append(node.bytes);
        node.bytes.clear();
    };

    // Page (CANVAS), no parent.
    node.writeVarUint(1);
    writeGuid(node, 1, 2);
    node.writeVarUint(3);
    node.writeString(QStringLiteral("CANVAS"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Page 1"));
    flushNode();

    // Frame "Card": auto layout, fill, rounded corners.
    node.writeVarUint(1);
    writeGuid(node, 1, 3);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 2, QStringLiteral("a"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("FRAME"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Card"));
    node.writeVarUint(7);
    writeVec2(node, 200, 140);
    node.writeVarUint(8);
    writeMatrix(node, 0, 0);
    node.writeVarUint(9);
    node.writeVarUint(1);
    writeSolidPaint(node, 0.9f, 0.9f, 0.9f);
    node.writeVarUint(10);
    node.writeVarFloat(8);
    node.writeVarUint(11);
    node.writeString(QStringLiteral("HORIZONTAL"));
    node.writeVarUint(12);
    node.writeVarFloat(12);
    flushNode();

    // Rectangle "Swatch": solid red fill.
    node.writeVarUint(1);
    writeGuid(node, 1, 4);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 3, QStringLiteral("a"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("RECTANGLE"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Swatch"));
    node.writeVarUint(7);
    writeVec2(node, 40, 40);
    node.writeVarUint(8);
    writeMatrix(node, 0, 0);
    node.writeVarUint(9);
    node.writeVarUint(1);
    writeSolidPaint(node, 1, 0, 0);
    flushNode();

    // Text "Label": "Hi!" with a bold run over "i!".
    node.writeVarUint(1);
    writeGuid(node, 1, 5);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 3, QStringLiteral("b"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("TEXT"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Label"));
    node.writeVarUint(7);
    writeVec2(node, 100, 20);
    node.writeVarUint(8);
    writeMatrix(node, 60, 0);
    node.writeVarUint(14);
    node.writeString(QStringLiteral("Hi!"));
    node.writeVarUint(3);
    node.writeVarUint(0);
    node.writeVarUint(1);
    node.writeVarUint(1);
    node.writeVarUint(1);
    node.writeVarUint(1);
    node.writeVarUint(1);
    node.writeString(QStringLiteral("Inter"));
    node.writeString(QStringLiteral("Bold"));
    node.writeVarFloat(18);
    flushNode();

    // Vector "Tri": a flattened triangle, via blob 0.
    node.writeVarUint(1);
    writeGuid(node, 1, 6);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 3, QStringLiteral("c"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("VECTOR"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Tri"));
    node.writeVarUint(7);
    writeVec2(node, 20, 20);
    node.writeVarUint(8);
    writeMatrix(node, 0, 60);
    node.writeVarUint(13);
    node.writeVarInt(0);
    writeVec2(node, 1, 1);
    flushNode();

    // Component master "Button", holding "ButtonBG".
    node.writeVarUint(1);
    writeGuid(node, 1, 7);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 2, QStringLiteral("b"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("SYMBOL"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Button"));
    node.writeVarUint(7);
    writeVec2(node, 80, 32);
    node.writeVarUint(8);
    writeMatrix(node, 300, 0);
    flushNode();

    node.writeVarUint(1);
    writeGuid(node, 1, 8);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 7, QStringLiteral("a"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("RECTANGLE"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("ButtonBG"));
    node.writeVarUint(7);
    writeVec2(node, 80, 32);
    node.writeVarUint(8);
    writeMatrix(node, 0, 0);
    node.writeVarUint(9);
    node.writeVarUint(1);
    writeSolidPaint(node, 0, 0, 1);
    flushNode();

    // Instance of Button, overriding ButtonBG's fill to green.
    node.writeVarUint(1);
    writeGuid(node, 1, 9);
    node.writeVarUint(2);
    writeParentIndex(node, 1, 2, QStringLiteral("c"));
    node.writeVarUint(3);
    node.writeString(QStringLiteral("INSTANCE"));
    node.writeVarUint(4);
    node.writeString(QStringLiteral("Button Instance"));
    node.writeVarUint(7);
    writeVec2(node, 80, 32);
    node.writeVarUint(8);
    writeMatrix(node, 300, 80);
    node.writeVarUint(15);
    writeGuid(node, 1, 7); // symbolID
    node.writeVarUint(1); // one override
    node.writeVarUint(1); // guidPath.guids: one guid
    writeGuid(node, 1, 8); // guidPath.guids[0]: ButtonBG
    node.writeVarUint(1); // one fill paint
    writeSolidPaint(node, 0, 1, 0);
    flushNode();

    KiwiWriter root;
    root.writeVarUint(1);
    root.writeVarUint(8); // 8 nodeChanges
    root.bytes += message;
    root.writeVarUint(2);
    root.writeVarUint(1); // one blob
    root.writeByteArray(triangleBlob());
    root.writeVarUint(0);
    return root.bytes;
}

QByteArray fixtureFile()
{
    return kiwiContainer(buildSchema(), buildMessage());
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

class FigmaImporterTests : public QObject {
    Q_OBJECT

private slots:
    void canReadSniffsTheMagic()
    {
        QVERIFY(FigmaImporter::canRead(fixtureFile()));
        QVERIFY(!FigmaImporter::canRead(QByteArray("not a figma file")));
    }

    void pageBecomesAnArtboard()
    {
        QStringList warnings;
        const VectorDocument document = FigmaImporter::parse(fixtureFile(), &warnings);
        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().name, QStringLiteral("Page 1"));
        QCOMPARE(document.layers().size(), size_t(1));
    }

    void frameCarriesAutoLayoutFillAndCorners()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *card = named(document, QStringLiteral("Card"));
        QVERIFY(card);
        QCOMPARE(card->kind, ObjectKind::frame);
        QVERIFY(card->autoLayout.has_value());
        QCOMPARE(card->autoLayout->direction, LayoutDirection::horizontal);
        QCOMPARE(card->autoLayout->gap, 12.0);
        QVERIFY(card->shape.has_value());
        for (double radius : card->shape->radii)
            QCOMPARE(radius, 8.0);
        QVERIFY(card->fill.isVisible());
    }

    void rectangleTakesItsFill()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *swatch = named(document, QStringLiteral("Swatch"));
        QVERIFY(swatch);
        QCOMPARE(swatch->kind, ObjectKind::path);
        QCOMPARE(swatch->fill.kind, PaintKind::solid);
        QCOMPARE(swatch->fill.color.redF(), 1.0f);
        QCOMPARE(swatch->fill.color.greenF(), 0.0f);
    }

    void textCarriesAStyledRun()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *label = named(document, QStringLiteral("Label"));
        QVERIFY(label);
        QCOMPARE(label->kind, ObjectKind::text);
        QCOMPARE(label->text.text, QStringLiteral("Hi!"));
        QCOMPARE(label->text.runs.size(), size_t(1));
        const TextRun &run = label->text.runs.front();
        QCOMPARE(run.start, 1);
        QCOMPARE(run.length, 2);
        QCOMPARE(run.format.style, QStringLiteral("Bold"));
        QCOMPARE(run.format.size, 18.0);
    }

    void vectorBlobBecomesATriangle()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *tri = named(document, QStringLiteral("Tri"));
        QVERIFY(tri);
        QCOMPARE(tri->kind, ObjectKind::path);
        QCOMPARE(tri->path.contours.size(), size_t(1));
        QCOMPARE(tri->path.contours.front().nodes.size(), size_t(3));
        QVERIFY(tri->path.contours.front().closed);
    }

    void instanceOverridesTheMasterFill()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *instance = named(document, QStringLiteral("Button Instance"));
        QVERIFY(instance);
        QCOMPARE(instance->kind, ObjectKind::group);
        QVERIFY(instance->instance.has_value());
        const std::vector<QUuid> children = document.children(instance->id);
        QCOMPARE(children.size(), size_t(1));
        const VectorObject *copy = document.find(children.front());
        QVERIFY(copy);
        QCOMPARE(copy->name, QStringLiteral("ButtonBG"));
        // Overridden to green, not the master's blue.
        QCOMPARE(copy->fill.color.greenF(), 1.0f);
        QCOMPARE(copy->fill.color.blueF(), 0.0f);
    }

    void componentMasterKeepsItsOwnFill()
    {
        const VectorDocument document = FigmaImporter::parse(fixtureFile());
        const VectorObject *button = named(document, QStringLiteral("Button"));
        QVERIFY(button);
        QVERIFY(button->component.has_value());
        QCOMPARE(button->component->set, QStringLiteral("Button"));
        const std::vector<QUuid> children = document.children(button->id);
        QCOMPARE(children.size(), size_t(1));
        const VectorObject *bg = document.find(children.front());
        QCOMPARE(bg->fill.color.blueF(), 1.0f);
    }

    void notFigmaDataThrows()
    {
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(QByteArray("hello")), FileError);
    }

    void pasteReadsTheFigmaHtmlComment()
    {
        const QByteArray html = "<!--(figmeta)" + QByteArray("anything").toBase64() + "(/figmeta)-->"
            + "<!--(figma)" + fixtureFile().toBase64() + "(/figma)-->";
        QVERIFY(FigmaImporter::isFigmaClipboardHtml(html));
        const VectorDocument document = FigmaImporter::parseClipboardHtml(html);
        QVERIFY(named(document, QStringLiteral("Card")));
    }

    void pasteRejectsPlainHtml()
    {
        const QByteArray html = "<html><body>hi</body></html>";
        QVERIFY(!FigmaImporter::isFigmaClipboardHtml(html));
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parseClipboardHtml(html), FileError);
    }

    void linkParsesKeyAndNodeID()
    {
        const auto design = FigmaImporter::parseLink(QStringLiteral("https://www.figma.com/design/abc123/My-File?node-id=1-2&t=xyz"));
        QVERIFY(design.has_value());
        QCOMPARE(design->fileKey, QStringLiteral("abc123"));
        QCOMPARE(design->nodeID, QStringLiteral("1:2"));

        const auto file = FigmaImporter::parseLink(QStringLiteral("https://www.figma.com/file/def456/Old-Style-Link"));
        QVERIFY(file.has_value());
        QCOMPARE(file->fileKey, QStringLiteral("def456"));
        QVERIFY(file->nodeID.isEmpty());

        QVERIFY(!FigmaImporter::parseLink(QStringLiteral("https://example.com/not-figma")).has_value());
    }

    void tokenRoundTripsWithRestrictivePermissions()
    {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        QVERIFY(!FigmaImporter::Token::load().has_value());
        FigmaImporter::Token::save(QStringLiteral("figd_secret"));
        const auto loaded = FigmaImporter::Token::load();
        QVERIFY(loaded.has_value());
        QCOMPARE(*loaded, QStringLiteral("figd_secret"));
        const QString path = QDir(config.path()).filePath(QStringLiteral("omastrator/figma.json"));
        QVERIFY(QFile::exists(path));
        const QFile::Permissions perms = QFile(path).permissions();
        QVERIFY(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
        FigmaImporter::Token::forget();
        QVERIFY(!FigmaImporter::Token::load().has_value());
        qunsetenv("XDG_CONFIG_HOME");
    }

    void figFileOpensAsAZip()
    {
        const QByteArray fig = buildZipArchive(
            {{QStringLiteral("canvas.fig"), fixtureFile(), false}, {QStringLiteral("thumbnail.png"), "not a real png", false}});
        QVERIFY(FigmaImporter::canRead(fig));
        const VectorDocument document = FigmaImporter::parse(fig);
        QVERIFY(named(document, QStringLiteral("Card")));
    }

    // A small, hand-written REST API response (docs.figma.com): one page with an
    // auto-layout frame holding a rectangle, through GET /v1/files/:key.
    void restFileMapsAutoLayoutAndFills()
    {
        const QByteArray json = R"({
            "document": {
                "id": "0:0", "type": "DOCUMENT", "name": "Document",
                "children": [{
                    "id": "0:1", "type": "CANVAS", "name": "Page 1",
                    "children": [{
                        "id": "1:1", "type": "FRAME", "name": "Card",
                        "size": {"x": 200, "y": 140},
                        "relativeTransform": [[1, 0, 0], [0, 1, 0]],
                        "layoutMode": "VERTICAL", "itemSpacing": 8,
                        "paddingLeft": 4, "paddingTop": 4, "paddingRight": 4, "paddingBottom": 4,
                        "primaryAxisAlignItems": "MIN", "counterAxisAlignItems": "CENTER",
                        "fills": [{"type": "SOLID", "color": {"r": 0.2, "g": 0.4, "b": 0.9, "a": 1}, "opacity": 1, "visible": true}],
                        "cornerRadius": 6,
                        "children": [{
                            "id": "1:2", "type": "RECTANGLE", "name": "Swatch",
                            "size": {"x": 40, "y": 40},
                            "relativeTransform": [[1, 0, 0], [0, 1, 0]],
                            "fills": [{"type": "SOLID", "color": {"r": 1, "g": 0, "b": 0, "a": 1}, "opacity": 1, "visible": true}]
                        }]
                    }]
                }]
            }
        })";
        QStringList warnings;
        const VectorDocument document = FigmaImporter::parseRestFile(json, QString(), &warnings);
        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().name, QStringLiteral("Page 1"));
        const VectorObject *card = named(document, QStringLiteral("Card"));
        QVERIFY(card);
        QCOMPARE(card->kind, ObjectKind::frame);
        QVERIFY(card->autoLayout.has_value());
        QCOMPARE(card->autoLayout->direction, LayoutDirection::vertical);
        QCOMPARE(card->autoLayout->gap, 8.0);
        QCOMPARE(card->shape->radii[0], 6.0);
        const VectorObject *swatch = named(document, QStringLiteral("Swatch"));
        QVERIFY(swatch);
        QCOMPARE(swatch->fill.color.redF(), 1.0f);
    }

    void restErrorResponseThrows()
    {
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parseRestFile(R"({"status":403,"err":"Invalid token"})", QString()), FileError);
    }
};

QTEST_MAIN(FigmaImporterTests)
#include "FigmaImporterTests.moc"
