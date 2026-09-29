#include "UI/FigmaPasteHandler.h"
#include "Document/EditorSession.h"
#include "IO/FigmaImporter.h"
#include <QMimeData>

namespace FigmaPasteHandler {
namespace {
// The pasted content, as a flat list with parentID intact: EditorSession's own
// paste tail (renumbering ids, placing roots) expects the same shape copy()
// produces, so a full VectorDocument's layer wrapper is dropped here.
std::vector<VectorObject> flatten(const VectorDocument &document)
{
    std::vector<VectorObject> result;
    for (const QUuid &layer : document.layers()) {
        for (const QUuid &child : document.children(layer)) {
            VectorObject root = *document.find(child);
            root.parentID.reset();
            result.push_back(root);
            for (const QUuid &descendant : document.descendants(child))
                result.push_back(*document.find(descendant));
        }
    }
    return result;
}
}

void install()
{
    EditorSession::setExternalPasteHandler({
        [](const QMimeData &data) {
            if (!data.hasFormat(QStringLiteral("text/html")))
                return false;
            // Only the start: Figma's marker comes first, and a full clipboard can be megabytes.
            return FigmaImporter::isFigmaClipboardHtml(data.data(QStringLiteral("text/html")).left(16384));
        },
        [](const QMimeData &data) -> std::optional<EditorSession::ExternalPaste> {
            if (!data.hasFormat(QStringLiteral("text/html")))
                return std::nullopt;
            const QByteArray html = data.data(QStringLiteral("text/html"));
            if (!FigmaImporter::isFigmaClipboardHtml(html))
                return std::nullopt;
            try {
                EditorSession::ExternalPaste paste;
                const VectorDocument document = FigmaImporter::parseClipboardHtml(html, &paste.warnings);
                paste.objects = flatten(document);
                return paste;
            } catch (const FileError &) {
                return std::nullopt;
            }
        },
    });
}
}
