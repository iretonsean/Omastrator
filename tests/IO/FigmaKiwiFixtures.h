#pragma once
#include <QByteArray>
#include <QString>
#include <cstring>
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
