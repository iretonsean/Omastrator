#pragma once
#include "Canvas/EditorCanvas.h"
#include <QList>
#include <QMenu>

class Menus;
class NativeLayerList;

// Right-click menus. They reuse the menu bar's actions, so shortcuts and remaps show,
// and list only what applies to what was clicked, the likeliest first.
namespace ContextMenus {
// The canvas's menu for the selection the right-click left, or for the empty canvas.
// `underPointer` (topmost first) fills the Select picker when objects are stacked there.
QMenu *forCanvas(Menus &menus, EditorSession &session, EditorCanvas &canvas, const QList<QUuid> &underPointer, QWidget *parent);
// A Layers row's menu; without `menus` (no window) the menu bar's entries are left out.
QMenu *forLayerRow(Menus *menus, EditorSession &session, NativeLayerList &list, const QUuid &row, QWidget *parent);
// The six align entries, for two or more objects; the task bar's Align menu too.
void addAlign(QMenu *menu, EditorSession &session);
// Unite, Minus Front, Intersect, Exclude and Make Compound Path, when the selection can combine.
void addPathfinder(QMenu *menu, Menus &menus, EditorSession &session);
}
