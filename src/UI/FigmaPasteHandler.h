#pragma once

// Registers Figma's paste (docs/import/figma.md) with EditorSession, once, at
// startup. Lives in oma_ui because it needs both EditorSession (oma_core) and
// FigmaImporter (oma_io), which don't depend on each other.
namespace FigmaPasteHandler {
void install();
}
