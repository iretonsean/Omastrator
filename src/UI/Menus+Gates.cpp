#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/LiveFrames.h"
#include "UI/Menus.h"
#include "UI/PageWorkspaces.h"
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
    const bool locked = s.isDocumentLocked();
    for (const char *name : {"save", "saveAs"})
        action(QString::fromLatin1(name))->setEnabled(free && drawn && !proposal);
    action(QStringLiteral("place"))->setEnabled(free && drawn && !proposal && !locked);
    action(QStringLiteral("lockDocument"))->setEnabled(free && drawn && !proposal && !typing);
    action(QStringLiteral("lockDocument"))->setText(locked ? QStringLiteral("Unlock Document") : QStringLiteral("Lock Document"));
    for (const char *name : {"exportPNG", "exportJPEG", "exportSVG", "exportPDF"})
        action(QString::fromLatin1(name))->setEnabled(free && drawn);
    action(QStringLiteral("exportMenu"))->setEnabled(free && drawn);
    const bool site = m_share && !m_share->liveProject().isEmpty();
    for (const char *name : {"shareWithClient", "shareOptions"})
        action(QString::fromLatin1(name))->setEnabled(m_share && free && !m_share->running() && (drawn || site));
    action(QStringLiteral("sendToDevice"))->setEnabled(m_share && free && !m_share->running() && drawn);
    action(QStringLiteral("sharedLinks"))->setEnabled(m_share != nullptr);
    // In Edit Page, Ctrl+Z is the page's own history and never the document's.
    const bool pageHistory = !field && m_canvas && m_canvas->editPageFrame();
    if (pageHistory) {
        action(QStringLiteral("undo"))->setText(QStringLiteral("Undo Page Edit"));
        action(QStringLiteral("undo"))->setEnabled(m_canvas->canUndoPageEdit());
        action(QStringLiteral("redo"))->setText(QStringLiteral("Redo Page Edit"));
        action(QStringLiteral("redo"))->setEnabled(m_canvas->canRedoPageEdit());
    } else {
        action(QStringLiteral("undo"))->setText(!field && s.canUndo() ? QStringLiteral("Undo %1").arg(s.undoName()) : QStringLiteral("Undo"));
        action(QStringLiteral("undo"))->setEnabled(field || (!typing && !proposal && !locked && s.canUndo()));
        action(QStringLiteral("redo"))->setText(!field && s.canRedo() ? QStringLiteral("Redo %1").arg(s.redoName()) : QStringLiteral("Redo"));
        action(QStringLiteral("redo"))->setEnabled(field || (!typing && !proposal && !locked && s.canRedo()));
    }
    action(QStringLiteral("cut"))->setEnabled(field || (!typing && !proposal && !locked && selected));
    action(QStringLiteral("copy"))->setEnabled(field || (!typing && selected));
    action(QStringLiteral("paste"))->setEnabled(field || (!typing && !proposal && !locked && drawn && s.canPaste()));
    action(QStringLiteral("pasteInPlace"))->setEnabled(!field && !typing && !proposal && !locked && drawn && s.canPaste());
    action(QStringLiteral("selectAll"))->setEnabled(field || (!typing && drawn));
    const bool editing = !field && !typing && !proposal && !locked;
    action(QStringLiteral("duplicate"))->setEnabled(editing && selected);
    action(QStringLiteral("delete"))->setEnabled(editing && (selected || !s.pickedNodes().empty()));
    action(QStringLiteral("deselect"))->setEnabled(editing && selected);
    for (const char *name : {"moveDialog", "rotateDialog", "reflectDialog", "scaleDialog", "bringToFront", "bringForward", "sendBackward",
                             "sendToBack", "lockSelection", "hideSelection", "outlineStroke", "offsetPath", "simplify", "releaseCompoundPath",
                             "releaseClippingMask"})
        action(QString::fromLatin1(name))->setEnabled(editing && selected);
    for (const char *name : {"transformMenu", "arrangeMenu", "pathMenu", "compoundMenu", "clippingMenu", "opacityMaskMenu"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    action(QStringLiteral("group"))->setEnabled(editing && s.canGroup());
    action(QStringLiteral("componentsMenu"))->setEnabled(drawn);
    action(QStringLiteral("makeComponent"))->setEnabled(editing && selected);
    action(QStringLiteral("detachInstance"))->setEnabled(editing && !s.selectedInstances().empty());
    action(QStringLiteral("resetOverrides"))->setEnabled(editing && !s.selectedInstances().empty());
    action(QStringLiteral("selectMainComponent"))->setEnabled(drawn && s.selectedMaster().has_value());
    action(QStringLiteral("ungroup"))->setEnabled(editing && s.canUngroup());
    action(QStringLiteral("frameSelection"))->setEnabled(editing && s.canGroup());
    action(QStringLiteral("addAutoLayout"))->setEnabled(editing && s.canGroup());
    action(QStringLiteral("removeAutoLayout"))->setEnabled(editing && s.canRemoveAutoLayout());
    action(QStringLiteral("clipContent"))->setEnabled(editing && !s.selectedFrames().empty());
    action(QStringLiteral("clipContent"))->setChecked(s.selectedFramesClip());
    action(QStringLiteral("makeCompoundPath"))->setEnabled(editing && s.canCombine());
    action(QStringLiteral("makeClippingMask"))->setEnabled(editing && s.selection().size() >= 2);
    action(QStringLiteral("makeOpacityMask"))->setEnabled(editing && s.selection().size() >= 2);
    const std::optional<QUuid> maskGroup = s.selectedMaskGroup();
    action(QStringLiteral("releaseOpacityMask"))->setEnabled(editing && maskGroup.has_value());
    action(QStringLiteral("opacityMaskClip"))->setEnabled(editing && maskGroup.has_value());
    action(QStringLiteral("opacityMaskClip"))->setChecked(maskGroup && s.document()->find(*maskGroup)->mask->clip);
    action(QStringLiteral("invertOpacityMask"))->setEnabled(editing && maskGroup.has_value());
    action(QStringLiteral("invertOpacityMask"))->setChecked(maskGroup && s.document()->find(*maskGroup)->mask->inverted);
    action(QStringLiteral("unlockAll"))->setEnabled(editing && drawn);
    action(QStringLiteral("showAll"))->setEnabled(editing && drawn);
    action(QStringLiteral("artboardSize"))->setEnabled(editing && drawn);
    action(QStringLiteral("artboardsMenu"))->setEnabled(drawn);
    action(QStringLiteral("artboardExported"))->setEnabled(editing && drawn);
    action(QStringLiteral("artboardExported"))->setChecked(!drawn || s.document()->artboard(s.activeArtboard()).exported);
    for (const char *name : {"newArtboard", "duplicateArtboard", "renameArtboard", "fitArtboardToArtwork", "switchArtboardOrientation",
                             "fitAllArtboards"})
        action(QString::fromLatin1(name))->setEnabled(editing && drawn);
    const int artboards = drawn ? s.document()->artboardCount() : 1;
    action(QStringLiteral("deleteArtboard"))->setEnabled(editing && drawn && artboards > 1);
    for (const char *name : {"nextArtboard", "previousArtboard"})
        action(QString::fromLatin1(name))->setEnabled(drawn && artboards > 1);
    const int pages = drawn ? s.document()->pageCount() : 1;
    action(QStringLiteral("pagesMenu"))->setEnabled(drawn);
    for (const char *name : {"newPage", "duplicatePage", "renamePage"})
        action(QString::fromLatin1(name))->setEnabled(editing && drawn);
    action(QStringLiteral("deletePage"))->setEnabled(editing && drawn && pages > 1);
    for (const char *name : {"nextPage", "previousPage"})
        action(QString::fromLatin1(name))->setEnabled(drawn && pages > 1 && !proposal);
    action(QStringLiteral("moveToPageMenu"))->setEnabled(editing && selected && pages > 1);
    action(QStringLiteral("collectForExport"))->setEnabled(editing && selected);
    action(QStringLiteral("exportForScreens"))->setEnabled(drawn);
    action(QStringLiteral("createOutlines"))->setEnabled(editing && drawn && selectionHas(s, ObjectKind::text));
    bool hasPoint = false, hasArea = false;
    for (const QUuid &id : s.selectedTexts()) {
        const bool area = s.document()->find(id)->text.area.has_value();
        hasPoint = hasPoint || !area;
        hasArea = hasArea || area;
    }
    action(QStringLiteral("convertToAreaType"))->setEnabled(editing && hasPoint);
    action(QStringLiteral("convertToPointType"))->setEnabled(editing && hasArea);
    action(QStringLiteral("findFont"))->setEnabled(editing && drawn);
    action(QStringLiteral("showTypeStyles"))->setEnabled(drawn);
    // The type keys work on selected type, or while typing; otherwise Alt+arrows fall through to duplicate and nudge.
    const std::vector<QUuid> leaves = s.selectedLeaves();
    const bool allText = drawn && !leaves.empty() && s.selectedTexts().size() == leaves.size();
    const bool typeKeys = !field && !proposal && drawn && (typing || allText);
    for (const char *name : {"increaseFontSize", "decreaseFontSize", "tightenTracking", "loosenTracking", "tightenTrackingMore",
                             "loosenTrackingMore", "resetTracking", "decreaseLeading", "increaseLeading", "raiseBaseline", "lowerBaseline"})
        action(QString::fromLatin1(name))->setEnabled(typeKeys);
    for (const char *name : {"typeSizeMenu", "typeTrackingMenu", "typeLeadingMenu", "typeBaselineMenu"})
        action(QString::fromLatin1(name))->setEnabled(drawn);
    const bool browserSelected = editing && m_canvas && m_canvas->browserViewHost() && s.selectedBrowserView().has_value();
    const bool pageEditing = editing && m_canvas && m_canvas->editPageFrame().has_value();
    action(QStringLiteral("browserViewMenu"))->setEnabled(browserSelected || pageEditing);
    action(QStringLiteral("browserViewEditPage"))->setEnabled(browserSelected || pageEditing);
    for (const char *name : {"browserViewCopyUrl", "browserViewOpen", "browserViewReload", "browserViewReloadHard", "browserViewSignIn"})
        action(QString::fromLatin1(name))->setEnabled(browserSelected);
    // The project items need a frame to act on: the selected one, or the one in Edit Page.
    const bool onFrame = browserSelected || pageEditing;
    std::optional<QUuid> frame = browserSelected ? s.selectedBrowserView() : std::nullopt;
    if (!frame && pageEditing)
        frame = m_canvas->editPageFrame();
    LiveFrames *live = frame ? s.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    const bool liveHere = live && frame && live->active(*frame);
    const bool notYours = onFrame && (!liveHere || live->snapshot(*frame).project.isEmpty());
    for (const char *name : {"browserViewDeploy", "browserViewSave", "browserViewReviewChanges", "browserViewHistory", "browserViewBuildIt",
                             "browserViewBuildItWithNote"})
        action(QString::fromLatin1(name))->setEnabled(onFrame);
    action(QStringLiteral("browserViewStopBuild"))->setEnabled(onFrame && m_agent && frame && m_agent->buildingFrame() == *frame);
    action(QStringLiteral("browserViewStopLive"))->setEnabled(liveHere);
    for (const char *name : {"browserViewKeepEdits", "browserViewEditSets", "browserViewShowOriginal", "browserViewExportCss"})
        action(QString::fromLatin1(name))->setEnabled(liveHere && notYours);
    action(QStringLiteral("browserViewThisIsMySite"))->setEnabled(notYours);
    // Only while a breakpoint button is holding a width.
    action(QStringLiteral("browserViewDesignWidth"))->setEnabled(browserSelected && s.isPreviewOnly());
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
        action(QString::fromLatin1(name))->setEnabled(!field && !typing && !proposal && !locked && drawn && s.canPaste());
    action(QStringLiteral("transformAgain"))->setEnabled(editing && s.canTransformAgain());
    action(QStringLiteral("copyProperties"))->setEnabled(!field && !typing && s.canCopyProperties());
    action(QStringLiteral("pasteProperties"))->setEnabled(editing && s.canPasteProperties());
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
    action(QStringLiteral("makePixelPerfect"))->setEnabled(editing && s.canMakePixelPerfect());
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
    const bool guides = drawn && !s.document()->guidesOnCurrentPage().empty();
    action(QStringLiteral("releaseGuides"))->setEnabled(editing && guides);
    action(QStringLiteral("clearGuides"))->setEnabled(editing && guides);
    action(QStringLiteral("showHistory"))->setEnabled(drawn);
    action(QStringLiteral("contextualTaskBar"))->setChecked(TaskBar::isTurnedOn());
    QAction *pageWorkspaces = action(QStringLiteral("pageWorkspaces"));
    const bool onHyprland = PageWorkspaces::hyprlandReachable();
    pageWorkspaces->setChecked(PageWorkspaces::isTurnedOn());
    pageWorkspaces->setEnabled(onHyprland);
    pageWorkspaces->setToolTip(onHyprland ? QStringLiteral("Each page of a document with two or more pages gets its own Hyprland workspace")
                                          : QStringLiteral("Needs Hyprland"));
    action(QStringLiteral("showLayers"))->setChecked(ContentView::showsPanel(ContentView::layersKey));
    action(QStringLiteral("showProperties"))->setChecked(ContentView::showsPanel(ContentView::propertiesKey));
}
