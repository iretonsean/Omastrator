#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QString>

// The native `.omai` document: DocumentCodec's JSON, written atomically.
namespace ProjectStore {
inline constexpr const char *extension = "omai";
inline constexpr const char *mimeType = "application/x-omaillustrator";
// Throws FileError.
VectorDocument read(const QString &path);
void write(const VectorDocument &document, const QString &path);
}
