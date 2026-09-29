#include "IO/FigmaKiwi.h"
#include "Logging.h"
#include <QtEndian>
#include <cstring>

#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif
#ifdef OMASTRATOR_HAVE_ZSTD
#include <zstd.h>
#endif

namespace FigmaKiwi {

quint8 ByteReader::readByte()
{
    if (pos >= bytes.size())
        throw KiwiError("Kiwi data ended unexpectedly.");
    return quint8(bytes[pos++]);
}

QByteArray ByteReader::readBytes(qsizetype count)
{
    if (count < 0 || pos + count > bytes.size())
        throw KiwiError("Kiwi data ended unexpectedly.");
    QByteArray result = bytes.mid(pos, count);
    pos += count;
    return result;
}

quint32 ByteReader::readVarUint()
{
    quint32 value = 0;
    int shift = 0;
    quint8 byte;
    do {
        byte = readByte();
        if (shift < 32)
            value |= quint32(byte & 0x7f) << shift;
        shift += 7;
    } while ((byte & 0x80) && shift < 35);
    return value;
}

qint32 ByteReader::readVarInt()
{
    const quint32 value = readVarUint();
    return (value & 1) ? ~qint32(value >> 1) : qint32(value >> 1);
}

quint64 ByteReader::readVarUint64()
{
    quint64 value = 0;
    quint64 shift = 0;
    quint8 byte;
    while (true) {
        byte = readByte();
        if ((byte & 0x80) && shift < 56) {
            value |= quint64(byte & 0x7f) << shift;
            shift += 7;
            continue;
        }
        break;
    }
    // The last byte (the 9th, when every earlier one set its continuation bit)
    // contributes whole, unmasked: 8*7 + 8 = 64 bits.
    value |= quint64(byte) << shift;
    return value;
}

qint64 ByteReader::readVarInt64()
{
    const quint64 value = readVarUint64();
    return (value & 1) ? qint64(~(value >> 1)) : qint64(value >> 1);
}

float ByteReader::readVarFloat()
{
    const quint8 first = readByte();
    if (first == 0)
        return 0.0f;
    const quint8 b1 = readByte(), b2 = readByte(), b3 = readByte();
    quint32 bits = quint32(first) | (quint32(b1) << 8) | (quint32(b2) << 16) | (quint32(b3) << 24);
    // The exponent was moved to the front so a zero mantissa's low byte is 0; put it back.
    bits = (bits << 23) | (bits >> 9);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

QString ByteReader::readString()
{
    const qsizetype start = pos;
    while (readByte() != 0) { }
    // A NUL can't occur inside a UTF-8 sequence, so the byte just consumed is the terminator.
    return QString::fromUtf8(bytes.constData() + start, int(pos - start - 1));
}

void ByteReader::spendValues(qint64 count)
{
    valuesLeft -= count;
    if (valuesLeft < 0)
        throw KiwiError("Kiwi data holds far more values than its size allows.");
}

QByteArray ByteReader::readByteArray()
{
    const quint32 length = readVarUint();
    return readBytes(length);
}

const Field *Definition::fieldWithTag(quint32 tag) const
{
    for (const Field &field : fields) {
        if (field.tag == tag)
            return &field;
    }
    return nullptr;
}

const Definition *Schema::find(const QString &name) const
{
    for (const Definition &definition : definitions) {
        if (definition.name == name)
            return &definition;
    }
    return nullptr;
}

namespace {
// Nesting deeper than any real Figma message; a hostile schema can recurse without reading a byte.
constexpr int maximumDepth = 64;
// Decompressed output above this is a decompression bomb, not a design file.
constexpr qsizetype maximumDecompressed = qsizetype(256) << 20;
constexpr const char *primitiveTypes[] = {"bool", "byte", "int", "uint", "float", "string", "int64", "uint64"};
constexpr int primitiveCount = 8;
}

Schema decodeSchema(const QByteArray &data)
{
    ByteReader reader(data);
    const quint32 count = reader.readVarUint();
    // A definition takes at least a name terminator, a kind and a field count.
    if (qsizetype(count) > reader.remaining() / 3)
        throw KiwiError("Kiwi schema claims more definitions than it holds.");
    Schema schema;
    schema.definitions.resize(count);
    std::vector<std::vector<qint32>> rawTypes(count);
    for (quint32 i = 0; i < count; ++i) {
        Definition &definition = schema.definitions[i];
        definition.name = reader.readString();
        const quint8 kind = reader.readByte();
        if (kind > 2)
            throw KiwiError("Unknown Kiwi definition kind.");
        definition.kind = DefinitionKind(kind);
        const quint32 fieldCount = reader.readVarUint();
        // A field takes at least a name terminator, a type, an array flag and a tag.
        if (qsizetype(fieldCount) > reader.remaining() / 4)
            throw KiwiError("Kiwi definition claims more fields than it holds.");
        definition.fields.resize(fieldCount);
        rawTypes[i].resize(fieldCount);
        for (quint32 j = 0; j < fieldCount; ++j) {
            Field &field = definition.fields[j];
            field.name = reader.readString();
            rawTypes[i][j] = reader.readVarInt();
            field.isArray = (reader.readByte() & 1) != 0;
            field.tag = reader.readVarUint();
        }
    }
    // Bind each field's type name once every definition's own name is known.
    for (quint32 i = 0; i < count; ++i) {
        Definition &definition = schema.definitions[i];
        if (definition.kind == DefinitionKind::Enum)
            continue;
        for (quint32 j = 0; j < definition.fields.size(); ++j) {
            const qint32 type = rawTypes[i][j];
            if (type < 0) {
                const qint32 index = ~type;
                if (index < 0 || index >= primitiveCount)
                    throw KiwiError("Unknown Kiwi primitive type.");
                definition.fields[j].type = QString::fromLatin1(primitiveTypes[index]);
            } else {
                if (quint32(type) >= count)
                    throw KiwiError("Kiwi field refers to an unknown type.");
                definition.fields[j].type = schema.definitions[size_t(type)].name;
            }
        }
    }
    return schema;
}

namespace {
QVariant decodeField(ByteReader &reader, const Schema &schema, const QString &typeName, int depth);

// Arrays are a varuint length then that many elements, except a byte array,
// whose bytes come back as one QByteArray rather than a list of numbers.
QVariant decodeArray(ByteReader &reader, const Schema &schema, const QString &typeName, int depth)
{
    const quint32 length = reader.readVarUint();
    if (typeName == QLatin1String("byte"))
        return reader.readBytes(length);
    // Every element takes at least a byte except a struct with no fields; the value budget covers that one.
    const Definition *element = schema.find(typeName);
    const bool costsNothing = element && element->kind == DefinitionKind::Struct && element->fields.empty();
    if (qsizetype(length) > reader.remaining() && !costsNothing)
        throw KiwiError("Kiwi array is longer than the data.");
    reader.spendValues(length);
    QVariantList list;
    list.reserve(qsizetype(std::min<quint32>(length, quint32(reader.remaining()))));
    for (quint32 i = 0; i < length; ++i)
        list.append(decodeField(reader, schema, typeName, depth));
    return list;
}

QVariant decodeOne(ByteReader &reader, const Schema &schema, const Definition &definition, int depth)
{
    if (depth > maximumDepth)
        throw KiwiError("Kiwi data is nested too deeply.");
    if (definition.kind == DefinitionKind::Struct) {
        reader.spendValues(qint64(definition.fields.size()) + 1);
        QVariantMap result;
        for (const Field &field : definition.fields)
            result.insert(field.name, field.isArray ? decodeArray(reader, schema, field.type, depth + 1) : decodeField(reader, schema, field.type, depth + 1));
        return result;
    }
    // Message: a tag, then that field's value, until a 0 tag closes it.
    QVariantMap result;
    while (true) {
        const quint32 tag = reader.readVarUint();
        if (tag == 0)
            break;
        const Field *field = definition.fieldWithTag(tag);
        if (!field)
            throw KiwiError(QStringLiteral("Message \"%1\" has no field tagged %2.").arg(definition.name).arg(tag).toStdString());
        result.insert(field->name, field->isArray ? decodeArray(reader, schema, field->type, depth + 1) : decodeField(reader, schema, field->type, depth + 1));
    }
    return result;
}

QVariant decodeField(ByteReader &reader, const Schema &schema, const QString &typeName, int depth)
{
    if (typeName == QLatin1String("bool"))
        return reader.readBool();
    if (typeName == QLatin1String("byte"))
        return quint32(reader.readByte());
    if (typeName == QLatin1String("int"))
        return reader.readVarInt();
    if (typeName == QLatin1String("uint"))
        return reader.readVarUint();
    if (typeName == QLatin1String("float"))
        return double(reader.readVarFloat());
    if (typeName == QLatin1String("string"))
        return reader.readString();
    if (typeName == QLatin1String("int64"))
        return qlonglong(reader.readVarInt64());
    if (typeName == QLatin1String("uint64"))
        return qulonglong(reader.readVarUint64());
    const Definition *inner = schema.find(typeName);
    if (!inner)
        throw KiwiError(QStringLiteral("Unknown Kiwi type \"%1\".").arg(typeName).toStdString());
    if (inner->kind == DefinitionKind::Enum) {
        const quint32 value = reader.readVarUint();
        if (const Field *member = inner->fieldWithTag(value))
            return member->name;
        return value;
    }
    return decodeOne(reader, schema, *inner, depth);
}
}

QVariant decodeMessage(ByteReader &reader, const Schema &schema, const QString &typeName)
{
    const Definition *root = schema.find(typeName);
    if (!root)
        throw KiwiError(QStringLiteral("Kiwi schema has no \"%1\" type.").arg(typeName).toStdString());
    return decodeOne(reader, schema, *root, 0);
}

bool canDecompress(const QByteArray &data)
{
    static const QByteArray zstdMagic = QByteArray::fromHex("28b52ffd");
#ifdef OMASTRATOR_HAVE_ZSTD
    if (data.startsWith(zstdMagic))
        return true;
#else
    if (data.startsWith(zstdMagic))
        return false;
#endif
#ifdef OMASTRATOR_HAVE_ZLIB
    return true;
#else
    return false;
#endif
}

QByteArray decompress(const QByteArray &data)
{
    static const QByteArray zstdMagic = QByteArray::fromHex("28b52ffd");
    if (data.startsWith(zstdMagic)) {
#ifdef OMASTRATOR_HAVE_ZSTD
        const unsigned long long size = ZSTD_getFrameContentSize(data.constData(), size_t(data.size()));
        if (size != ZSTD_CONTENTSIZE_UNKNOWN && size != ZSTD_CONTENTSIZE_ERROR) {
            if (size > (unsigned long long)maximumDecompressed) {
                qCWarning(lcIO) << "Figma data claims to decompress past the size limit";
                return {};
            }
            QByteArray out(qsizetype(size), Qt::Uninitialized);
            const size_t result = ZSTD_decompress(out.data(), size_t(out.size()), data.constData(), size_t(data.size()));
            if (ZSTD_isError(result))
                return {};
            out.resize(qsizetype(result));
            return out;
        }
        // No content size in the frame: decompress in a stream, growing the buffer.
        ZSTD_DStream *stream = ZSTD_createDStream();
        if (!stream)
            return {};
        ZSTD_initDStream(stream);
        ZSTD_inBuffer in{data.constData(), size_t(data.size()), 0};
        QByteArray out;
        std::vector<char> chunk(ZSTD_DStreamOutSize());
        size_t code = 0;
        do {
            ZSTD_outBuffer outBuf{chunk.data(), chunk.size(), 0};
            code = ZSTD_decompressStream(stream, &outBuf, &in);
            if (ZSTD_isError(code)) {
                ZSTD_freeDStream(stream);
                return {};
            }
            out.append(chunk.data(), qsizetype(outBuf.pos));
            if (out.size() > maximumDecompressed) {
                ZSTD_freeDStream(stream);
                qCWarning(lcIO) << "Figma data decompressed past the size limit";
                return {};
            }
        } while (in.pos < in.size && code != 0);
        ZSTD_freeDStream(stream);
        return out;
#else
        qCWarning(lcIO) << "Figma data is zstd-compressed, but this build has no zstd";
        return {};
#endif
    }
#ifdef OMASTRATOR_HAVE_ZLIB
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK)
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    QByteArray out;
    std::vector<char> chunk(64 * 1024);
    int result = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = uInt(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }
        out.append(chunk.data(), qsizetype(chunk.size() - stream.avail_out));
        if (out.size() > maximumDecompressed) {
            inflateEnd(&stream);
            qCWarning(lcIO) << "Figma data decompressed past the size limit";
            return {};
        }
        // Truncated input: inflate makes no progress and returns Z_OK forever otherwise.
        if (result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) {
            inflateEnd(&stream);
            return {};
        }
    } while (result != Z_STREAM_END);
    inflateEnd(&stream);
    return out;
#else
    qCWarning(lcIO) << "Figma data is deflate-compressed, but this build has no zlib";
    return {};
#endif
}

bool looksLikeContainer(const QByteArray &data)
{
    return data.size() >= 12 && data.startsWith("fig-kiwi");
}

Container decodeContainer(const QByteArray &data)
{
    if (!looksLikeContainer(data))
        throw KiwiError("Not a fig-kiwi file.");
    Container container;
    container.version = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + 8));
    qsizetype at = 12;
    const auto readChunk = [&]() -> QByteArray {
        if (at + 4 > data.size())
            throw KiwiError("Truncated fig-kiwi file.");
        const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + at));
        at += 4;
        if (at + qsizetype(size) > data.size())
            throw KiwiError("Truncated fig-kiwi file.");
        const QByteArray compressed = data.mid(at, qsizetype(size));
        at += qsizetype(size);
        const QByteArray raw = decompress(compressed);
        if (raw.isEmpty() && size > 0)
            throw KiwiError("Couldn't decompress fig-kiwi data.");
        return raw;
    };
    const QByteArray schemaData = readChunk();
    container.schema = decodeSchema(schemaData);
    const QByteArray messageData = readChunk();
    ByteReader messageReader(messageData);
    container.message = decodeMessage(messageReader, container.schema, QStringLiteral("Message"));
    return container;
}
}
