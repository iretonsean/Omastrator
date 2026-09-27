#include "UI/Menus.h"
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ObjectDialogs.h"
#include <QApplication>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>

Menus::Menus(ProjectWorkspace &workspace, QMenuBar &bar, QWidget &window, AgentBridge *agent)
    : QObject(&bar), m_workspace(workspace), m_window(window), m_agent(agent)
{
    buildFile(bar);
    buildEdit(bar);
    buildObject(bar);
    buildViewAndWindow(bar);
    // A field gaining or losing focus changes Undo's meaning.
    connect(qApp, &QApplication::focusChanged, this, &Menus::focusMoved);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &Menus::remap);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &Menus::synchronize);
    if (m_agent)
        connect(m_agent, &AgentBridge::proposalChanged, this, &Menus::synchronize);
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
        if (original.isValid())
            entry->setShortcut(ShortcutSettings::shared().menu(original.value<QKeySequence>()));
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
    file->addSeparator();
    add(file, QStringLiteral("place"), QStringLiteral("Place…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this] { m_workspace.place(); });
    add(file, QStringLiteral("handToAgent"), QStringLiteral("Hand to Agent…"), QKeySequence(), [this] {
        if (m_agent)
            AgentSheets::handoff(*m_agent, &m_window);
    });
    QMenu *exports = file->addMenu(QStringLiteral("Export"));
    exports->menuAction()->setObjectName(QStringLiteral("exportMenu"));
    add(exports, QStringLiteral("exportPNG"), QStringLiteral("PNG…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_E),
        [this] { m_workspace.exportAs(DocumentExporter::Format::png); });
    add(exports, QStringLiteral("exportJPEG"), QStringLiteral("JPEG…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::jpeg); });
    add(exports, QStringLiteral("exportSVG"), QStringLiteral("SVG…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::svg); });
    add(exports, QStringLiteral("exportPDF"), QStringLiteral("PDF…"), QKeySequence(), [this] { m_workspace.exportAs(DocumentExporter::Format::pdf); });
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
    add(edit, QStringLiteral("duplicate"), QStringLiteral("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_J), [this] { session().duplicateSelection(); });
    // The Delete key stays with the canvas and list.
    add(edit, QStringLiteral("delete"), QStringLiteral("Delete"), QKeySequence(), [this] {
        if (session().tool() == Tool::directSelect && !session().pickedNodes().empty())
            session().deletePickedNodes();
        else
            session().deleteSelection();
    });
    edit->addSeparator();
    add(edit, QStringLiteral("selectAll"), QStringLiteral("Select All"), QKeySequence(Qt::CTRL | Qt::Key_A), [this] {
        if (m_field)
            m_field->selectAll();
        else
            session().selectAll();
    });
    add(edit, QStringLiteral("deselect"), QStringLiteral("Deselect"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] { session().deselectAll(); });
    edit->addSeparator();
    add(edit, QStringLiteral("keyboardShortcuts"), QStringLiteral("Keyboard Shortcuts…"), QKeySequence(), [this] {
        m_shortcutsPanel.onClose = [this] { m_shortcutsPanel.close(); };
        m_shortcutsPanel.show(QStringLiteral("Keyboard Shortcuts"), new KeyboardShortcutsSheet([this] { m_shortcutsPanel.close(); }));
    });
}

void Menus::buildObject(QMenuBar &bar)
{
    QMenu *object = bar.addMenu(QStringLiteral("&Object"));
    QMenu *transform = object->addMenu(QStringLiteral("Transform"));
    transform->menuAction()->setObjectName(QStringLiteral("transformMenu"));
    add(transform, QStringLiteral("moveDialog"), QStringLiteral("Move…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
        [this] { ObjectDialogs::move(session(), &m_window); });
    add(transform, QStringLiteral("rotateDialog"), QStringLiteral("Rotate…"), QKeySequence(), [this] { ObjectDialogs::rotate(session(), &m_window); });
    add(transform, QStringLiteral("reflectDialog"), QStringLiteral("Reflect…"), QKeySequence(), [this] { ObjectDialogs::reflect(session(), &m_window); });
    add(transform, QStringLiteral("scaleDialog"), QStringLiteral("Scale…"), QKeySequence(), [this] { ObjectDialogs::scale(session(), &m_window); });
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
    object->addSeparator();
    add(object, QStringLiteral("lockSelection"), QStringLiteral("Lock Selection"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] { session().lockSelection(); });
    add(object, QStringLiteral("unlockAll"), QStringLiteral("Unlock All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2), [this] { session().unlockAll(); });
    add(object, QStringLiteral("hideSelection"), QStringLiteral("Hide Selection"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] { session().hideSelection(); });
    add(object, QStringLiteral("showAll"), QStringLiteral("Show All"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3), [this] { session().showAll(); });
    object->addSeparator();
    QMenu *path = object->addMenu(QStringLiteral("Path"));
    path->menuAction()->setObjectName(QStringLiteral("pathMenu"));
    add(path, QStringLiteral("outlineStroke"), QStringLiteral("Outline Stroke"), QKeySequence(), [this] { session().outlineSelectedStrokes(); });
    add(path, QStringLiteral("offsetPath"), QStringLiteral("Offset Path…"), QKeySequence(), [this] { ObjectDialogs::offsetPath(session(), &m_window); });
    add(path, QStringLiteral("simplify"), QStringLiteral("Simplify"), QKeySequence(), [this] { session().simplifySelection(1); });
    QMenu *compound = object->addMenu(QStringLiteral("Compound Path"));
    compound->menuAction()->setObjectName(QStringLiteral("compoundMenu"));
    add(compound, QStringLiteral("makeCompoundPath"), QStringLiteral("Make"), QKeySequence(Qt::CTRL | Qt::Key_8), [this] { session().makeCompoundPath(); });
    add(compound, QStringLiteral("releaseCompoundPath"), QStringLiteral("Release"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_8),
        [this] { session().releaseCompoundPath(); });
    QMenu *clipping = object->addMenu(QStringLiteral("Clipping Mask"));
    clipping->menuAction()->setObjectName(QStringLiteral("clippingMenu"));
    add(clipping, QStringLiteral("makeClippingMask"), QStringLiteral("Make"), QKeySequence(Qt::CTRL | Qt::Key_7), [this] { session().makeClippingMask(); });
    add(clipping, QStringLiteral("releaseClippingMask"), QStringLiteral("Release"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_7),
        [this] { session().releaseClippingMask(); });
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
    object->addSeparator();
    add(object, QStringLiteral("artboardSize"), QStringLiteral("Artboard Size…"), QKeySequence(), [this] { ObjectDialogs::artboardSize(session(), &m_window); });
    QMenu *type = bar.addMenu(QStringLiteral("&Type"));
    add(type, QStringLiteral("createOutlines"), QStringLiteral("Create Outlines"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
        [this] { session().convertTextToPaths(); });
}

void Menus::buildViewAndWindow(QMenuBar &bar)
{
    QMenu *view = bar.addMenu(QStringLiteral("&View"));
    add(view, QStringLiteral("zoomIn"), QStringLiteral("Zoom In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { session().zoomIn(); });
    add(view, QStringLiteral("zoomOut"), QStringLiteral("Zoom Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { session().zoomOut(); });
    add(view, QStringLiteral("fitArtboard"), QStringLiteral("Fit Artboard in Window"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { session().zoomToFit(); });
    add(view, QStringLiteral("actualSize"), QStringLiteral("Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] { session().actualSize(); });
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
    QMenu *window = bar.addMenu(QStringLiteral("&Window"));
    // F7 is Illustrator's; no remap covers function keys.
    QAction *layers = add(window, QStringLiteral("showLayers"), QStringLiteral("Layers"), QKeySequence(Qt::Key_F7), [this] {
        ContentView::setShowsPanel(ContentView::layersKey, !ContentView::showsPanel(ContentView::layersKey));
        emit layersToggled(ContentView::showsPanel(ContentView::layersKey));
        synchronize();
    });
    layers->setCheckable(true);
    QAction *properties = add(window, QStringLiteral("showProperties"), QStringLiteral("Properties"), QKeySequence(), [this] {
        ContentView::setShowsPanel(ContentView::propertiesKey, !ContentView::showsPanel(ContentView::propertiesKey));
        emit propertiesToggled(ContentView::showsPanel(ContentView::propertiesKey));
        synchronize();
    });
    properties->setCheckable(true);
    add(window, QStringLiteral("showSwatches"), QStringLiteral("Swatches"), QKeySequence(), [this] {
        if (m_agent)
            m_agent->showSwatchesPanel();
    })->setEnabled(m_agent != nullptr);
    QMenu *help = bar.addMenu(QStringLiteral("&Help"));
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

void Menus::watchFront(EditorCanvas *canvas)
{
    // The front tab's session and canvas drive every entry.
    disconnect(m_sessionWatch);
    m_sessionWatch = connect(&session(), &EditorSession::changed, this, &Menus::synchronize);
    disconnect(m_canvasWatch);
    m_canvas = canvas;
    if (m_canvas)
        m_canvasWatch = connect(m_canvas, &EditorCanvas::textEditingChanged, this, &Menus::synchronize);
    synchronize();
}

void Menus::focusMoved(QWidget *, QWidget *to)
{
    // The menu bar borrows focus; the field keeps Undo.
    if (qobject_cast<QMenuBar *>(to) == nullptr)
        m_field = qobject_cast<QLineEdit *>(to);
    synchronize();
}
