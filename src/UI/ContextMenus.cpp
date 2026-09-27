#include "UI/ContextMenus.h"
#include "UI/Menus.h"
#include "UI/NativeLayerList.h"
#include "UI/PanelIcons.h"

namespace {
PanelIcon kindIcon(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::layer: return PanelIcon::layer;
    case ObjectKind::group: return PanelIcon::group;
    case ObjectKind::path: return PanelIcon::path;
    case ObjectKind::text: return PanelIcon::text;
    case ObjectKind::image: return PanelIcon::image;
    }
    return PanelIcon::path;
}

// A menu bar entry, when it exists and could run now; context menus leave out what can't apply.
void share(QMenu *menu, const Menus *menus, const char *name)
{
    if (!menus)
        return;
    if (QAction *entry = menus->action(QString::fromLatin1(name)); entry && entry->isEnabled())
        menu->addAction(entry);
}

QAction *local(QMenu *menu, const QString &name, const QString &text, std::function<void()> run)
{
    QAction *entry = menu->addAction(text);
    entry->setObjectName(name);
    QObject::connect(entry, &QAction::triggered, menu, std::move(run));
    return entry;
}

// A submenu that only stays when something in it applies.
QMenu *submenu(QMenu *menu, const QString &name, const QString &title)
{
    QMenu *made = menu->addMenu(title);
    made->menuAction()->setObjectName(name);
    return made;
}

void dropEmpty(QMenu *menu)
{
    for (QAction *entry : menu->actions()) {
        if (entry->menu() && entry->menu()->actions().isEmpty())
            menu->removeAction(entry);
    }
}

bool selectionHas(const EditorSession &session, ObjectKind kind)
{
    for (const QUuid &id : session.selectedLeaves()) {
        if (session.document()->find(id)->kind == kind)
            return true;
    }
    return false;
}

void emptyCanvas(QMenu *menu, Menus &menus)
{
    share(menu, &menus, "generate");
    menu->addSeparator();
    for (const char *name : {"paste", "pasteInPlace", "selectAll"})
        share(menu, &menus, name);
    menu->addSeparator();
    for (const char *name : {"fitArtboard", "actualSize"})
        share(menu, &menus, name);
    menu->addSeparator();
    for (const char *name : {"showGrid", "snapToGrid", "outline"})
        share(menu, &menus, name);
    menu->addSeparator();
    share(menu, &menus, "artboardSize");
}
}

