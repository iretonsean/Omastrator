#pragma once
#include "Anywhere/AnywhereSettings.h"
#include <QJsonArray>
#include <QString>

// The floating contextual bar, OS-wide (docs/ANYWHERE.md): the likeliest
// actions for what is hovered or selected, an Ask field, and up to three
// suggestions tuned by the onboarding answers. Labels stay literal (HUMOR.md).
namespace Bar {
// What the bar is next to: "web" (an element of a page in Omastrator's browser), "window", "browser" (another
// browser's window), "desktop", or "art:<kind>" with the in-app task bar's kind ("art:path", "art:group", …).
QString kindOf(const QString &surfaceKind, bool otherBrowser);
// Each action: {id, label, tip}; `enabled: false` with the reason as the tip where it can't run yet.
QJsonArray actions(const QString &kind);
// 1–3 chips {id, label, action, prompt?, ai}; fewer with AI kept quiet, none of the AI ones.
QJsonArray suggestions(const QString &kind, const AnywhereSettings::Answers &answers);
// The Ask field's placeholder for this kind.
QString askPlaceholder(const QString &kind);
}
