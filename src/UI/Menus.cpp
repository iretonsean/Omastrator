#include "UI/Menus.h"
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/CommandPalette.h"
#include "UI/ContextMenus.h"
#include "UI/ExportForScreensSheet.h"
#include "UI/HistoryPanel.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ObjectDialogs.h"
#include "UI/ShareController.h"
#include "UI/SharePanels.h"
#include "UI/TaskBarActions.h"
#include "UI/TextStylesPanel.h"
#include "UI/DesignSystemPanel.h"
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

Menus::Menus(ProjectWorkspace &workspace, QMenuBar &bar, QWidget &window, AgentBridge *agent, ShareController *share)
    : QObject(&bar), m_workspace(workspace), m_window(window), m_agent(agent), m_share(share)
{
    buildFile(bar);
    buildEdit(bar);
    buildObject(bar);
    buildSelect(bar);
    buildViewAndWindow(bar);
    // A field gaining or losing focus changes Undo's meaning.
    connect(qApp, &QApplication::focusChanged, this, &Menus::focusMoved);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &Menus::remap);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &Menus::synchronize);
    // Copying changes no document, but it's what the Paste entries wait for.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &Menus::synchronize);
    if (m_agent) {
        connect(m_agent, &AgentBridge::proposalChanged, this, &Menus::synchronize);
        connect(m_agent, &AgentBridge::designSystemRequested, this, [this](EditorSession *on, const QJsonObject &scan, const QString &source) {
            QPointer<EditorSession> target = on;
            DesignSystemPanel *panel = on ? showDesignSystem([target]() -> EditorSession * { return target; }) : showDesignSystem();
            if (!scan.isEmpty())
                panel->useProposal(SiteExtract::propose(scan, source));
        });
    }
    if (m_share)
        connect(m_share, &ShareController::changed, this, &Menus::synchronize);
    watchFront(nullptr);
}

QAction *Menus::add(QMenu *menu, const QString &name, const QString &text, const QKeySequence &shortcut, const std::function<void()> &run)
{
    QAction *made = menu->addAction(text);
    made->setObjectName(name);
    made->setProperty("originalShortcut", shortcut);
    made->setShortcut(ShortcutSettings::shared().menu(shortcut));
    connect(made, &QAction::triggered, this, run);
    return made;
}

void Menus::remap()
{
    for (QAction *entry : parent()->findChildren<QAction *>()) {
        const QVariant original = entry->property("originalShortcut");
        if (!original.isValid())
            continue;
        // A fixed second key (Figma's Shift+1 and Shift+2) rides along with the remappable one.
        const QVariant alias = entry->property("aliasShortcut");
        const QKeySequence mapped = ShortcutSettings::shared().menu(original.value<QKeySequence>());
        if (alias.isValid())
            entry->setShortcuts({mapped, alias.value<QKeySequence>()});
        else
            entry->setShortcut(mapped);
    }
}

Menus::~Menus()
{
    // Closing the shortcuts panel below moves focus; the menus it would update are already gone.
    disconnect(qApp, nullptr, this, nullptr);
}

QAction *Menus::action(const QString &name) const
{
    return parent()->findChild<QAction *>(name);
}

