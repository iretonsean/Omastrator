#pragma once
#include "Document/EditorSession.h"
#include <QFrame>

// Character's OpenType features: ligatures, alternates, small caps, fractions,
// ordinals, figures and stylistic sets, for the selection or the characters
// selected. Features the font lacks are dimmed. It floats under its button
// and goes away with a click elsewhere.
namespace OpenTypePopover {
QFrame *show(EditorSession &session, QWidget *anchor);
}
