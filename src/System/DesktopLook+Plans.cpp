#include "Agent/Setup.h"
#include "System/ConfigBackup.h"
#include "System/DesktopLook.h"
#include "System/OmarchyThemes.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

// What an edit of the desktop's look previews live, and the plan that writes
// it: which files, the commands after them, the backup and what Revert runs.

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

const QStringList &hyprKeys()
{
    static const QStringList keys{"gapsIn", "gapsOut", "borderSize", "rounding", "activeBorder", "inactiveBorder"};
    return keys;
}

const QStringList &barFileKeys()
{
    static const QStringList keys{"barPosition", "barTransparent", "barLayout"};
    return keys;
}

const QStringList &shellTomlKeys()
{
    static const QStringList keys{"barHeight", "barBackground", "barText", "textSize"};
    return keys;
}

bool hasAny(const QJsonObject &edits, const QStringList &keys)
{
    return std::any_of(keys.begin(), keys.end(), [&](const QString &key) { return edits.contains(key); });
}

QJsonObject only(const QJsonObject &edits, const QStringList &keys)
{
    QJsonObject result;
    for (const QString &key : keys) {
        if (edits.contains(key))
            result[key] = edits[key];
    }
    return result;
}

QString jsonLiteral(const QJsonValue &value)
{
    const QByteArray array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QString quoted(const QString &text)
{
    return QLatin1Char('"') + text + QLatin1Char('"');
}

bool vertical(const QString &position)
{
    return position == QLatin1String("left") || position == QLatin1String("right");
}

// The bar and font keys of shell.toml set in `toml`.
QByteArray withShellKeys(QByteArray toml, const QJsonObject &edits, const DesktopLook::Look &now)
{
    const QString position = edits["barPosition"].toString(now.barPosition);
    if (edits.contains(QLatin1String("barHeight")))
        toml = DesktopLook::setTomlKey(toml, QStringLiteral("bar"), vertical(position) ? QStringLiteral("size-vertical") : QStringLiteral("size-horizontal"),
                                       QString::number(edits["barHeight"].toInt()));
    if (edits.contains(QLatin1String("barBackground"))) {
        const QColor colour = DesktopLook::parseColor(edits["barBackground"].toString());
        toml = DesktopLook::setTomlKey(toml, QStringLiteral("bar"), QStringLiteral("background"), quoted(colour.name(QColor::HexRgb)));
        toml = DesktopLook::setTomlKey(toml, QStringLiteral("bar"), QStringLiteral("background-alpha"), QString::number(colour.alphaF(), 'g', 3));
    }
    if (edits.contains(QLatin1String("barText")))
        toml = DesktopLook::setTomlKey(toml, QStringLiteral("bar"), QStringLiteral("text"),
                                       quoted(DesktopLook::parseColor(edits["barText"].toString()).name(QColor::HexRgb)));
    if (edits.contains(QLatin1String("textSize")))
        toml = DesktopLook::setTomlKey(toml, QStringLiteral("font"), QStringLiteral("base-size"), QString::number(edits["textSize"].toInt()));
    return toml;
}

std::vector<DesignToken> colorTokens(const QJsonObject &colors)
{
    std::vector<DesignToken> tokens;
    for (auto it = colors.begin(); it != colors.end(); ++it)
        tokens.push_back(DesignToken::color(QStringLiteral("color/") + it.key(), DesktopLook::parseColor(it.value().toString())));
    return tokens;
}

// The Hyprland keys an edit sets, borders from the theme's colours included.
QJsonObject hyprValues(const QJsonObject &edits)
{
    QJsonObject values = only(edits, hyprKeys());
    const QJsonObject colors = edits["colors"].toObject();
    if (colors.contains(QLatin1String("hyprland_active_border")) && !values.contains(QLatin1String("activeBorder")))
        values["activeBorder"] = colors["hyprland_active_border"];
    if (colors.contains(QLatin1String("hyprland_inactive_border")) && !values.contains(QLatin1String("inactiveBorder")))
        values["inactiveBorder"] = colors["hyprland_inactive_border"];
    return values;
}

std::vector<QStringList> hyprPreview(const QJsonObject &values, bool lua)
{
    if (values.isEmpty())
        return {};
    if (lua)
        return {{DesktopLook::hyprctl(), QStringLiteral("eval"), DesktopLook::luaEval(values)}};
    std::vector<QStringList> commands;
    static const QHash<QString, QString> names{{"gapsIn", "general:gaps_in"},       {"gapsOut", "general:gaps_out"},
                                               {"borderSize", "general:border_size"}, {"rounding", "decoration:rounding"},
                                               {"activeBorder", "general:col.active_border"}, {"inactiveBorder", "general:col.inactive_border"}};
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QString value = it.value().isString() ? DesktopLook::hyprColor(DesktopLook::parseColor(it.value().toString())) : QString::number(it.value().toInt());
        commands.push_back({DesktopLook::hyprctl(), QStringLiteral("keyword"), names.value(it.key()), value});
    }
    return commands;
}

