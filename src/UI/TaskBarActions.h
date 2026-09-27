#pragma once
#include "Canvas/TaskBar.h"
#include "Document/EditorSession.h"
#include <QString>

class AgentBridge;
class EditorCanvas;
class Menus;

// What the contextual task bar offers for each kind of selection: the menu bar's own actions, then Ask AI… and More.
namespace TaskBarActions {
// "path", "paths", "objects", "text:point", "text:area", "image", "group" or "clipGroup"; empty with nothing to offer.
QString kind(const EditorSession &session);
// A bar on `canvas`, filled for its selection and held back while an agent edits.
TaskBar *attach(Menus &menus, AgentBridge *agent, EditorCanvas &canvas);
}
