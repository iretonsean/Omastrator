#include "UI/FigmaLinkSheet.h"
#include "UI/ProjectWorkspace.h"

// File ▸ Import from Figma Link…: the sheet fetches and maps the file itself
// (docs/import/figma.md); this just lands the result as a new tab.
void ProjectWorkspace::importFigmaLink()
{
    auto *sheet = new FigmaLinkSheet(window);
    sheet->onImported = [this](VectorDocument document, QString title, QStringList warnings) { openDocument(std::move(document), title, warnings); };
    sheet->open();
}