QStringList applyTheme(const QByteArray &colors, const QByteArray &shell)
{
    return {DesktopLook::omarchyShell(), QStringLiteral("shell"), QStringLiteral("applyTheme"), QString::fromLatin1(colors.toBase64()),
            QString::fromLatin1(shell.toBase64())};
}

bool temporary(const QString &path)
{
    const QString canonical = QFileInfo(path).absoluteFilePath();
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return canonical.startsWith(QDir::tempPath() + QLatin1Char('/')) || (!runtime.isEmpty() && canonical.startsWith(runtime + QLatin1Char('/')));
}

// Keys of the user's shell.toml that an edit sets, which a preview through the theme can't override.
QStringList pinnedKeys(const QJsonObject &edits, const DesktopLook::Paths &paths)
{
    const QByteArray user = contents(paths.userShellToml()).value_or(QByteArray());
    QStringList pinned;
    const auto pinnedIn = [&](const QString &section, const QString &key) { return !DesktopLook::tomlValue(user, section, key).isEmpty(); };
    if (edits.contains(QLatin1String("barHeight")) && (pinnedIn("bar", "size-horizontal") || pinnedIn("bar", "size-vertical")))
        pinned.append(QStringLiteral("the bar's height"));
    if (edits.contains(QLatin1String("barBackground")) && pinnedIn("bar", "background"))
        pinned.append(QStringLiteral("the bar's background"));
    if (edits.contains(QLatin1String("barText")) && pinnedIn("bar", "text"))
        pinned.append(QStringLiteral("the bar's text colour"));
    if (edits.contains(QLatin1String("textSize")) && pinnedIn("font", "base-size"))
        pinned.append(QStringLiteral("the text size"));
    return pinned;
}
}

