#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/Menus.h"

namespace {
bool selectionHas(const EditorSession &session, ObjectKind kind)
{
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (object && object->kind == kind)
            return true;
    }
    return false;
}
}

// Entries follow the session's gates, as the menus are opened.
void Menus::synchronize()
{
    const EditorSession &s = session();
    const bool drawn = s.hasDocument();
    const bool free = !m_workspace.isManaging();
    const bool selected = s.hasSelection();
    // Type edited in place keeps its keys: the entries rest.
    const bool typing = m_canvas && m_canvas->isEditingText();
    const bool field = m_field != nullptr;
    // An agent's proposal waits for Enter or Esc: nothing may commit it behind the user's back.
    const bool proposal = m_agent && m_agent->hasProposalIn(s);
    for (const char *name : {"newDocument", "open", "closeDocument"})
        action(QString::fromLatin1(name))->setEnabled(free);
    for (const char *name : {"save", "saveAs", "place"})
        action(QString::fromLatin1(name))->setEnabled(free && drawn && !proposal);
    for (const char *name : {"exportPNG", "exportJPEG", "exportSVG", "exportPDF"})
        action(QString::fromLatin1(name))->setEnabled(free && drawn);
    action(QStringLiteral("exportMenu"))->setEnabled(free && drawn);
    action(QStringLiteral("undo"))->setText(!field && s.canUndo() ? QStringLiteral("Undo %1").arg(s.undoName()) : QStringLiteral("Undo"));
    action(QStringLiteral("undo"))->setEnabled(field || (!typing && !proposal && s.canUndo()));
    action(QStringLiteral("redo"))->setText(!field && s.canRedo() ? QStringLiteral("Redo %1").arg(s.redoName()) : QStringLiteral("Redo"));
    action(QStringLiteral("redo"))->setEnabled(field || (!typing && !proposal && s.canRedo()));
    action(QStringLiteral("cut"))->setEnabled(field || (!typing && !proposal && selected));
    action(QStringLiteral("copy"))->setEnabled(field || (!typing && selected));
    action(QStringLiteral("paste"))->setEnabled(field || (!typing && !proposal && drawn && s.canPaste()));
    action(QStringLiteral("pasteInPlace"))->setEnabled(!field && !typing && !proposal && drawn && s.canPaste());
    action(QStringLiteral("selectAll"))->setEnabled(field || (!typing && drawn));
    const bool editing = !field && !typing && !proposal;
    action(QStringLiteral("duplicate"))->setEnabled(editing && selected);
    action(QStringLiteral("delete"))->setEnabled(editing && (selected || !s.pickedNodes().empty()));
    action(QStringLiteral("deselect"))->setEnabled(editing && selected);
    for (const char *name : {"moveDialog", "rotateDialog", "reflectDialog", "scaleDialog", "bringToFront", "bringForward", "sendBackward",
                             "sendToBack", "lockSelection", "hideSelection", "outlineStroke", "offsetPath", "simplify", "releaseCompoundPath",
                             "releaseClippingMask"})
        action(QString::fromLatin1(name))->setEnabled(editing && selected);
    for (const char *name : {"transformMenu", "arrangeMenu", "pathMenu", "compoundMenu", "clippingMenu"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("group"))->setEnabled(editing && s.canGroup());
    action(QStringLiteral("ungroup"))->setEnabled(editing && s.canUngroup());
    action(QStringLiteral("makeCompoundPath"))->setEnabled(editing && s.canCombine());
    action(QStringLiteral("makeClippingMask"))->setEnabled(editing && s.selection().size() >= 2);
    action(QStringLiteral("unlockAll"))->setEnabled(editing && drawn);
    action(QStringLiteral("showAll"))->setEnabled(editing && drawn);
    action(QStringLiteral("artboardSize"))->setEnabled(editing && drawn);
    action(QStringLiteral("createOutlines"))->setEnabled(editing && drawn && selectionHas(s, ObjectKind::text));
    const bool agent = m_agent != nullptr;
    action(QStringLiteral("imageTraceMenu"))->setEnabled(drawn);
    action(QStringLiteral("imageTraceMake"))->setEnabled(editing && s.selectedImage().has_value());
    action(QStringLiteral("vectorizeWithAI"))->setEnabled(agent && editing && s.selectedImage().has_value());
    action(QStringLiteral("generate"))->setEnabled(agent && !typing && drawn);
    action(QStringLiteral("editWithInstruction"))->setEnabled(agent && !typing && drawn);
    action(QStringLiteral("connectAgent"))->setEnabled(agent);
    for (const char *name : {"zoomIn", "zoomOut", "fitArtboard", "actualSize", "outline", "showGrid", "snapToGrid"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("outline"))->setChecked(s.showsOutline);
    action(QStringLiteral("showGrid"))->setChecked(s.showsGrid);
    action(QStringLiteral("snapToGrid"))->setChecked(s.snapsToGrid);
    action(QStringLiteral("showLayers"))->setChecked(ContentView::showsPanel(ContentView::layersKey));
    action(QStringLiteral("showProperties"))->setChecked(ContentView::showsPanel(ContentView::propertiesKey));
}
