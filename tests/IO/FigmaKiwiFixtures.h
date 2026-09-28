#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstring>
#include <optional>
#include <vector>
#include <zlib.h>

// A tiny Kiwi encoder mirroring evanw/kiwi's wire format (the inverse of
// FigmaKiwi::ByteReader), for building test fixtures without a real Figma
// file (FIGMA.md: "Build Kiwi fixtures with a small in-test encoder and a
// schema you write to mirror Figma's field names").
class KiwiWriter {
public:
    QByteArray bytes;

    void writeByte(quint8 b) { bytes.append(char(b)); }
    void writeBool(bool b) { writeByte(b ? 1 : 0); }

    void writeVarUint(quint32 value)
    {
        do {
            const quint8 low = value & 0x7f;
            value >>= 7;
            writeByte(value ? (low | 0x80) : low);
        } while (value);
    }

    void writeVarInt(qint32 value) { writeVarUint((quint32(value) << 1) ^ quint32(value >> 31)); }

    void writeVarFloat(float value)
    {
        quint32 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const quint32 rotated = (bits >> 23) | (bits << 9);
        if ((rotated & 0xff) == 0) {
            writeByte(0);
            return;
        }
        writeByte(rotated & 0xff);
        writeByte((rotated >> 8) & 0xff);
        writeByte((rotated >> 16) & 0xff);
        writeByte((rotated >> 24) & 0xff);
    }

    void writeString(const QString &s)
    {
        bytes.append(s.toUtf8());
        writeByte(0);
    }

    void writeByteArray(const QByteArray &data)
    {
        writeVarUint(quint32(data.size()));
        bytes.append(data);
    }
};

// A field in a schema definition being built.
struct KiwiFieldDef {
    QString name;
    // ~index into {bool,byte,int,uint,float,string,int64,uint64} for a primitive,
    // else another definition's index.
    qint32 type = 0;
    bool isArray = false;
    quint32 tag = 0;
};

struct KiwiDefDef {
    QString name;
    quint8 kind = 1; // 0 enum, 1 struct, 2 message
    std::vector<KiwiFieldDef> fields;
};

// Builds a schema definition by definition, tracking each one's index so later
// fields can reference an earlier struct by name.
class KiwiSchemaBuilder {
public:
    int addStruct(const QString &name) { return add(name, 1); }
    int addMessage(const QString &name) { return add(name, 2); }

    void field(int defIndex, const QString &name, const QString &primitive, quint32 tag, bool isArray = false)
    {
        static const QStringList primitives{QStringLiteral("bool"), QStringLiteral("byte"),   QStringLiteral("int"),   QStringLiteral("uint"),
                                             QStringLiteral("float"), QStringLiteral("string"), QStringLiteral("int64"), QStringLiteral("uint64")};
        const int index = primitives.indexOf(primitive);
        Q_ASSERT(index >= 0);
        defs[defIndex].fields.push_back({name, qint32(~index), isArray, tag});
    }

    void fieldOfType(int defIndex, const QString &name, int typeDefIndex, quint32 tag, bool isArray = false)
    {
        defs[defIndex].fields.push_back({name, qint32(typeDefIndex), isArray, tag});
    }

    QByteArray encode() const
    {
        KiwiWriter w;
        w.writeVarUint(quint32(defs.size()));
        for (const KiwiDefDef &d : defs) {
            w.writeString(d.name);
            w.writeByte(d.kind);
            w.writeVarUint(quint32(d.fields.size()));
            for (const KiwiFieldDef &f : d.fields) {
                w.writeString(f.name);
                w.writeVarInt(f.type);
                w.writeByte(f.isArray ? 1 : 0);
                w.writeVarUint(f.tag);
            }
        }
        return w.bytes;
    }

private:
    std::vector<KiwiDefDef> defs;

    int add(const QString &name, quint8 kind)
    {
        defs.push_back({name, kind, {}});
        return int(defs.size()) - 1;
    }
};

// Raw deflate (no zlib header), matching what FigmaKiwi::decompress expects.
inline QByteArray deflateRaw(const QByteArray &data)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out(int(data.size() + 64), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(int(out.size() - stream.avail_out));
    deflateEnd(&stream);
    return out;
}