QMenu *ContextMenus::forCanvas(Menus &menus, EditorSession &session, EditorCanvas &canvas, const QList<QUuid> &underPointer, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("canvasContextMenu"));
    if (!session.hasDocument())
        return menu;
    const VectorDocument &document = *session.document();
    // Stacked objects: pick any one of them, topmost first (Figma's Select layer, Paper's ⌘-right-click).
    const auto layerPicker = [&] {
        if (underPointer.size() < 2)
            return;
        QMenu *picker = submenu(menu, QStringLiteral("selectLayerMenu"), QStringLiteral("Select Layer"));
        const QColor ink = menu->palette().color(QPalette::WindowText);
        for (const QUuid &id : underPointer) {
            const VectorObject *object = document.find(id);
            if (!object)
                continue;
            QAction *entry = local(picker, QStringLiteral("selectLayer"), object->name.isEmpty() ? rawValue(object->kind) : object->name,
                                   [&session, id] { session.select({id}); });
            entry->setIcon(PanelIcons::pixmap(kindIcon(object->kind), 16, ink, menu->devicePixelRatio()));
            entry->setCheckable(true);
            entry->setChecked(session.isSelected(id));
        }
    };
    if (!session.hasSelection()) {
        layerPicker();
        emptyCanvas(menu, menus);
        dropEmpty(menu);
        return menu;
    }
    // AI first: the right-click already scoped Edit with Instruction to what was clicked.
    if (QAction *instruct = menus.action(QStringLiteral("editWithInstruction")); instruct && instruct->isEnabled())
        local(menu, QStringLiteral("askAI"), QStringLiteral("Ask AI…"), [instruct] { instruct->trigger(); });
    layerPicker();
    menu->addSeparator();
    for (const char *name : {"cut", "copy", "paste", "pasteInFront", "pasteInBack", "duplicate", "delete"})
        share(menu, &menus, name);
    menu->addSeparator();
    // What this kind of selection is for.
    const std::vector<QUuid> &selected = session.selection();
    const VectorObject *single = selected.size() == 1 ? document.find(selected.front()) : nullptr;
    if (selected.size() >= 2)
        share(menu, &menus, "group");
    if (single && single->kind == ObjectKind::group && !single->isClipGroup)
        local(menu, QStringLiteral("isolateGroup"), QStringLiteral("Isolate Selected Group"), [&canvas, id = single->id] { canvas.isolateGroup(id); });
    share(menu, &menus, "ungroup");
    if (canvas.isolatedGroup())
        local(menu, QStringLiteral("exitIsolation"), QStringLiteral("Exit Isolation Mode"), [&canvas] { canvas.exitIsolation(); });
    if (single && single->isClipGroup)
        share(menu, &menus, "releaseClippingMask");
    else if (selected.size() >= 2)
        share(menu, &menus, "makeClippingMask");
    if (selectionHas(session, ObjectKind::text))
        share(menu, &menus, "createOutlines");
    if (session.selectedImage()) {
        share(menu, &menus, "imageTraceMake");
        share(menu, &menus, "vectorizeWithAI");
    }
    menu->addSeparator();
    QMenu *arrange = submenu(menu, QStringLiteral("contextArrange"), QStringLiteral("Arrange"));
    for (const char *name : {"bringToFront", "bringForward", "sendBackward", "sendToBack"})
        share(arrange, &menus, name);
    QMenu *transform = submenu(menu, QStringLiteral("contextTransform"), QStringLiteral("Transform"));
    for (const char *name : {"transformAgain", "flipHorizontal", "flipVertical", "moveDialog", "rotateDialog", "reflectDialog", "scaleDialog"})
        share(transform, &menus, name);
    if (selected.size() >= 2) {
        QMenu *align = submenu(menu, QStringLiteral("contextAlign"), QStringLiteral("Align"));
        const std::vector<std::pair<const char *, AlignEdge>> edges{
            {"Left", AlignEdge::left}, {"Horizontal Center", AlignEdge::horizontalCenter}, {"Right", AlignEdge::right},
            {"Top", AlignEdge::top},   {"Vertical Center", AlignEdge::verticalCenter},     {"Bottom", AlignEdge::bottom}};
        for (const auto &[text, edge] : edges)
            local(align, QStringLiteral("contextAlign%1").arg(QString::fromLatin1(text).remove(QLatin1Char(' '))), QString::fromLatin1(text),
                  [&session, edge] { session.align(edge); });
    }
    if (session.canCombine()) {
        QMenu *pathfinder = submenu(menu, QStringLiteral("contextPathfinder"), QStringLiteral("Pathfinder"));
        const std::vector<std::pair<const char *, BooleanOperation>> operations{
            {"Unite", BooleanOperation::unite}, {"Minus Front", BooleanOperation::minusFront},
            {"Intersect", BooleanOperation::intersect}, {"Exclude", BooleanOperation::exclude}};
        for (const auto &[text, operation] : operations)
            local(pathfinder, QStringLiteral("contextPathfinder%1").arg(QString::fromLatin1(text).remove(QLatin1Char(' '))), QString::fromLatin1(text),
                  [&session, operation] { session.combineSelection(operation); });
        share(pathfinder, &menus, "makeCompoundPath");
    }
    if (selectionHas(session, ObjectKind::path)) {
        QMenu *path = submenu(menu, QStringLiteral("contextPath"), QStringLiteral("Path"));
        for (const char *name : {"outlineStroke", "offsetPath", "simplify", "releaseCompoundPath"})
            share(path, &menus, name);
    }
    QMenu *same = submenu(menu, QStringLiteral("contextSelectSame"), QStringLiteral("Select Same"));
    for (const char *name : {"selectSameFillAndStroke", "selectSameFillColor", "selectSameStrokeColor", "selectSameStrokeWeight", "selectSameOpacity",
                             "selectSameBlendMode", "selectSameFontFamily", "selectSameFontFamilyStyleSize"})
        share(same, &menus, name);
    menu->addSeparator();
    for (const char *name : {"lockSelection", "hideSelection"})
        share(menu, &menus, name);
    dropEmpty(menu);
    return menu;
}

