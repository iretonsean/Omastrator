#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

// Figma in: paste, a figma.com link (the REST API) and .fig files, landed as
// native frames, auto layout, components and text (docs/import/figma.md).
// Kiwi decoding is FigmaKiwi.h; the tree the three sources share is built in
// FigmaImporter+Mapper.cpp.
namespace FigmaImporter {
// A whole file, or the node `nodeID` alone (as a link's ?node-id= does); nullopt reads everything.
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &data, QStringList *warnings = nullptr);
// By content: the fig-kiwi magic, or a zip whose first entry is canvas.fig.
bool canRead(const QString &path);
bool canRead(const QByteArray &data);

// Edit ▸ Paste: the clipboard's `text/html` carries `(figmeta)`/`(figma)` HTML
// comments, each base64 of a fig-kiwi blob. Images referenced by hash come back
// as placeholders (with a warning): paste carries no image bytes.
bool isFigmaClipboardHtml(const QByteArray &html);
VectorDocument parseClipboardHtml(const QByteArray &html, QStringList *warnings = nullptr);

// File ▸ Import from Figma Link…
struct LinkTarget {
    QString fileKey;
    // The URL's ?node-id=, normalised to Figma's own "1:2" form; empty imports the whole file.
    QString nodeID;
};
// Accepts figma.com/design/<key>, /file/<key> and /proto/<key>, with or without a
// trailing path or query; a /branch/<key> link gives the branch's own key.
// nullopt if the URL isn't one of those.
std::optional<LinkTarget> parseLink(const QString &url);

// The personal access token, in ~/.config/omastrator/figma.json (mode 0600).
namespace Token {
std::optional<QString> load();
// False when the token couldn't be written (unwritable folder, full disk).
bool save(const QString &token);
void forget();
QString settingsPageURL();
}

// The REST API (docs.figma.com): GET /v1/files/:key (or /v1/files/:key/nodes),
// geometry=paths for vector data, and /v1/files/:key/images for image fills.
// Thrown as FileError: 403 ("The Figma token was rejected."), 404, rate limits.
VectorDocument parseRestFile(const QByteArray &json, const QString &nodeID, QStringList *warnings = nullptr);
}