// A minimal zip archive: stored or raw-deflated entries, no CRC (ZipReader
// doesn't check it), no zip64.
struct ZipEntry {
    QString name;
    QByteArray data;
    bool deflate = false;
};

inline QByteArray buildZipArchive(const std::vector<ZipEntry> &entries)
{
    const auto u16 = [](QByteArray &out, quint16 v) {
        out.append(char(v & 0xff));
        out.append(char((v >> 8) & 0xff));
    };
    const auto u32 = [](QByteArray &out, quint32 v) {
        out.append(char(v & 0xff));
        out.append(char((v >> 8) & 0xff));
        out.append(char((v >> 16) & 0xff));
        out.append(char((v >> 24) & 0xff));
    };
    QByteArray data;
    struct Written {
        QString name;
        quint32 offset, compressedSize, uncompressedSize;
        quint16 method;
    };
    std::vector<Written> written;
    for (const ZipEntry &entry : entries) {
        const QByteArray compressed = entry.deflate ? deflateRaw(entry.data) : entry.data;
        const quint16 method = entry.deflate ? 8 : 0;
        const quint32 offset = quint32(data.size());
        u32(data, 0x04034b50);
        u16(data, 20);
        u16(data, 0);
        u16(data, method);
        u16(data, 0);
        u16(data, 0);
        u32(data, 0);
        u32(data, quint32(compressed.size()));
        u32(data, quint32(entry.data.size()));
        const QByteArray name = entry.name.toUtf8();
        u16(data, quint16(name.size()));
        u16(data, 0);
        data += name;
        data += compressed;
        written.push_back({entry.name, offset, quint32(compressed.size()), quint32(entry.data.size()), method});
    }
    const quint32 centralStart = quint32(data.size());
    for (const Written &w : written) {
        u32(data, 0x02014b50);
        u16(data, 20);
        u16(data, 20);
        u16(data, 0);
        u16(data, w.method);
        u16(data, 0);
        u16(data, 0);
        u32(data, 0);
        u32(data, w.compressedSize);
        u32(data, w.uncompressedSize);
        const QByteArray name = w.name.toUtf8();
        u16(data, quint16(name.size()));
        u16(data, 0);
        u16(data, 0);
        u16(data, 0);
        u16(data, 0);
        u32(data, 0);
        u32(data, w.offset);
        data += name;
    }
    const quint32 centralSize = quint32(data.size()) - centralStart;
    u32(data, 0x06054b50);
    u16(data, 0);
    u16(data, 0);
    u16(data, quint16(written.size()));
    u16(data, quint16(written.size()));
    u32(data, centralSize);
    u32(data, centralStart);
    u16(data, 0);
    return data;
}

// The fig-kiwi container: magic, version, then the schema and message, each
// length-prefixed and deflate-compressed.
inline QByteArray kiwiContainer(const QByteArray &schema, const QByteArray &message, quint32 version = 42)
{
    QByteArray result = "fig-kiwi";
    const auto le32 = [](quint32 v) {
        QByteArray b(4, Qt::Uninitialized);
        b[0] = char(v & 0xff);
        b[1] = char((v >> 8) & 0xff);
        b[2] = char((v >> 16) & 0xff);
        b[3] = char((v >> 24) & 0xff);
        return b;
    };
    result += le32(version);
    const QByteArray compressedSchema = deflateRaw(schema);
    result += le32(quint32(compressedSchema.size())) + compressedSchema;
    const QByteArray compressedMessage = deflateRaw(message);
    result += le32(quint32(compressedMessage.size())) + compressedMessage;
    return result;
}

// The smallest .fig that maps: a schema of Guid, ParentIndex, Vec2, VectorData,
// Blob, Node and Message, and a message built from addNode() calls. Hostile
// tests use it to make odd trees and blobs without the big fixture.
class MinimalFig {
public:
    MinimalFig()
    {
        writeNode(1, 0, QStringLiteral("CANVAS"), QStringLiteral("Page 1"));
    }

    // Node `local` under `parent` (0 = the page), of `type`; `count` becomes the STAR/polygon
    // point count when set, `blob` the vector network it points at.
    void addNode(quint32 local, quint32 parent, const QString &type, const QString &name = QStringLiteral("n"), std::optional<float> count = {},
                 std::optional<qint32> blob = {})
    {
        writeNode(local + 1, parent == 0 ? 1 : parent + 1, type, name, count, blob);
    }

