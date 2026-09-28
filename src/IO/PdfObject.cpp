#include "IO/PdfObject.h"

namespace Pdf {

Object Object::boolean(bool value)
{
    Object object;
    object.m_type = Type::boolean;
    object.m_boolean = value;
    return object;
}

Object Object::integer(qint64 value)
{
    Object object;
    object.m_type = Type::integer;
    object.m_number = double(value);
    return object;
}

Object Object::real(double value)
{
    Object object;
    object.m_type = Type::real;
    object.m_number = value;
    return object;
}

Object Object::string(QByteArray value)
{
    Object object;
    object.m_type = Type::string;
    object.m_bytes = std::move(value);
    return object;
}

Object Object::name(QByteArray value)
{
    Object object;
    object.m_type = Type::name;
    object.m_bytes = std::move(value);
    return object;
}

Object Object::array(Array value)
{
    Object object;
    object.m_type = Type::array;
    object.m_array = std::make_shared<Array>(std::move(value));
    return object;
}

Object Object::dictionary(Dict value)
{
    Object object;
    object.m_type = Type::dictionary;
    object.m_dict = std::make_shared<Dict>(std::move(value));
    return object;
}

Object Object::stream(Dict dict, QByteArray rawBytes)
{
    Object object;
    object.m_type = Type::stream;
    object.m_dict = std::make_shared<Dict>(std::move(dict));
    object.m_bytes = std::move(rawBytes);
    return object;
}

Object Object::reference(int number, int generation)
{
    Object object;
    object.m_type = Type::reference;
    object.m_reference = {number, generation};
    return object;
}

Array Object::toArray() const
{
    return m_array ? *m_array : Array();
}

Dict Object::toDict() const
{
    return m_dict ? *m_dict : Dict();
}

Object Object::at(const QString &key) const
{
    return toDict().value(key);
}

}
