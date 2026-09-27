#pragma once
#include "Document/DesignTokens.h"
#include "System/SyncPlan.h"
#include <QString>
#include <vector>

// Omarchy themes as design systems (docs/DESIGN-SYSTEMS.md): a theme's
// colors.toml and shell.spacing.toml read as tokens, edited, saved as a theme
// folder under ~/.config/omarchy/themes, and applied with `omarchy theme set`.
namespace OmarchyThemes {
// $OMASTRATOR_OMARCHY, else omarchy.
QString omarchy();
// The theme in use: ~/.local/state/omarchy/current/theme.name, else nothing.
QString currentName();
// The current theme's files: ~/.local/state/omarchy/current/theme.
QString currentDirectory();
// A theme's folder: the user's copy in ~/.config/omarchy/themes, else Omarchy's own.
QString directoryOf(const QString &name);
// "Tokyo Night" → "tokyo-night".
QString slug(const QString &name);

struct Theme {
    QString name;
    QString directory;
    std::vector<DesignToken> tokens;
    // colors.toml's `mode`: "dark" or "light".
    QString mode;
};
// Colours from colors.toml (every quoted value that is a colour) as color/<key>, spacing from shell.spacing.toml.
Theme read(const QString &directory, const QString &name = {});
// colors.toml with the colour tokens' values in place; new colours are added at the end.
QByteArray writeColors(const QByteArray &toml, const std::vector<DesignToken> &tokens);
QByteArray writeSpacing(const QByteArray &toml, const std::vector<DesignToken> &tokens);

// A theme folder called `name` made from `source` with `tokens` written in, under ~/.config/omarchy/themes.
// A user theme of that name is changed in place. With `apply`, `omarchy theme set` switches the desktop to it.
SyncPlan savePlan(const QString &source, const QString &name, const std::vector<DesignToken> &tokens, bool apply);
// Reads a theme into the document; `apply` runs when confirmed.
SyncPlan pullPlan(const QString &directory, const QString &name, const QString &documentName, std::function<QString(const Theme &)> apply);
}