    void addBlob(const QByteArray &bytes) { blobs.push_back(bytes); }

    QByteArray file() const { return kiwiContainer(schema(), message()); }

    QByteArray message() const
    {
        KiwiWriter root;
        root.writeVarUint(1);
        root.writeVarUint(quint32(nodeCount));
        root.bytes += nodes;
        if (!blobs.empty()) {
            root.writeVarUint(2);
            root.writeVarUint(quint32(blobs.size()));
            for (const QByteArray &blob : blobs) {
                KiwiWriter one;
                one.writeByteArray(blob);
                root.bytes += one.bytes;
            }
        }
        root.writeVarUint(0);
        return root.bytes;
    }

    static QByteArray schema()
    {
        KiwiSchemaBuilder s;
        const int vec2 = s.addStruct(QStringLiteral("Vec2"));
        s.field(vec2, QStringLiteral("x"), QStringLiteral("float"), 1);
        s.field(vec2, QStringLiteral("y"), QStringLiteral("float"), 2);
        const int guid = s.addStruct(QStringLiteral("GUID"));
        s.field(guid, QStringLiteral("sessionID"), QStringLiteral("uint"), 1);
        s.field(guid, QStringLiteral("localID"), QStringLiteral("uint"), 2);
        const int parent = s.addStruct(QStringLiteral("ParentIndex"));
        s.fieldOfType(parent, QStringLiteral("guid"), guid, 1);
        s.field(parent, QStringLiteral("position"), QStringLiteral("string"), 2);
        const int vectorData = s.addStruct(QStringLiteral("VectorData"));
        s.field(vectorData, QStringLiteral("vectorNetworkBlob"), QStringLiteral("int"), 1);
        s.fieldOfType(vectorData, QStringLiteral("normalizedSize"), vec2, 2);
        const int blob = s.addStruct(QStringLiteral("Blob"));
        s.field(blob, QStringLiteral("bytes"), QStringLiteral("byte"), 1, true);
        const int node = s.addMessage(QStringLiteral("Node"));
        s.fieldOfType(node, QStringLiteral("guid"), guid, 1);
        s.fieldOfType(node, QStringLiteral("parentIndex"), parent, 2);
        s.field(node, QStringLiteral("type"), QStringLiteral("string"), 3);
        s.field(node, QStringLiteral("name"), QStringLiteral("string"), 4);
        s.fieldOfType(node, QStringLiteral("size"), vec2, 7);
        s.fieldOfType(node, QStringLiteral("vectorData"), vectorData, 13);
        s.field(node, QStringLiteral("count"), QStringLiteral("float"), 16);
        const int message = s.addMessage(QStringLiteral("Message"));
        s.fieldOfType(message, QStringLiteral("nodeChanges"), node, 1, true);
        s.fieldOfType(message, QStringLiteral("blobs"), blob, 2, true);
        return s.encode();
    }

private:
    QByteArray nodes;
    int nodeCount = 0;
    std::vector<QByteArray> blobs;

    // `local` and `parentLocal` are the guids' localIDs; parentLocal 0 has no parent.
    void writeNode(quint32 local, quint32 parentLocal, const QString &type, const QString &name, std::optional<float> count = {},
                   std::optional<qint32> blob = {})
    {
        KiwiWriter w;
        w.writeVarUint(1);
        w.writeVarUint(1);
        w.writeVarUint(local);
        if (parentLocal != 0) {
            w.writeVarUint(2);
            w.writeVarUint(1);
            w.writeVarUint(parentLocal);
            w.writeString(QStringLiteral("a"));
        }
        w.writeVarUint(3);
        w.writeString(type);
        w.writeVarUint(4);
        w.writeString(name);
        w.writeVarUint(7);
        w.writeVarFloat(40);
        w.writeVarFloat(40);
        if (blob) {
            w.writeVarUint(13);
            w.writeVarInt(*blob);
            w.writeVarFloat(1);
            w.writeVarFloat(1);
        }
        if (count) {
            w.writeVarUint(16);
            w.writeVarFloat(*count);
        }
        w.writeVarUint(0);
        nodes += w.bytes;
        ++nodeCount;
    }
};
