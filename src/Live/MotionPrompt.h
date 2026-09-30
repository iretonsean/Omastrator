#pragma once
#include "Live/AgentWork.h"
#include "Live/MotionStack.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

// What the agent is told when the designer asks for motion (docs/MOTION.md, section 4): the selection, the page's tokens, the
// project's stack, the motion already on those elements, and the output contract the result is checked against.
namespace MotionPrompt {
struct Brief {
    QString instruction;
    // Each selected element: selector, tag, classes, text, box, styles and markup (as `info()` reports them).
    QJsonArray elements;
    // The page's tokens as the overlay's chips have them (`TokenSet::toJson`), the motion ones among them.
    QJsonObject tokens;
    // The motion already on the selection ([{selector, name, kind, duration, delay, easing, properties}]), and every @keyframes
    // name the page uses, so a new one doesn't clash.
    QJsonArray motion;
    QStringList keyframeNames;
    MotionStack::Info stack;
    // The frame's picture saved as a PNG, and how wide the page is drawn.
    QString screenshot;
    QString width;
    QString url;
    QString command;
    // A reduced-motion rule is part of the result unless the designer turned it off for this run.
    bool reducedMotion = true;
    // Several elements animated as one group, with one prompt (docs/MOTION.md, section 5).
    bool together = false;
    // The short name that starts a new @keyframes name: "nl-rise".
    QString prefix = QStringLiteral("oma");
};

// The prompt, for the worktree in `work`.
QString animate(const AgentWork &work, const Brief &brief);
// A site's short name for @keyframes names: the first letters of its words ("Northlight Coffee" is "nc"), or the first two of one.
QString keyframePrefix(const QString &siteName);
// The name the prefix comes from: the page's own site ("northlight" for www.northlight.example), or `folderName` for an address that
// names no site (an IP address, localhost), since "127.0.0.1" and "localhost" are nobody's.
QString siteName(const QUrl &page, const QString &folderName);
// The marker lines every block is written between.
QString startMarker(const QString &name);
QString endMarker();
}