void Menus::buildFile(QMenuBar &bar)
{
    QMenu *file = bar.addMenu(QStringLiteral("&File"));
    add(file, QStringLiteral("newDocument"), QStringLiteral("New…"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] { m_workspace.newTab(); });
    add(file, QStringLiteral("open"), QStringLiteral("Open…"), QKeySequence(Qt::CTRL | Qt::Key_O), [this] { m_workspace.open(); });
    m_recent = file->addMenu(QStringLiteral("Open Recent"));
    m_recent->menuAction()->setObjectName(QStringLiteral("openRecent"));
    connect(m_recent, &QMenu::aboutToShow, this, &Menus::fillRecent);
    add(file, QStringLiteral("connectCloud"), QStringLiteral("Connect Cloud Storage…"), QKeySequence(), [this] { m_workspace.connectCloud(); });
    file->addSeparator();
    add(file, QStringLiteral("closeDocument"), QStringLiteral("Close"), QKeySequence(Qt::CTRL | Qt::Key_W),
        [this] { m_workspace.close(m_workspace.current().id); });
    add(file, QStringLiteral("save"), QStringLiteral("Save"), QKeySequence(Qt::CTRL | Qt::Key_S), [this] { m_workspace.save(m_workspace.current().id); });
    add(file, QStringLiteral("saveAs"), QStringLiteral("Save As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
        [this] { m_workspace.save(m_workspace.current().id, true); });
    // Ctrl+K is the command palette, so the lock takes Figma's Lock key (Ctrl+Shift+L) with Alt held: the heavier lock.
    add(file, QStringLiteral("lockDocument"), QStringLiteral("Lock Document"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L),
        [this] { session().setDocumentLocked(!session().isDocumentLocked()); });
    file->addSeparator();
    add(file, QStringLiteral("place"), QStringLiteral("Place…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this] { m_workspace.place(); });
    add(file, QStringLiteral("importFigmaLink"), QStringLiteral("Import from Figma Link…"), QKeySequence(), [this] { m_workspace.importFigmaLink(); });
    add(file, QStringLiteral("handToAgent"), QStringLiteral("Hand to Agent…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::handoff(*m_agent, &m_window);
    });
    // Share with client: one press shares and copies the link; the options and the list are one step away.
    add(file, QStringLiteral("shareWithClient"), QStringLiteral("Share"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S), [this] {
        if (m_share)
            SharePanels::shareNow(*m_share);
    });
    add(file, QStringLiteral("shareOptions"), QStringLiteral("Share Options…"), QKeySequence(), [this] {
        if (m_share)
            SharePanels::showOptions(*m_share, m_window);
    });
    add(file, QStringLiteral("sendToDevice"), QStringLiteral("Send to a Device…"), QKeySequence(), [this] {
        if (m_share)
            SharePanels::showDevices(*m_share, m_window);
    });
    add(file, QStringLiteral("sharedLinks"), QStringLiteral("Shared Links…"), QKeySequence(), [this] {
        if (m_share)
            SharePanels::showShared(*m_share, m_window);
    });
    QMenu *exports = file->addMenu(QStringLiteral("Export"));
    exports->menuAction()->setObjectName(QStringLiteral("exportMenu"));
    add(exports, QStringLiteral("exportPNG"), QStringLiteral("PNG…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E),
        [this] { m_workspace.exportAs(DocumentExporter::Format::png); });
    add(exports, QStringLiteral("exportJPEG"), QStringLiteral("JPEG…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::jpeg); });
    add(exports, QStringLiteral("exportSVG"), QStringLiteral("SVG…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::svg); });
    add(exports, QStringLiteral("exportPDF"), QStringLiteral("PDF…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::pdf); });
    exports->addSeparator();
    // Alt+Ctrl+E is PNG's; the batch export takes Figma's Export key.
    add(exports, QStringLiteral("exportForScreens"), QStringLiteral("Export for Screens…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E),
        [this] { (new ExportForScreensSheet(session(), &m_window))->open(); });
    file->addSeparator();
    add(file, QStringLiteral("quit"), QStringLiteral("Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { m_window.close(); })
        ->setMenuRole(QAction::QuitRole);
}

void Menus::fillRecent()
{
    m_recent->clear();
    for (const QString &path : ProjectWorkspace::recentFiles()) {
        QAction *entry = m_recent->addAction(ProjectWorkspace::recentIcon(path), ProjectWorkspace::recentLabel(path), this,
                                             [this, path] { m_workspace.openFile(path); });
        entry->setToolTip(path);
    }
    if (!m_recent->isEmpty())
        m_recent->addSeparator();
    QAction *clear = m_recent->addAction(QStringLiteral("Clear Recent"), this, [] { ProjectWorkspace::clearRecent(); });
    clear->setEnabled(!ProjectWorkspace::recentFiles().isEmpty());
}

void Menus::buildEdit(QMenuBar &bar)
{
    QMenu *edit = bar.addMenu(QStringLiteral("&Edit"));
    // A focused field keeps its own editing keys.
    add(edit, QStringLiteral("undo"), QStringLiteral("Undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), [this] {
        if (m_field)
            m_field->undo();
        else
            session().undo();
    });
    add(edit, QStringLiteral("redo"), QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] {
        if (m_field)
            m_field->redo();
        else
            session().redo();
    });
    edit->addSeparator();
    add(edit, QStringLiteral("cut"), QStringLiteral("Cut"), QKeySequence(Qt::CTRL | Qt::Key_X), [this] {
        if (m_field)
            m_field->cut();
        else
            session().cut();
    });
    add(edit, QStringLiteral("copy"), QStringLiteral("Copy"), QKeySequence(Qt::CTRL | Qt::Key_C), [this] {
        if (m_field)
            m_field->copy();
        else
            session().copy();
    });
    add(edit, QStringLiteral("paste"), QStringLiteral("Paste"), QKeySequence(Qt::CTRL | Qt::Key_V), [this] {
        if (m_field)
            m_field->paste();
        else
            session().paste();
    });
    add(edit, QStringLiteral("pasteInPlace"), QStringLiteral("Paste in Place"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
        [this] { session().paste(true); });
    add(edit, QStringLiteral("pasteInFront"), QStringLiteral("Paste in Front"), QKeySequence(Qt::CTRL | Qt::Key_F),
        [this] { session().paste(PastePosition::front); });
    add(edit, QStringLiteral("pasteInBack"), QStringLiteral("Paste in Back"), QKeySequence(Qt::CTRL | Qt::Key_B),
        [this] { session().paste(PastePosition::back); });
    // The style alone: fills, strokes, opacity and blend, and type's character style between texts.
    add(edit, QStringLiteral("copyProperties"), QStringLiteral("Copy Properties"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C),
        [this] { session().copyProperties(); });
    add(edit, QStringLiteral("pasteProperties"), QStringLiteral("Paste Properties"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V),
        [this] { session().pasteProperties(); });
    // Ctrl+J is Illustrator's Join; Ctrl+D is Transform Again.
    add(edit, QStringLiteral("duplicate"), QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D),
        [this] { session().duplicateSelection(); });
    // The Delete key stays with the canvas and list.
    add(edit, QStringLiteral("delete"), QStringLiteral("Delete"), QKeySequence(), [this] {
        if (session().tool() == Tool::directSelect && !session().pickedNodes().empty())
            session().deletePickedNodes();
        else
            session().deleteSelection();
    });
    edit->addSeparator();
    add(edit, QStringLiteral("preferences"), QStringLiteral("Preferences…"), QKeySequence(), [this] { ObjectDialogs::preferences(&m_window); })
        ->setMenuRole(QAction::PreferencesRole);
    add(edit, QStringLiteral("exportSettings"), QStringLiteral("Export Settings…"), QKeySequence(), [this] { m_workspace.exportSettings(); });
    add(edit, QStringLiteral("importSettings"), QStringLiteral("Import Settings…"), QKeySequence(), [this] { m_workspace.importSettings(); });
}

void Menus::buildObject(QMenuBar &bar)
{
    QMenu *object = bar.addMenu(QStringLiteral("&Object"));
    QMenu *transform = object->addMenu(QStringLiteral("Transform"));
    transform->menuAction()->setObjectName(QStringLiteral("transformMenu"));
    add(transform, QStringLiteral("transformAgain"), QStringLiteral("Transform Again"), QKeySequence(Qt::CTRL | Qt::Key_D),
        [this] { session().transformAgain(); });
    transform->addSeparator();
    add(transform, QStringLiteral("moveDialog"), QStringLiteral("Move…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
        [this] { ObjectDialogs::move(session(), &m_window); });
    add(transform, QStringLiteral("rotateDialog"), QStringLiteral("Rotate…"), QKeySequence(), [this] { ObjectDialogs::rotate(session(), &m_window); });
    add(transform, QStringLiteral("reflectDialog"), QStringLiteral("Reflect…"), QKeySequence(), [this] { ObjectDialogs::reflect(session(), &m_window); });
    add(transform, QStringLiteral("scaleDialog"), QStringLiteral("Scale…"), QKeySequence(), [this] { ObjectDialogs::scale(session(), &m_window); });
    transform->addSeparator();
    add(transform, QStringLiteral("flipHorizontal"), QStringLiteral("Flip Horizontal"), QKeySequence(Qt::SHIFT | Qt::Key_H), [this] { session().flipSelection(Qt::Horizontal); });
    add(transform, QStringLiteral("flipVertical"), QStringLiteral("Flip Vertical"), QKeySequence(Qt::SHIFT | Qt::Key_V), [this] { session().flipSelection(Qt::Vertical); });
    QMenu *arrange = object->addMenu(QStringLiteral("Arrange"));
    arrange->menuAction()->setObjectName(QStringLiteral("arrangeMenu"));
    add(arrange, QStringLiteral("bringToFront"), QStringLiteral("Bring to Front"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight),
        [this] { session().arrange(ArrangeOrder::bringToFront); });
    add(arrange, QStringLiteral("bringForward"), QStringLiteral("Bring Forward"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
        [this] { session().arrange(ArrangeOrder::bringForward); });
    add(arrange, QStringLiteral("sendBackward"), QStringLiteral("Send Backward"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
        [this] { session().arrange(ArrangeOrder::sendBackward); });
    add(arrange, QStringLiteral("sendToBack"), QStringLiteral("Send to Back"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft),
        [this] { session().arrange(ArrangeOrder::sendToBack); });
    object->addSeparator();
    add(object, QStringLiteral("group"), QStringLiteral("Group"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { session().groupSelection(); });
    add(object, QStringLiteral("ungroup"), QStringLiteral("Ungroup"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { session().ungroupSelection(); });
    // Figma's frames: the selection in a frame its size, and whether a frame shows what lies past its box.
    add(object, QStringLiteral("frameSelection"), QStringLiteral("Frame Selection"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G),
        [this] { session().frameSelection(); });
    add(object, QStringLiteral("clipContent"), QStringLiteral("Clip Content"), QKeySequence(), [this] {
        session().setClipsContent(!session().selectedFramesClip());
    })->setCheckable(true);
    // Figma's Shift+A. Text fields and open type answer the key before a menu does, so a capital A still types.
    add(object, QStringLiteral("addAutoLayout"), QStringLiteral("Add Auto Layout"), QKeySequence(Qt::SHIFT | Qt::Key_A),
        [this] { session().addAutoLayout(); });
    add(object, QStringLiteral("removeAutoLayout"), QStringLiteral("Remove Auto Layout"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_A),
        [this] { session().removeAutoLayout(); });
    object->addSeparator();
    // Components (docs/DESIGN-SYSTEMS.md): Figma's keys for Make and Detach.
    QMenu *components = object->addMenu(QStringLiteral("Components"));
    components->menuAction()->setObjectName(QStringLiteral("componentsMenu"));
    add(components, QStringLiteral("makeComponent"), QStringLiteral("Make Component"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K),
        [this] { session().makeComponent(); });
    add(components, QStringLiteral("detachInstance"), QStringLiteral("Detach Instance"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B),
        [this] { session().detachInstances(); });
    add(components, QStringLiteral("resetOverrides"), QStringLiteral("Reset Overrides"), QKeySequence(), [this] { session().resetOverrides(); });
    add(components, QStringLiteral("selectMainComponent"), QStringLiteral("Select Main Component"), QKeySequence(), [this] {
        if (const auto master = session().selectedMaster())
            session().select({*master});
    });
    object->addSeparator();
    // Figma's Ctrl+Shift+L and Ctrl+Shift+H ride beside Illustrator's keys.
    alias(add(object, QStringLiteral("lockSelection"), QStringLiteral("Lock Selection"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] { session().lockSelection(); }),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    add(object, QStringLiteral("unlockAll"), QStringLiteral("Unlock All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2), [this] { session().unlockAll(); });
    alias(add(object, QStringLiteral("hideSelection"), QStringLiteral("Hide Selection"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] { session().hideSelection(); }),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    add(object, QStringLiteral("showAll"), QStringLiteral("Show All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3), [this] { session().showAll(); });
    object->addSeparator();
    QMenu *path = object->addMenu(QStringLiteral("Path"));
    path->menuAction()->setObjectName(QStringLiteral("pathMenu"));
    add(path, QStringLiteral("outlineStroke"), QStringLiteral("Outline Stroke"), QKeySequence(), [this] { session().outlineSelectedStrokes(); });
    add(path, QStringLiteral("offsetPath"), QStringLiteral("Offset Path…"), QKeySequence(), [this] { ObjectDialogs::offsetPath(session(), &m_window); });
    add(path, QStringLiteral("simplify"), QStringLiteral("Simplify"), QKeySequence(), [this] { session().simplifySelection(1); });
    path->addSeparator();
    // Duplicate moved to Ctrl+Alt+D so Ctrl+J is Join, as in Illustrator.
    add(path, QStringLiteral("join"), QStringLiteral("Join"), QKeySequence(Qt::CTRL | Qt::Key_J), [this] { session().joinPaths(); });
    add(path, QStringLiteral("average"), QStringLiteral("Average…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J),
        [this] { ObjectDialogs::average(session(), &m_window); });
    add(path, QStringLiteral("reversePathDirection"), QStringLiteral("Reverse Path Direction"), QKeySequence(), [this] { session().reversePaths(); });
    object->addSeparator();
    add(object, QStringLiteral("makePixelPerfect"), QStringLiteral("Make Pixel Perfect"), QKeySequence(), [this] { session().makePixelPerfect(); });
    QMenu *compound = object->addMenu(QStringLiteral("Compound Path"));
    compound->menuAction()->setObjectName(QStringLiteral("compoundMenu"));
    add(compound, QStringLiteral("makeCompoundPath"), QStringLiteral("Make"), QKeySequence(Qt::CTRL | Qt::Key_8), [this] { session().makeCompoundPath(); });
    add(compound, QStringLiteral("releaseCompoundPath"), QStringLiteral("Release"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_8),
        [this] { session().releaseCompoundPath(); });
    compound->addSeparator();
    add(compound, QStringLiteral("evenOddFillRule"), QStringLiteral("Even-Odd Fill Rule"), QKeySequence(), [this] {
        const std::vector<QUuid> paths = session().selectedCompoundPaths();
        const bool evenOdd = !paths.empty() && session().document()->find(paths.front())->path.fillRule == Qt::OddEvenFill;
        session().setFillRuleOfSelection(evenOdd ? Qt::WindingFill : Qt::OddEvenFill);
    })->setCheckable(true);
    QMenu *clipping = object->addMenu(QStringLiteral("Clipping Mask"));
    clipping->menuAction()->setObjectName(QStringLiteral("clippingMenu"));
    // Figma's Use as Mask is Ctrl+Alt+M.
    alias(add(clipping, QStringLiteral("makeClippingMask"), QStringLiteral("Make"), QKeySequence(Qt::CTRL | Qt::Key_7), [this] { session().makeClippingMask(); }),
          QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M));
    add(clipping, QStringLiteral("releaseClippingMask"), QStringLiteral("Release"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_7),
        [this] { session().releaseClippingMask(); });
    QMenu *opacityMask = object->addMenu(QStringLiteral("Opacity Mask"));
    opacityMask->menuAction()->setObjectName(QStringLiteral("opacityMaskMenu"));
    add(opacityMask, QStringLiteral("makeOpacityMask"), QStringLiteral("Make Mask"), QKeySequence(), [this] { session().makeOpacityMask(); });
    add(opacityMask, QStringLiteral("releaseOpacityMask"), QStringLiteral("Release"), QKeySequence(), [this] { session().releaseOpacityMask(); });
    opacityMask->addSeparator();
    add(opacityMask, QStringLiteral("opacityMaskClip"), QStringLiteral("Clip"), QKeySequence(), [this] {
        const std::optional<QUuid> group = session().selectedMaskGroup();
        const bool clipped = group && session().document()->find(*group)->mask->clip;
        session().setOpacityMaskClip(!clipped);
    })->setCheckable(true);
    add(opacityMask, QStringLiteral("invertOpacityMask"), QStringLiteral("Invert Mask"), QKeySequence(), [this] {
        const std::optional<QUuid> group = session().selectedMaskGroup();
        const bool inverted = group && session().document()->find(*group)->mask->inverted;
        session().setOpacityMaskInverted(!inverted);
    })->setCheckable(true);
    QMenu *trace = object->addMenu(QStringLiteral("Image Trace"));
    trace->menuAction()->setObjectName(QStringLiteral("imageTraceMenu"));
    add(trace, QStringLiteral("imageTraceMake"), QStringLiteral("Make"), QKeySequence(), [this] { session().traceSelectedImage(); });
    add(trace, QStringLiteral("vectorizeWithAI"), QStringLiteral("Vectorize with AI…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::vectorize(*m_agent, &m_window);
    });
    object->addSeparator();
    add(object, QStringLiteral("generate"), QStringLiteral("Generate…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::generate(*m_agent, &m_window);
    });
    add(object, QStringLiteral("editWithInstruction"), QStringLiteral("Edit with Instruction…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::editWithInstruction(*m_agent, &m_window);
    });
    // Object ▸ Text Wrap ▸ Make/Release (P2-4): the selected non-text objects become,
    // or stop being, exclusions area type above them in paint order wraps around.
    QMenu *wrap = object->addMenu(QStringLiteral("Text Wrap"));
    wrap->menuAction()->setObjectName(QStringLiteral("textWrapMenu"));
    add(wrap, QStringLiteral("makeTextWrap"), QStringLiteral("Make"), QKeySequence(), [this] {
        for (const QUuid &id : session().selection()) {
            const VectorObject *found = session().document()->find(id);
            if (found && found->kind != ObjectKind::text)
                session().setTextWrap(id, 6.0);
        }
    });
    add(wrap, QStringLiteral("releaseTextWrap"), QStringLiteral("Release"), QKeySequence(), [this] {
        for (const QUuid &id : session().selection())
            session().setTextWrap(id, std::nullopt);
    });
    object->addSeparator();
    add(object, QStringLiteral("artboardSize"), QStringLiteral("Artboard Size…"), QKeySequence(), [this] { ObjectDialogs::artboardSize(session(), &m_window); });
    QMenu *artboards = object->addMenu(QStringLiteral("Artboards"));
    artboards->menuAction()->setObjectName(QStringLiteral("artboardsMenu"));
    add(artboards, QStringLiteral("newArtboard"), QStringLiteral("New Artboard"), QKeySequence(), [this] { session().addArtboard(); });
    add(artboards, QStringLiteral("duplicateArtboard"), QStringLiteral("Duplicate Artboard"), QKeySequence(),
        [this] { session().duplicateArtboard(session().activeArtboard()); });
    add(artboards, QStringLiteral("renameArtboard"), QStringLiteral("Rename…"), QKeySequence(),
        [this] { ObjectDialogs::renameArtboard(session(), session().activeArtboard(), &m_window); });
    add(artboards, QStringLiteral("deleteArtboard"), QStringLiteral("Delete Artboard"), QKeySequence(),
        [this] { session().deleteArtboard(session().activeArtboard()); });
    add(artboards, QStringLiteral("artboardExported"), QStringLiteral("Export Artboard"), QKeySequence(), [this] {
        const int index = session().activeArtboard();
        session().setArtboardExported(index, !session().document()->artboard(index).exported);
    })->setCheckable(true);
    add(artboards, QStringLiteral("fitArtboardToArtwork"), QStringLiteral("Fit to Artwork Bounds"), QKeySequence(),
        [this] { session().fitArtboardToArtwork(session().activeArtboard()); });
    add(artboards, QStringLiteral("switchArtboardOrientation"), QStringLiteral("Switch Orientation"), QKeySequence(),
        [this] { session().switchArtboardOrientation(session().activeArtboard()); });
    artboards->addSeparator();
    // Page keys aren't one-character chords; they're the only entries this test lets past F7.
    add(artboards, QStringLiteral("nextArtboard"), QStringLiteral("Next Artboard"), QKeySequence(Qt::SHIFT | Qt::Key_PageDown),
        [this] { session().showArtboard(true); });
    add(artboards, QStringLiteral("previousArtboard"), QStringLiteral("Previous Artboard"), QKeySequence(Qt::SHIFT | Qt::Key_PageUp),
        [this] { session().showArtboard(false); });
    add(object, QStringLiteral("collectForExport"), QStringLiteral("Collect for Export"), QKeySequence(),
        [this] { session().collectForExport(session().selection()); });
    QMenu *type = bar.addMenu(QStringLiteral("&Type"));
    add(type, QStringLiteral("createOutlines"), QStringLiteral("Create Outlines"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
        [this] { session().convertTextToPaths(); });
    add(type, QStringLiteral("convertToAreaType"), QStringLiteral("Convert to Area Type"), QKeySequence(), [this] { session().convertTextType(true); });
    add(type, QStringLiteral("convertToPointType"), QStringLiteral("Convert to Point Type"), QKeySequence(), [this] { session().convertTextType(false); });
    // Type on a Path (P2-3): Flip reads the path the other way, keeping the visible start put.
    QMenu *onPath = type->addMenu(QStringLiteral("Type on a Path"));
    onPath->menuAction()->setObjectName(QStringLiteral("typeOnPathMenu"));
    add(onPath, QStringLiteral("flipTypeOnPath"), QStringLiteral("Flip"), QKeySequence(), [this] {
        for (const QUuid &id : session().selectedTexts())
            session().flipTypeOnPath(id);
    });
    // Threaded text (P2-4): Remove Threading splits the story so each box keeps what it shows.
    QMenu *threaded = type->addMenu(QStringLiteral("Threaded Text"));
    threaded->menuAction()->setObjectName(QStringLiteral("threadedTextMenu"));
    add(threaded, QStringLiteral("removeThreading"), QStringLiteral("Remove Threading"), QKeySequence(), [this] {
        for (const QUuid &id : session().selectedTexts())
            session().removeThreading(id);
    });
    add(type, QStringLiteral("findFont"), QStringLiteral("Find/Replace Font…"), QKeySequence(), [this] { ObjectDialogs::findFont(session(), &m_window); });
    type->addSeparator();
    buildTypeKeys(*type);
}

// Illustrator's type keys, as entries so they show their keys and can be remapped.
void Menus::buildTypeKeys(QMenu &type)
{
    const auto step = [this](EditorSession::TextStep what, double amount) {
        return [this, what, amount] { session().stepText(what, amount); };
    };
    // At a caret, the tracking keys kern the pair around it instead.
    const auto track = [this](double amount) {
        return [this, amount] {
            if (!(m_canvas && m_canvas->kernAtCaret(amount)))
                session().stepText(EditorSession::TextStep::tracking, amount);
        };
    };
    QMenu *size = type.addMenu(QStringLiteral("Size"));
    size->menuAction()->setObjectName(QStringLiteral("typeSizeMenu"));
    add(size, QStringLiteral("increaseFontSize"), QStringLiteral("Increase Font Size"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Greater),
        step(EditorSession::TextStep::size, 2));
    add(size, QStringLiteral("decreaseFontSize"), QStringLiteral("Decrease Font Size"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Less),
        step(EditorSession::TextStep::size, -2));
    QMenu *tracking = type.addMenu(QStringLiteral("Tracking"));
    tracking->menuAction()->setObjectName(QStringLiteral("typeTrackingMenu"));
    add(tracking, QStringLiteral("tightenTracking"), QStringLiteral("Tighten Tracking"), QKeySequence(Qt::ALT | Qt::Key_Left), track(-20));
    add(tracking, QStringLiteral("loosenTracking"), QStringLiteral("Loosen Tracking"), QKeySequence(Qt::ALT | Qt::Key_Right), track(20));
    add(tracking, QStringLiteral("tightenTrackingMore"), QStringLiteral("Tighten Tracking ×5"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left),
        track(-100));
    add(tracking, QStringLiteral("loosenTrackingMore"), QStringLiteral("Loosen Tracking ×5"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right),
        track(100));
    add(tracking, QStringLiteral("resetTracking"), QStringLiteral("Reset Tracking"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Q), [this] {
        session().updateText([](TextContent &text) {
            text.tracking = 0;
            text.kerns.clear();
        }, QStringLiteral("Reset Tracking"));
    });
    // Alt+Up tightens leading, as in Illustrator.
    QMenu *leading = type.addMenu(QStringLiteral("Leading"));
    leading->menuAction()->setObjectName(QStringLiteral("typeLeadingMenu"));
    add(leading, QStringLiteral("decreaseLeading"), QStringLiteral("Decrease Leading"), QKeySequence(Qt::ALT | Qt::Key_Up),
        step(EditorSession::TextStep::leading, -2));
    add(leading, QStringLiteral("increaseLeading"), QStringLiteral("Increase Leading"), QKeySequence(Qt::ALT | Qt::Key_Down),
        step(EditorSession::TextStep::leading, 2));
    QMenu *baseline = type.addMenu(QStringLiteral("Baseline Shift"));
    baseline->menuAction()->setObjectName(QStringLiteral("typeBaselineMenu"));
    add(baseline, QStringLiteral("raiseBaseline"), QStringLiteral("Raise Baseline"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Up),
        step(EditorSession::TextStep::baselineShift, 2));
    add(baseline, QStringLiteral("lowerBaseline"), QStringLiteral("Lower Baseline"), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Down),
        step(EditorSession::TextStep::baselineShift, -2));
}

void Menus::buildViewAndWindow(QMenuBar &bar)
{
    QMenu *view = bar.addMenu(QStringLiteral("&View"));
    add(view, QStringLiteral("zoomIn"), QStringLiteral("Zoom In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { session().zoomIn(); });
    add(view, QStringLiteral("zoomOut"), QStringLiteral("Zoom Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { session().zoomOut(); });
    alias(add(view, QStringLiteral("fitArtboard"), QStringLiteral("Fit Artboard in Window"), QKeySequence(Qt::CTRL | Qt::Key_0),
              [this] { session().zoomToFit(); }),
          QKeySequence(Qt::SHIFT | Qt::Key_1));
    add(view, QStringLiteral("fitAllArtboards"), QStringLiteral("Fit All in Window"), QKeySequence(), [this] { session().fitAllArtboards(); });
    alias(add(view, QStringLiteral("zoomToSelection"), QStringLiteral("Zoom to Selection"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0),
              [this] { session().zoomToSelection(); }),
          QKeySequence(Qt::SHIFT | Qt::Key_2));
    // Shift+0 is Figma's Zoom to 100 %.
    alias(add(view, QStringLiteral("actualSize"), QStringLiteral("Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { session().actualSize(); }),
          QKeySequence(Qt::SHIFT | Qt::Key_0));
    view->addSeparator();
    add(view, QStringLiteral("outline"), QStringLiteral("Outline"), QKeySequence(Qt::CTRL | Qt::Key_Y),
        [this] { session().setShowsOutline(!session().showsOutline); })
        ->setCheckable(true);
    add(view, QStringLiteral("showGrid"), QStringLiteral("Show Grid"), QKeySequence(Qt::CTRL | Qt::Key_Apostrophe),
        [this] { session().setShowsGrid(!session().showsGrid); })
        ->setCheckable(true);
    add(view, QStringLiteral("snapToGrid"), QStringLiteral("Snap to Grid"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Apostrophe),
        [this] { session().setSnapsToGrid(!session().snapsToGrid); })
        ->setCheckable(true);
    add(view, QStringLiteral("snapToPixel"), QStringLiteral("Snap to Pixel"), QKeySequence(), [this] { session().setSnapsToPixel(!session().snapsToPixel); })
        ->setCheckable(true);
    add(view, QStringLiteral("pixelGrid"), QStringLiteral("Pixel Grid"), QKeySequence(), [this] { session().setShowsPixelGrid(!session().showsPixelGrid); })
        ->setCheckable(true);
    view->addSeparator();
    add(view, QStringLiteral("rulers"), QStringLiteral("Rulers"), QKeySequence(Qt::CTRL | Qt::Key_R), [this] { session().setShowsRulers(!session().showsRulers); })
        ->setCheckable(true);
    QMenu *guides = view->addMenu(QStringLiteral("Guides"));
    guides->menuAction()->setObjectName(QStringLiteral("guidesMenu"));
    add(guides, QStringLiteral("hideGuides"), QStringLiteral("Hide Guides"), QKeySequence(Qt::CTRL | Qt::Key_Semicolon),
        [this] { session().setShowsGuides(!session().showsGuides); });
    add(guides, QStringLiteral("lockGuides"), QStringLiteral("Lock Guides"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon),
        [this] { session().setGuidesLocked(!session().guidesLocked); })
        ->setCheckable(true);
    add(guides, QStringLiteral("makeGuides"), QStringLiteral("Make Guides"), QKeySequence(Qt::CTRL | Qt::Key_5), [this] { session().makeGuides(); });
    add(guides, QStringLiteral("releaseGuides"), QStringLiteral("Release Guides"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_5),
        [this] { session().releaseGuides(); });
    add(guides, QStringLiteral("clearGuides"), QStringLiteral("Clear Guides"), QKeySequence(), [this] { session().clearGuides(); });
    view->addSeparator();
    add(view, QStringLiteral("contextualTaskBar"), QStringLiteral("Contextual Task Bar"), QKeySequence(), [this] {
        TaskBar::setTurnedOn(!TaskBar::isTurnedOn());
        synchronize();
    })->setCheckable(true);
    QMenu *window = bar.addMenu(QStringLiteral("&Window"));
    // F7 is Illustrator's; no remap covers function keys.
    // Alt+1 is Figma's Layers key.
    QAction *layers = add(window, QStringLiteral("showLayers"), QStringLiteral("Layers"), QKeySequence(Qt::Key_F7), [this] {
        ContentView::setShowsPanel(ContentView::layersKey, !ContentView::showsPanel(ContentView::layersKey));
        emit layersToggled(ContentView::showsPanel(ContentView::layersKey));
        synchronize();
    });
    layers->setCheckable(true);
    alias(layers, QKeySequence(Qt::ALT | Qt::Key_1));
    QAction *properties = add(window, QStringLiteral("showProperties"), QStringLiteral("Properties"), QKeySequence(Qt::ALT | Qt::Key_8), [this] {
        ContentView::setShowsPanel(ContentView::propertiesKey, !ContentView::showsPanel(ContentView::propertiesKey));
        emit propertiesToggled(ContentView::showsPanel(ContentView::propertiesKey));
        synchronize();
    });
    properties->setCheckable(true);
    add(window, QStringLiteral("showHistory"), QStringLiteral("History"), QKeySequence(), [this] { showHistory(); });
    add(window, QStringLiteral("showTypeStyles"), QStringLiteral("Type Styles"), QKeySequence(), [this] {
        if (!m_typeStyles || !m_typeStylesPanel.isVisible()) {
            m_typeStyles = new TextStylesPanel([this]() -> EditorSession * { return &session(); });
            m_typeStylesPanel.show(QStringLiteral("Type Styles"), m_typeStyles);
        }
    });
    add(window, QStringLiteral("showDesignSystem"), QStringLiteral("Design System"), QKeySequence(), [this] { showDesignSystem(); });
    add(window, QStringLiteral("showSwatches"), QStringLiteral("Swatches"), QKeySequence(), [this] {
        if (m_agent)
            m_agent->showSwatchesPanel();
    })->setEnabled(m_agent != nullptr);
    QMenu *help = bar.addMenu(QStringLiteral("&Help"));
    // Figma's Actions menu keys: Ctrl+K, and Ctrl+/ beside it.
    alias(add(help, QStringLiteral("commandPalette"), QStringLiteral("Command Palette…"), QKeySequence(Qt::CTRL | Qt::Key_K),
              [this] { commandPalette()->open(); }),
          QKeySequence(Qt::CTRL | Qt::Key_Slash));
    // Figma's Ctrl+Shift+? (typed as Ctrl+Shift+/), Illustrator's Edit ▸ Keyboard Shortcuts in its other home.
    alias(add(help, QStringLiteral("keyboardShortcuts"), QStringLiteral("Keyboard Shortcuts…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Question), [this] {
              m_shortcutsPanel.onClose = [this] { m_shortcutsPanel.close(); };
              m_shortcutsPanel.show(QStringLiteral("Keyboard Shortcuts"), new KeyboardShortcutsSheet([this] { m_shortcutsPanel.close(); }));
          }),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Slash));
    help->addSeparator();
    add(help, QStringLiteral("connectAgent"), QStringLiteral("Connect an Agent…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::connectAgent(*m_agent, &m_window);
    });
    add(help, QStringLiteral("about"), QStringLiteral("About Omastrator"), QKeySequence(), [this] {
        QMessageBox::about(&m_window, QStringLiteral("About Omastrator"),
                           QStringLiteral("<b>Omastrator</b> %1<br>Vector illustration for Linux, made for Omarchy.<br>"
                                          "Built on OmaPhoto, the Linux port of Compositor.")
                               .arg(QApplication::applicationVersion()));
    })->setMenuRole(QAction::AboutRole);
}

DesignSystemPanel *Menus::designSystem() const
{
    return m_designSystem;
}

DesignSystemPanel *Menus::showDesignSystem(std::function<EditorSession *()> sessionOf)
{
    if (!sessionOf)
        sessionOf = [this]() -> EditorSession * { return &session(); };
    m_designSystem = new DesignSystemPanel(std::move(sessionOf));
    m_designSystemPanel.show(QStringLiteral("Design System"), m_designSystem);
    return m_designSystem;
}

void Menus::showHistory()
{
    m_historyPanel.onClose = [this] { m_historyPanel.close(); };
    m_historyPanel.show(QStringLiteral("History"), new HistoryPanel(session()));
}

void Menus::watchFront(EditorCanvas *canvas)
{
    // The History panel follows the front document.
    if (m_historyPanel.isVisible())
        showHistory();
    // The front tab's session and canvas drive every entry.
    disconnect(m_sessionWatch);
    m_sessionWatch = connect(&session(), &EditorSession::changed, this, &Menus::synchronize);
    disconnect(m_canvasWatch);
    m_canvas = canvas;
    if (m_typeStyles)
        m_typeStyles->follow();
    if (m_designSystem)
        m_designSystem->follow();
    disconnect(m_menuWatch);
    if (m_canvas) {
        if (!m_canvas->findChild<TaskBar *>())
            TaskBarActions::attach(*this, m_agent, *m_canvas);
        m_canvasWatch = connect(m_canvas, &EditorCanvas::textEditingChanged, this, &Menus::synchronize);
        m_menuWatch = connect(m_canvas, &EditorCanvas::contextMenuRequested, this, [this](QPoint at, const QList<QUuid> &hits) {
            if (!m_canvas)
                return;
            QMenu *menu = ContextMenus::forCanvas(*this, session(), *m_canvas, hits, m_canvas);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(at);
        });
    }
    synchronize();
}

CommandPalette *Menus::commandPalette()
{
    if (!m_palette)
        m_palette = new CommandPalette(*this, &m_window);
    return m_palette;
}

void Menus::alias(QAction *entry, const QKeySequence &second)
{
    entry->setProperty("aliasShortcut", second);
    entry->setShortcuts({entry->shortcut(), second});
}

void Menus::focusMoved(QWidget *, QWidget *to)
{
    // The menu bar borrows focus, and so does the command palette's search: the field keeps Undo.
    if (to && to->objectName() == QLatin1String("commandSearch"))
        return;
    if (qobject_cast<QMenuBar *>(to) == nullptr)
        m_field = qobject_cast<QLineEdit *>(to);
    synchronize();
}
