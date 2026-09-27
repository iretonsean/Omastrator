#pragma once
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <QJsonObject>
#include <stdexcept>
#include <vector>

// Thrown when JSON is not an OmaIllustrator document or clipboard.
struct CodecError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The JSON the project format and the clipboard share. Placed images travel
// as base64 PNG inside the object that shows them.
namespace DocumentCodec {
constexpr int version = 1;
constexpr const char *clipboardMimeType = "application/x-omaillustrator-objects";

QJsonObject encode(const VectorDocument &document);
VectorDocument decode(const QJsonObject &json);

QJsonObject encode(const VectorObject &object);
VectorObject decodeObject(const QJsonObject &json);
QJsonArray encode(const std::vector<VectorObject> &objects);
std::vector<VectorObject> decodeObjects(const QJsonArray &json);

QJsonObject encode(const Paint &paint);
Paint decodePaint(const QJsonObject &json);
QJsonObject encode(const StrokeStyle &stroke);
StrokeStyle decodeStroke(const QJsonObject &json);
QJsonObject encode(const VectorPath &path);
VectorPath decodePath(const QJsonObject &json);
}
