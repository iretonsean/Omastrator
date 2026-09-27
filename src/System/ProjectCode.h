#pragma once
#include "Document/DesignTokens.h"
#include "System/SyncPlan.h"
#include <QString>
#include <vector>

// A design system in a project's code: tokens.json, Tailwind (v4 CSS, v3 config)
// and CSS custom properties (docs/DESIGN-SYSTEMS.md).
namespace ProjectCode {
struct Source {
    enum class Kind { w3c, tailwind, tailwindConfig, css };
    Kind kind;
    QString path;
    // "tokens.json (W3C design tokens)".
    QString label() const;
};
// The token files in `folder`: tokens.json and friends, CSS with @theme or :root variables (not in
// node_modules, dist or build), and tailwind.config.*.
std::vector<Source> detect(const QString &folder);

struct Pulled {
    std::vector<DesignToken> tokens;
    QStringList modes;
    QStringList reads;
    QStringList skipped;
};
// Every source read and merged by name: the Tailwind config first, then CSS, then Tailwind v4, then tokens.json.
Pulled read(const std::vector<Source> &sources);

// Writes the tokens to every writable source found (the v3 config is read only), or a new tokens.json at the
// folder's top when there's none. Commits to the folder's repository when it's in one.
SyncPlan pushPlan(const QString &folder, const std::vector<DesignToken> &tokens, const QStringList &modes);
// Reads the project; `apply` receives what was read when the plan runs.
SyncPlan pullPlan(const QString &folder, const QString &documentName, std::function<QString(const Pulled &)> apply);
}
