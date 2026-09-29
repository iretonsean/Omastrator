#pragma once
#include <algorithm>
#include <QByteArray>
#include <QString>
#include <QVariant>
#include <stdexcept>
#include <vector>

// Evan Wallace's Kiwi wire format (github.com/evanw/kiwi), decoded generically
// against the schema each .fig file (or Figma paste) carries with it: nothing
// here hard-codes a field's tag number, only its name. Figma's own container
// (magic "fig-kiwi") wraps a compressed schema and a compressed message
// (deflate or zstd); see FigmaKiwi::decodeContainer.
namespace FigmaKiwi {

struct KiwiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Reads Kiwi's primitives from a byte buffer; throws KiwiError past the end.
class ByteReader {
public:
    // Also a fixed ceiling: a 256 MB inflated message could otherwise claim billions of empty values.
    explicit ByteReader(const QByteArray &bytes) : bytes(bytes), valuesLeft(std::min<qint64>(qint64(bytes.size()) * 8 + 1024, 32'000'000)) {}
    bool atEnd() const { return pos >= bytes.size(); }
    qsizetype remaining() const { return bytes.size() - pos; }
    quint8 readByte();
    QByteArray readBytes(qsizetype count);
    bool readBool() { return readByte() != 0; }
    quint32 readVarUint();
    qint32 readVarInt();
    quint64 readVarUint64();
    qint64 readVarInt64();
    float readVarFloat();
    QString readString();
    QByteArray readByteArray();
    // Zero-byte values (empty structs) cost no input, so each decoded value is charged against a budget instead.
    void spendValues(qint64 count);

private:
    QByteArray bytes;
    qsizetype pos = 0;
    qint64 valuesLeft;
};

enum class DefinitionKind { Enum, Struct, Message };

// A field's `type` is a primitive name ("bool", "byte", "int", "uint", "float",
// "string", "int64", "uint64") or another definition's name; empty for an enum
// member. `tag` is the field's wire number in a MESSAGE, or an enum member's value.
struct Field {
    QString name;
    QString type;
    bool isArray = false;
    quint32 tag = 0;
};

struct Definition {
    QString name;
    DefinitionKind kind = DefinitionKind::Struct;
    std::vector<Field> fields;
    const Field *fieldWithTag(quint32 tag) const;
};

struct Schema {
    std::vector<Definition> definitions;
    const Definition *find(const QString &name) const;
};

// The schema binary format: definitionCount, then per definition a name, a kind
// byte (0 enum, 1 struct, 2 message) and its fields (name, a varint type index —
// negative is a primitive, ~index into the eight above — isArray, and its tag).
Schema decodeSchema(const QByteArray &data);

// A struct's fields decode in declaration order; a message's carry a leading
// tag each, ending at tag 0. The result is a QVariantMap (struct/message), a
// QVariantList (an array field) or a QVariant of the field's own type; an enum
// decodes to its member name, or its raw number when the schema doesn't have it
// (a newer file using a value this schema snapshot predates).
QVariant decodeMessage(ByteReader &reader, const Schema &schema, const QString &typeName);

// Sniffs the zstd magic (28 B5 2F FD); anything else is raw deflate (no zlib
// header). Empty when the matching library wasn't linked in.
QByteArray decompress(const QByteArray &data);
bool canDecompress(const QByteArray &data);

struct Container {
    quint32 version = 0;
    Schema schema;
    // Decoded against Figma's root type, "Message".
    QVariant message;
};

// The magic "fig-kiwi" plus a version, a plain fig-kiwi file's whole content.
bool looksLikeContainer(const QByteArray &data);
Container decodeContainer(const QByteArray &data);
}