namespace DesktopLook {
std::vector<FileWrite> fontWrites(const Paths &paths, const QString &family)
{
    // omarchy-font-set's edits, file by file, as its script makes them.
    std::vector<FileWrite> writes;
    const QString config = paths.home + QStringLiteral("/.config");
    auto edit = [&](const QString &path, const std::function<QString(QString)> &change, bool create = false) {
        const std::optional<QByteArray> before = contents(path);
        if (!before && !create)
            return;
        FileWrite write(path, before, change(QString::fromUtf8(before.value_or(QByteArray()))).toUtf8());
        write.byCommand = true;
        writes.push_back(write);
    };
    edit(config + QStringLiteral("/alacritty/alacritty.toml"), [&](QString text) {
        static const QRegularExpression family_(QStringLiteral(R"re(family = ".*")re"));
        return text.replace(family_, QStringLiteral("family = \"%1\"").arg(family));
    });
    const bool kitty = QFileInfo::exists(config + QStringLiteral("/kitty/kitty.conf")) || !QStandardPaths::findExecutable(QStringLiteral("kitty")).isEmpty();
    if (kitty) {
        edit(config + QStringLiteral("/kitty/kitty.conf"), [&](QString text) {
            static const QRegularExpression line(QStringLiteral(R"(^[ \t]*font_family[ \t]+.*$)"), QRegularExpression::MultilineOption);
            if (line.match(text).hasMatch())
                return text.replace(line, QStringLiteral("font_family ") + family);
            return text + QStringLiteral("\nfont_family %1\n").arg(family);
        }, true);
    }
    edit(config + QStringLiteral("/ghostty/config"), [&](QString text) {
        static const QRegularExpression line(QStringLiteral(R"re(font-family = ".*")re"));
        return text.replace(line, QStringLiteral("font-family = \"%1\"").arg(family));
    });
    edit(config + QStringLiteral("/foot/foot.ini"), [&](QString text) {
        static const QRegularExpression line(QStringLiteral(R"(^font=.*$)"), QRegularExpression::MultilineOption);
        return text.replace(line, QStringLiteral("font=%1:size=9").arg(family));
    });
    edit(config + QStringLiteral("/fontconfig/fonts.conf"), [&](const QString &) {
        return QStringLiteral("<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n<fontconfig>\n  <match target=\"pattern\">\n"
                              "    <test name=\"family\" qual=\"any\">\n      <string>monospace</string>\n    </test>\n"
                              "    <edit name=\"family\" mode=\"prepend_first\" binding=\"strong\">\n      <string>%1</string>\n    </edit>\n"
                              "  </match>\n</fontconfig>\n")
            .arg(family);
    }, true);
    return writes;
}

std::vector<QStringList> previewCommands(const QJsonObject &edits, const Look &now, const Paths &paths)
{
    std::vector<QStringList> commands = hyprPreview(hyprValues(edits), paths.lua());
    // The shell's colours and sizes, through its own theme IPC: nothing is written.
    if (edits.contains(QLatin1String("colors")) || hasAny(edits, shellTomlKeys())) {
        QByteArray colors = contents(paths.themeDirectory() + QStringLiteral("/colors.toml")).value_or(QByteArray());
        if (edits.contains(QLatin1String("colors")))
            colors = OmarchyThemes::writeColors(colors, colorTokens(edits["colors"].toObject()));
        const QByteArray shell = withShellKeys(contents(paths.themeDirectory() + QStringLiteral("/shell.toml")).value_or(QByteArray()), edits, now);
        commands.push_back(applyTheme(colors, shell));
    }
    if (edits.contains(QLatin1String("wallpaper")))
        commands.push_back({omarchyShell(), QStringLiteral("-q"), QStringLiteral("background"), QStringLiteral("set"),
                            QFileInfo(edits["wallpaper"].toString()).absoluteFilePath()});
    return commands;
}

std::vector<QStringList> discardCommands(const QJsonObject &edits, const Look &now, const Paths &paths)
{
    std::vector<QStringList> commands;
    // Hyprland's files haven't changed, so reading them again undoes the preview.
    if (!hyprValues(edits).isEmpty())
        commands.push_back({hyprctl(), QStringLiteral("reload"), QStringLiteral("config-only")});
    if (edits.contains(QLatin1String("colors")) || hasAny(edits, shellTomlKeys()))
        commands.push_back(applyTheme(contents(paths.themeDirectory() + QStringLiteral("/colors.toml")).value_or(QByteArray()),
                                      contents(paths.themeDirectory() + QStringLiteral("/shell.toml")).value_or(QByteArray())));
    if (edits.contains(QLatin1String("wallpaper")) && !now.wallpaper.isEmpty())
        commands.push_back({omarchyShell(), QStringLiteral("-q"), QStringLiteral("background"), QStringLiteral("set"), now.wallpaper});
    return commands;
}

QString previewNote(const QJsonObject &edits, const Paths &paths)
{
    QStringList live, later;
    if (hasAny(edits, hyprKeys()))
        live.append(QStringLiteral("gaps, borders and corners"));
    if (edits.contains(QLatin1String("colors")))
        live.append(QStringLiteral("the theme's colours in the bar and panels"));
    const QStringList pinned = pinnedKeys(edits, paths);
    if (hasAny(edits, shellTomlKeys())) {
        if (pinned.isEmpty())
            live.append(QStringLiteral("the bar's size and colours"));
        else
            later.append(pinned.join(QStringLiteral(", ")) + QStringLiteral(" (~/.config/omarchy/shell.toml sets them, and it wins over a preview)"));
    }
    if (edits.contains(QLatin1String("wallpaper")))
        live.append(QStringLiteral("the wallpaper"));
    if (hasAny(edits, barFileKeys()))
        later.append(QStringLiteral("the bar's position and widget order"));
    if (edits.contains(QLatin1String("font")))
        later.append(QStringLiteral("the font (Omarchy restarts the shell for it)"));
    if (edits.contains(QLatin1String("colors")))
        later.append(QStringLiteral("terminals, GTK apps and the rest of the theme (omarchy theme set remakes them)"));
    QStringList sentences;
    if (!live.isEmpty())
        sentences.append(QStringLiteral("Showing now: %1.").arg(live.join(QStringLiteral(", "))));
    if (!later.isEmpty())
        sentences.append(QStringLiteral("Changes when saved: %1.").arg(later.join(QStringLiteral("; "))));
    return sentences.join(QLatin1Char(' '));
}

SyncPlan savePlan(const QJsonObject &edits, const Look &now, const Paths &paths)
{
    SyncPlan plan;
    plan.title = QStringLiteral("Save Desktop Look");
    plan.destination = QStringLiteral("This desktop's Omarchy config, in %1. Nothing is committed or published.").arg(paths.config);
    if (const QString problem = validate(edits); !problem.isEmpty()) {
        plan.problem = problem;
        return plan;
    }
    std::vector<QStringList> reverts;
    QStringList notes;

    // The theme's colours: the theme folder itself, then Omarchy remakes everything from it.
    if (edits.contains(QLatin1String("colors"))) {
        if (now.theme.isEmpty() || now.themeDirectory.isEmpty()) {
            plan.problem = QStringLiteral("There's no current Omarchy theme to change (%1/theme.name is missing).").arg(paths.state);
            return plan;
        }
        const bool mine = QFileInfo(now.themeDirectory).absoluteFilePath().startsWith(QDir::homePath() + QStringLiteral("/.config/omarchy/themes/"));
        const QString name = mine ? now.theme : now.theme + QStringLiteral(" Edited");
        SyncPlan theme = OmarchyThemes::savePlan(now.themeDirectory, name, colorTokens(edits["colors"].toObject()), true);
        if (!theme.problem.isEmpty()) {
            plan.problem = theme.problem;
            return plan;
        }
        for (FileWrite &write : theme.writes)
            plan.writes.push_back(write);
        plan.commands.push_back(theme.command);
        reverts.push_back({OmarchyThemes::omarchy(), QStringLiteral("theme"), QStringLiteral("set"), OmarchyThemes::slug(now.theme)});
        notes.append(QStringLiteral("omarchy theme set then remakes the current theme's files in %1 and reloads what uses them.").arg(paths.themeDirectory()));
        if (!mine)
            notes.append(QStringLiteral("“%1” is one of Omarchy's own themes, so the colours go in a copy called “%2”.").arg(now.theme, name));
    }

    // Gaps, borders and corners: Omastrator's block at the end of looknfeel, loaded after the theme.
    const QJsonObject hypr = only(edits, hyprKeys());
    if (!hypr.isEmpty()) {
        const QString path = paths.looknfeel();
        const std::optional<QByteArray> before = contents(path);
        QJsonObject merged = blockValues(before.value_or(QByteArray()));
        for (auto it = hypr.begin(); it != hypr.end(); ++it)
            merged[it.key()] = it.value();
        const QByteArray block = paths.lua() ? luaBlock(merged) : confBlock(merged);
        plan.writes.emplace_back(path, before, withBlock(before.value_or(QByteArray()), block, paths.lua()));
        plan.commands.push_back({hyprctl(), QStringLiteral("reload"), QStringLiteral("config-only")});
        reverts.push_back({hyprctl(), QStringLiteral("reload"), QStringLiteral("config-only")});
        if (hypr.contains(QLatin1String("activeBorder")) || hypr.contains(QLatin1String("inactiveBorder")))
            notes.append(QStringLiteral("Border colours set here stay through theme switches until they're reverted."));
    }

    // The bar's position, transparency and widgets: shell.json, edited with jq so its layout and key order stay.
    const QJsonObject bar = only(edits, barFileKeys());
    if (!bar.isEmpty()) {
        const std::optional<QByteArray> before = contents(paths.shellJson());
        const QByteArray source = before && !before->trimmed().isEmpty()
            ? *before
            : contents(paths.omarchy + QStringLiteral("/config/omarchy/shell.json")).value_or(QByteArrayLiteral("{}"));
        QStringList filter;
        if (bar.contains(QLatin1String("barPosition")))
            filter.append(QStringLiteral(".bar.position = ") + jsonLiteral(bar["barPosition"]));
        if (bar.contains(QLatin1String("barTransparent")))
            filter.append(QStringLiteral(".bar.transparent = ") + jsonLiteral(bar["barTransparent"]));
        if (bar.contains(QLatin1String("barLayout")))
            filter.append(QStringLiteral(".bar.layout = ") + jsonLiteral(reorderLayout(now.barLayout, bar["barLayout"].toObject())));
        QString error;
        const std::optional<QByteArray> after = Setup::jq(source, filter.join(QStringLiteral(" | ")), &error);
        if (!after) {
            plan.problem = error;
            return plan;
        }
        plan.writes.emplace_back(paths.shellJson(), before, *after);
        plan.commands.push_back({omarchyShell(), QStringLiteral("shell"), QStringLiteral("reloadConfig")});
        reverts.push_back({omarchyShell(), QStringLiteral("shell"), QStringLiteral("reloadConfig")});
    }

    // The bar's size and colours and the text size: the machine-level shell.toml, which the shell watches.
    if (hasAny(edits, shellTomlKeys())) {
        const std::optional<QByteArray> before = contents(paths.userShellToml());
        plan.writes.emplace_back(paths.userShellToml(), before, withShellKeys(before.value_or(QByteArray()), edits, now));
        notes.append(QStringLiteral("The bar's size, colours and the text size live in ~/.config/omarchy/shell.toml, so they stay through theme switches."));
    }

    // The font: Omarchy's own command, which writes these files itself.
    if (edits.contains(QLatin1String("font"))) {
        const QString family = edits["font"].toString().trimmed();
        for (const FileWrite &write : fontWrites(paths, family))
            plan.writes.push_back(write);
        plan.commands.push_back({OmarchyThemes::omarchy(), QStringLiteral("font"), QStringLiteral("set"), family});
        reverts.push_back({OmarchyThemes::omarchy(), QStringLiteral("restart"), QStringLiteral("shell")});
        notes.append(QStringLiteral("Ghostty and Foot show the new font once they're restarted."));
    }

    // The wallpaper: Omarchy's link to it. A picture made from an artboard is kept first.
    if (edits.contains(QLatin1String("wallpaper"))) {
        QString image = QFileInfo(edits["wallpaper"].toString()).absoluteFilePath();
        if (temporary(image)) {
            const QString kept = QDir(paths.wallpapers()).filePath(QFileInfo(image).fileName());
            plan.writes.emplace_back(kept, contents(kept), QByteArray(), image);
            image = kept;
        }
        FileWrite link;
        link.path = paths.background();
        link.linkTo = image;
        link.linkBefore = QFileInfo(paths.background()).symLinkTarget();
        link.byCommand = true;
        plan.writes.push_back(link);
        plan.commands.push_back({OmarchyThemes::omarchy(), QStringLiteral("theme"), QStringLiteral("bg"), QStringLiteral("set"), image});
        if (!now.wallpaper.isEmpty())
            reverts.push_back({omarchyShell(), QStringLiteral("-q"), QStringLiteral("background"), QStringLiteral("set"), now.wallpaper});
    }

    plan.backupFolder = ConfigBackup::newFolder(QStringLiteral("desktop look"));
    plan.revertCommands = reverts;
    notes.append(QStringLiteral("Revert puts every file back from the backup."));
    plan.note = notes.join(QLatin1Char(' '));
    return plan;
}
}
