#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

// The value types a PDF file is built from: the eight object types the spec
// defines (kept flat here, with streams as dictionaries carrying raw bytes),
// plus indirect references. Names and dictionary keys travel as QString via
// Latin-1, which round-trips every byte a PDF name can hold.
namespace Pdf {

struct Reference {
    int number = 0;
    int generation = 0;
    bool operator==(const Reference &other) const = default;
};

class Object;
using Array = QList<Object>;
using Dict = QHash<QString, Object>;

enum class Type { null, boolean, integer, real, string, name, array, dictionary, stream, reference };

class Object {
public:
    Object() = default;

    static Object null() { return Object(); }
    static Object boolean(bool value);
    static Object integer(qint64 value);
    static Object real(double value);
    static Object string(QByteArray value);
    static Object name(QByteArray value);
    static Object array(Array value);
    static Object dictionary(Dict value);
    static Object stream(Dict dict, QByteArray rawBytes);
    static Object reference(int number, int generation);

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::null; }
    bool isNumber() const { return m_type == Type::integer || m_type == Type::real; }
    bool isName() const { return m_type == Type::name; }
    bool isString() const { return m_type == Type::string; }
    bool isArray() const { return m_type == Type::array; }
    bool isDictionary() const { return m_type == Type::dictionary || m_type == Type::stream; }
    bool isStream() const { return m_type == Type::stream; }
    bool isReference() const { return m_type == Type::reference; }
    // A name equal to this literal (the common "is this /Type the thing I expect" check).
    bool isName(QLatin1StringView literal) const { return m_type == Type::name && m_bytes == literal; }

    double toReal(double fallback = 0) const { return isNumber() ? m_number : fallback; }
    qint64 toInt(qint64 fallback = 0) const { return isNumber() ? qint64(m_number) : fallback; }
    bool toBool(bool fallback = false) const { return m_type == Type::boolean ? m_boolean : fallback; }
    // The string's raw bytes, already unescaped and (for hex strings) undecoded PDFDocEncoding.
    QByteArray toStringValue() const { return m_type == Type::string ? m_bytes : QByteArray(); }
    QByteArray toNameValue() const { return m_type == Type::name ? m_bytes : QByteArray(); }
    // By value: QList/QHash are ref-counted, and a value return can't dangle
    // when called on a temporary (e.g. `document.resolve(x).toArray()`),
    // unlike a reference into this Object's own (possibly temporary) storage.
    Array toArray() const;
    // For a stream, the entries of its dictionary.
    Dict toDict() const;
    Reference toReference() const { return m_type == Type::reference ? m_reference : Reference{}; }
    // A stream's bytes exactly as they sit between `stream` and `endstream`, not yet filtered.
    QByteArray rawStreamBytes() const { return m_type == Type::stream ? m_bytes : QByteArray(); }

    // Looks a key up in a dictionary (or a stream's dictionary); null Object if absent.
    Object at(const QString &key) const;

private:
    Type m_type = Type::null;
    bool m_boolean = false;
    double m_number = 0;
    QByteArray m_bytes;
    std::shared_ptr<Array> m_array;
    std::shared_ptr<Dict> m_dict;
    Reference m_reference;
};

}