QMenu *ContextMenus::forLayerRow(Menus *menus, EditorSession &session, NativeLayerList &list, const QUuid &row, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("layerContextMenu"));
    const VectorObject *object = session.document() ? session.document()->find(row) : nullptr;
    if (!object)
        return menu;
    const bool layer = object->kind == ObjectKind::layer;
    local(menu, QStringLiteral("layerRename"), QStringLiteral("Rename"), [&list, row] {
        for (LayerCell *cell : list.cells()) {
            if (cell->objectID() == row)
                cell->beginRenaming();
        }
    });
    if (layer) {
        local(menu, QStringLiteral("layerDuplicate"), QStringLiteral("Duplicate Layer"), [&session, row] { session.duplicateLayer(row); });
        local(menu, QStringLiteral("layerDelete"), QStringLiteral("Delete Layer"), [&session, row] { session.deleteObjects({row}); });
    } else {
        share(menu, menus, "duplicate");
        local(menu, QStringLiteral("layerDelete"), QStringLiteral("Delete"), [&session, row] { session.deleteObjects({row}); });
    }
    local(menu, QStringLiteral("layerNew"), QStringLiteral("New Layer"), [&session] { session.addLayer(); });
    menu->addSeparator();
    local(menu, QStringLiteral("layerVisibility"), object->isVisible ? QStringLiteral("Hide") : QStringLiteral("Show"),
          [&session, row, shown = object->isVisible] { session.setVisible(row, !shown); });
    local(menu, QStringLiteral("layerLocking"), object->isLocked ? QStringLiteral("Unlock") : QStringLiteral("Lock"),
          [&session, row, locked = object->isLocked] { session.setLocked(row, !locked); });
    const bool othersShown = session.anyOtherVisible(row), othersOpen = session.anyOtherUnlocked(row);
    local(menu, QStringLiteral("layerHideOthers"), othersShown ? QStringLiteral("Hide Others") : QStringLiteral("Show Others"),
          [&session, row, othersShown] { session.setOthersVisible(row, !othersShown); });
    local(menu, QStringLiteral("layerLockOthers"), othersOpen ? QStringLiteral("Lock Others") : QStringLiteral("Unlock Others"),
          [&session, row, othersOpen] { session.setOthersLocked(row, othersOpen); });
    menu->addSeparator();
    if (!layer) {
        for (const char *name : {"group", "ungroup", "makeClippingMask", "releaseClippingMask"})
            share(menu, menus, name);
    }
    if (object->isContainer() && !session.document()->children(row).empty())
        local(menu, QStringLiteral("layerSelectChildren"), QStringLiteral("Select Children"), [&session, row] {
            std::vector<QUuid> children;
            for (const QUuid &child : session.document()->children(row)) {
                if (session.document()->isEffectivelyVisible(child) && !session.document()->isEffectivelyLocked(child))
                    children.push_back(child);
            }
            session.select(children);
        });
    local(menu, QStringLiteral("layerLocate"), QStringLiteral("Locate on Canvas"), [&session, row, layer] {
        if (!layer)
            session.select({row});
        session.zoomToRect(session.document()->bounds(row, true));
    });
    if (layer) {
        QMenu *colors = submenu(menu, QStringLiteral("layerColorMenu"), QStringLiteral("Layer Color"));
        const std::array names{"Blue", "Red", "Green", "Violet", "Orange", "Cyan"};
        for (int index = 0; index < int(names.size()); ++index) {
            const QColor color = nextLayerColor(index);
            QPixmap swatch(12, 12);
            swatch.fill(color);
            QAction *entry = local(colors, QStringLiteral("layerColor"), QString::fromLatin1(names[size_t(index)]),
                                   [&session, row, color] { session.setLayerColor(row, color); });
            entry->setIcon(swatch);
            entry->setCheckable(true);
            entry->setChecked(object->layerColor == color);
        }
    }
    dropEmpty(menu);
    return menu;
}
