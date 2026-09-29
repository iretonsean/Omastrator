#pragma once
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <QJsonObject>
#include <stdexcept>
#include <vector>

// Thrown when JSON is not an Omastrator document or clipboard.
struct CodecError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The JSON the project format and the clipboard share. Placed images travel
// as base64 PNG inside the object that shows them.
namespace DocumentCodec {
// 2: text styles by face name, tracking in 1/1000 em, leading in pt, area type.
// 3: styled runs, paragraph formats, OpenType features, character and paragraph styles.
// 4: design tokens and modes, token references, components, variants and instances.
// 5: artboards and export assets.
// 6: two or more pages (a one-page document is still written as 5, so older builds open it).
constexpr int version = 5;
constexpr int pagesVersion = 6;
constexpr const char *clipboardMimeType = "application/x-omastrator-objects";

// `pictures` off leaves a Browser View's last picture out (the clipboard, the agent's reads).
QJsonObject encode(const VectorDocument &document, bool pictures = true);
VectorDocument decode(const QJsonObject &json);

QJsonObject encode(const VectorObject &object);
VectorObject decodeObject(const QJsonObject &json);
// A Browser View's picture is written as a locked image child right after its frame, and taken back out on read.
QJsonArray encode(const std::vector<VectorObject> &objects, bool pictures = true);
std::vector<VectorObject> decodeObjects(const QJsonArray &json);

QJsonObject encode(const TextContent &text);
// Reads version 1 text too.
TextContent decodeText(const QJsonObject &json);
QJsonObject encode(const Paint &paint);
Paint decodePaint(const QJsonObject &json);
QJsonObject encode(const StrokeStyle &stroke);
StrokeStyle decodeStroke(const QJsonObject &json);
QJsonObject encode(const VectorPath &path);
VectorPath decodePath(const QJsonObject &json);
QJsonObject encode(const LiveRectangle &shape);
std::optional<LiveRectangle> decodeShape(const QJsonObject &json);
QJsonArray encode(const std::vector<Guide> &guides);
std::vector<Guide> decodeGuides(const QJsonArray &json);
}
