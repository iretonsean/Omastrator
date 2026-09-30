#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QColor>
#include <QString>
#include <QUuid>
#include <functional>
#include <optional>

class AgentBridge;
class ElementBar;
class EditorCanvas;

// What the element bar offers for the page elements picked in Edit Page (docs/LIVE-IN-FRAME.md, section 3): text,
// colours, spacing, size, type and radius, then Ask… and More. Each change is Live's own edit, snapped to a token.
namespace ElementBarActions {
ElementBar *attach(AgentBridge *agent, EditorCanvas &canvas);
// Tests say which project a frame's Live runs on; empty means the site isn't the user's. Pass {} to reset.
void setProjectResolver(std::function<QString(const QUuid &frame)> resolver);

// The number in a computed length such as "16px" or "700"; nullopt for auto, normal and the like.
std::optional<double> numberOf(const QString &css);
// The rgba() or #rrggbb computed colour as a QColor; invalid for transparent or unknown forms.
QColor colorOf(const QString &css);
// The picked elements' value of one style property: the shared value, or mixed. Empty and not mixed with no elements.
struct Common {
    QString value;
    bool mixed = false;
};
Common common(const QJsonArray &selection, const QString &property);
// The controls the picked elements call for, as text: it changes only when the controls have to be rebuilt.
QString signature(const QJsonArray &selection);
}
