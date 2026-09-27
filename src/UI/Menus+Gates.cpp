#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/Menus.h"
#include "UI/ShareController.h"
#include "Canvas/TaskBar.h"

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
    const bool site = m_share && !m_share->liveProject().isEmpty();
    for (const char *name : {"shareWithClient", "shareOptions"})
        action(QString::fromLatin1(name))->setEnabled(m_share && free && !m_share->running() && (drawn || site));
    action(QStringLiteral("sharedLinks"))->setEnabled(m_share != nullptr);
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
    bool hasPoint = false, hasArea = false;
    for (const QUuid &id : s.selectedTexts()) {
        const bool area = s.document()->find(id)->text.area.has_value();
        hasPoint = hasPoint || !area;
        hasArea = hasArea || area;
    }
    action(QStringLiteral("convertToAreaType"))->setEnabled(editing && hasPoint);
    action(QStringLiteral("convertToPointType"))->setEnabled(editing && hasArea);
    // The type keys work on selected type, or while typing; otherwise Alt+arrows fall through to duplicate and nudge.
    const std::vector<QUuid> leaves = s.selectedLeaves();
    const bool allText = drawn && !leaves.empty() && s.selectedTexts().size() == leaves.size();
    const bool typeKeys = !field && !proposal && drawn && (typing || allText);
    for (const char *name : {"increaseFontSize", "decreaseFontSize", "tightenTracking", "loosenTracking", "tightenTrackingMore",
                             "loosenTrackingMore", "resetTracking", "decreaseLeading", "increaseLeading", "raiseBaseline", "lowerBaseline"})
        action(QString::fromLatin1(name))->setEnabled(typeKeys);
    for (const char *name : {"typeSizeMenu", "typeTrackingMenu", "typeLeadingMenu", "typeBaselineMenu"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
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
    for (const char *name : {"pasteInFront", "pasteInBack"})
        action(QString::fromLatin1(name))->setEnabled(!field && !typing && !proposal && drawn && s.canPaste());
    action(QStringLiteral("transformAgain"))->setEnabled(editing && s.canTransformAgain());
    for (const char *name : {"flipHorizontal", "flipVertical", "nextObjectAbove", "nextObjectBelow", "selectSameFillAndStroke", "selectSameFillColor",
                             "selectSameOpacity", "selectSameStrokeColor", "selectSameStrokeWeight", "selectSameBlendMode", "selectSameLayers"})
        action(QString::fromLatin1(name))->setEnabled(editing && selected);
    for (const char *name : {"selectSameFontFamily", "selectSameFontFamilyStyleSize"})
        action(QString::fromLatin1(name))->setEnabled(editing && selectionHas(s, ObjectKind::text));
    for (const char *name : {"selectInverse", "selectClippingMasks", "selectStrayPoints", "selectTextObjects", "selectImages", "selectOpenPaths"})
        action(QString::fromLatin1(name))->setEnabled(editing && drawn);
    for (const char *name : {"selectSameMenu", "selectObjectMenu"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("reselect"))->setEnabled(editing && drawn && s.canReselect());
    action(QStringLiteral("zoomToSelection"))->setEnabled(drawn && selected && !typing);
    action(QStringLiteral("join"))->setEnabled(editing && s.canJoin());
    action(QStringLiteral("average"))->setEnabled(editing && s.canAverage());
    action(QStringLiteral("reversePathDirection"))->setEnabled(editing && selectionHas(s, ObjectKind::path));
    const std::vector<QUuid> compound = drawn ? s.selectedCompoundPaths() : std::vector<QUuid>();
    action(QStringLiteral("evenOddFillRule"))->setEnabled(editing && !compound.empty());
    action(QStringLiteral("evenOddFillRule"))->setChecked(!compound.empty() && s.document()->find(compound.front())->path.fillRule == Qt::OddEvenFill);
    for (const char *name : {"snapToPixel", "pixelGrid", "rulers", "guidesMenu", "hideGuides", "lockGuides"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("snapToPixel"))->setChecked(s.snapsToPixel);
    action(QStringLiteral("pixelGrid"))->setChecked(s.showsPixelGrid);
    action(QStringLiteral("rulers"))->setChecked(s.showsRulers);
    action(QStringLiteral("hideGuides"))->setText(s.showsGuides ? QStringLiteral("Hide Guides") : QStringLiteral("Show Guides"));
    action(QStringLiteral("lockGuides"))->setChecked(s.guidesLocked);
    action(QStringLiteral("makeGuides"))->setEnabled(editing && drawn && s.canMakeGuides());
    const bool guides = drawn && !s.document()->guides.empty();
    action(QStringLiteral("releaseGuides"))->setEnabled(editing && guides);
    action(QStringLiteral("clearGuides"))->setEnabled(editing && guides);
    action(QStringLiteral("showHistory"))->setEnabled(drawn);
    action(QStringLiteral("contextualTaskBar"))->setChecked(TaskBar::isTurnedOn());
    action(QStringLiteral("showLayers"))->setChecked(ContentView::showsPanel(ContentView::layersKey));
    action(QStringLiteral("showProperties"))->setChecked(ContentView::showsPanel(ContentView::propertiesKey));
}
