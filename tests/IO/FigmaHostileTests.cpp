#include "FigmaKiwiFixtures.h"
#include "IO/FigmaImporter.h"
#include <QTest>

// A few bytes of hostile input must end in a FileError or a warning, never a crash or an
// unbounded allocation (Open and Place catch only FileError).
namespace {
QByteArray le32(quint32 v)
{
    QByteArray b;
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
    return b;
}

QByteArray blobHeader(quint32 vertices, quint32 segments, quint32 regions)
{
    return le32(vertices) + le32(segments) + le32(regions);
}

// A fig-kiwi container around raw chunk bytes, skipping the compression step.
QByteArray containerOf(const QByteArray &schemaChunk, const QByteArray &messageChunk)
{
    return QByteArray("fig-kiwi") + le32(42) + le32(quint32(schemaChunk.size())) + schemaChunk + le32(quint32(messageChunk.size())) + messageChunk;
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

class FigmaHostileTests : public QObject {
    Q_OBJECT

private slots:
    void aSchemaClaimingBillionsOfDefinitionsThrows()
    {
        KiwiWriter schema;
        schema.writeVarUint(4000000000u);
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(schema.bytes, QByteArray(1, '\0'))), FileError);
    }

    void aDefinitionClaimingBillionsOfFieldsThrows()
    {
        KiwiWriter schema;
        schema.writeVarUint(1);
        schema.writeString(QStringLiteral("Message"));
        schema.writeByte(2);
        schema.writeVarUint(4000000000u);
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(schema.bytes, QByteArray(1, '\0'))), FileError);
    }

    void anArrayLongerThanTheDataThrows()
    {
        KiwiWriter message;
        message.writeVarUint(1);
        message.writeVarUint(0x7fffffffu);
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(MinimalFig::schema(), message.bytes)), FileError);
    }

    void aStructThatContainsItselfThrows()
    {
        KiwiSchemaBuilder s;
        const int loop = s.addStruct(QStringLiteral("Loop"));
        s.fieldOfType(loop, QStringLiteral("again"), loop, 1);
        const int message = s.addMessage(QStringLiteral("Message"));
        s.fieldOfType(message, QStringLiteral("loop"), loop, 1);
        KiwiWriter body;
        body.writeVarUint(1); // Message.loop; the struct then reads no bytes before recursing
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(s.encode(), body.bytes)), FileError);
    }

    void anArrayOfEmptyStructsThrows()
    {
        // An empty struct takes no bytes, so a claimed length can't be checked against the data.
        KiwiSchemaBuilder s;
        const int empty = s.addStruct(QStringLiteral("Empty"));
        const int holder = s.addStruct(QStringLiteral("Holder"));
        s.fieldOfType(holder, QStringLiteral("items"), empty, 1, true);
        const int message = s.addMessage(QStringLiteral("Message"));
        s.fieldOfType(message, QStringLiteral("holders"), holder, 1, true);
        KiwiWriter body;
        body.writeVarUint(1);
        // 20,000 holders of 50,000 empty structs each: every length is under the bytes left, yet
        // together they are a billion values from about 100 KB.
        body.writeVarUint(20000);
        for (int i = 0; i < 20000; ++i)
            body.writeVarUint(50000);
        body.bytes += QByteArray(100000, '\0'); // keeps each length under the bytes left
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(s.encode(), body.bytes)), FileError);
    }

    void messagesNestedWithoutEndThrow()
    {
        KiwiSchemaBuilder s;
        const int message = s.addMessage(QStringLiteral("Message"));
        s.fieldOfType(message, QStringLiteral("inner"), message, 1);
        // One tag byte per level: a megabyte of them would be a million stack frames.
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(kiwiContainer(s.encode(), QByteArray(1 << 20, '\x01'))), FileError);
    }

    void aZstdFrameClaimingAHugeSizeThrows()
    {
        // Magic, a descriptor with an 8-byte content size, then a claimed 2^60 bytes.
        QByteArray frame = QByteArray::fromHex("28b52ffde0");
        for (int i = 0; i < 8; ++i)
            frame.append(char(i == 7 ? 0x10 : 0));
        frame.append(QByteArray(8, '\0'));
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(containerOf(frame, frame)), FileError);
    }

    void aDeflateBombPastTheLimitThrows()
    {
        // 300 MB of zeros deflates to a few hundred KB: stream it so the test itself stays small.
        z_stream stream{};
        QVERIFY(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
        const QByteArray zeros(1 << 20, '\0');
        QByteArray bomb;
        std::vector<char> out(1 << 16);
        for (int chunk = 0; chunk < 300; ++chunk) {
            stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(zeros.constData()));
            stream.avail_in = uInt(zeros.size());
            do {
                stream.next_out = reinterpret_cast<Bytef *>(out.data());
                stream.avail_out = uInt(out.size());
                deflate(&stream, chunk == 299 ? Z_FINISH : Z_NO_FLUSH);
                bomb.append(out.data(), qsizetype(out.size() - stream.avail_out));
            } while (stream.avail_out == 0);
        }
        deflateEnd(&stream);
        QVERIFY(bomb.size() < (2 << 20));
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(containerOf(deflateRaw(MinimalFig::schema()), bomb)), FileError);
    }

    void aTruncatedDeflateStreamThrows()
    {
        const QByteArray whole = kiwiContainer(MinimalFig::schema(), MinimalFig().message());
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(whole.left(whole.size() - 3)), FileError);
        QVERIFY_EXCEPTION_THROWN(FigmaImporter::parse(whole.left(20)), FileError);
    }

    void aChainOfTenThousandFramesMapsWithoutOverflowing()
    {
        MinimalFig fig;
        for (quint32 i = 1; i <= 10000; ++i)
            fig.addNode(i, i - 1, QStringLiteral("FRAME"), QStringLiteral("f%1").arg(i));
        QStringList warnings;
        const VectorDocument document = FigmaImporter::parse(fig.file(), &warnings);
        QVERIFY(named(document, QStringLiteral("f1")));
        QVERIFY(!named(document, QStringLiteral("f10000")));
        QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("nested deeper")));
    }

    void aDeepBooleanChainMapsWithoutOverflowing()
    {
        MinimalFig fig;
        for (quint32 i = 1; i <= 10000; ++i)
            fig.addNode(i, i - 1, QStringLiteral("BOOLEAN_OPERATION"));
        QStringList warnings;
        FigmaImporter::parse(fig.file(), &warnings);
        QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("nested deeper")));
    }

    void aStarWithAHugePointCountStaysSmall()
    {
        MinimalFig fig;
        fig.addNode(1, 0, QStringLiteral("STAR"), QStringLiteral("big"), 1e9f);
        fig.addNode(2, 0, QStringLiteral("REGULAR_POLYGON"), QStringLiteral("huge"), 1e30f);
        const VectorDocument document = FigmaImporter::parse(fig.file());
        for (const QString &name : {QStringLiteral("big"), QStringLiteral("huge")}) {
            const VectorObject *shape = named(document, name);
            QVERIFY(shape);
            QVERIFY(shape->path.contours.size() == 1);
            QVERIFY(shape->path.contours.front().nodes.size() <= 2000);
        }
    }

    void aVectorBlobWithHugeCountsIsLeftOut()
    {
        MinimalFig fig;
        fig.addBlob(blobHeader(0xffffffffu, 0xffffffffu, 0xffffffffu));
        fig.addBlob(blobHeader(0, 0, 1) + le32(1) + le32(1) + le32(0xffffffffu));
        fig.addNode(1, 0, QStringLiteral("VECTOR"), QStringLiteral("a"), {}, 0);
        fig.addNode(2, 0, QStringLiteral("VECTOR"), QStringLiteral("b"), {}, 1);
        QStringList warnings;
        const VectorDocument document = FigmaImporter::parse(fig.file(), &warnings);
        QVERIFY(named(document, QStringLiteral("a")));
        QVERIFY(named(document, QStringLiteral("b")));
        QVERIFY(!warnings.isEmpty());
    }

    void aRestNodeListingItselfAsAChildDoesNotLoop()
    {
        const QByteArray json = R"({"document": {"id": "0:0", "type": "DOCUMENT", "children": [
            {"id": "0:1", "type": "CANVAS", "name": "P", "children": [
                {"id": "1:1", "type": "FRAME", "name": "Loop", "size": {"x": 10, "y": 10},
                 "children": [{"id": "1:1", "type": "FRAME", "name": "Again", "size": {"x": 10, "y": 10}}]}]}]}})";
        const VectorDocument document = FigmaImporter::parseRestFile(json, QString());
        QVERIFY(named(document, QStringLiteral("Loop")));
        QVERIFY(!named(document, QStringLiteral("Again")));
    }
};

QTEST_MAIN(FigmaHostileTests)
#include "FigmaHostileTests.moc"
