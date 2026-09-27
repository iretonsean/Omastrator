#include "System/DesktopLook.h"
#include "Agent/Hyprland.h"
#include "System/OmarchyThemes.h"
#include "System/TokenFiles.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

// Reading the desktop's look, and the text formats it's written in: the
// Hyprland block, TOML keys and the bar's layout. The plans are in
// DesktopLook+Plans.cpp.

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

QString envOr(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

// "#rrggbb", or "#rrggbbaa" when it isn't opaque (CSS order).
QString hexOf(const QColor &color)
{
    QString text = color.name(QColor::HexRgb);
    if (color.alpha() != 255)
        text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    return text;
}

// The first `key = number` in Lua or hyprlang text, skipping comments (a later one is often a group bar's).
std::optional<int> firstNumber(const QString &text, const QString &key)
{
    const QRegularExpression pattern(QStringLiteral(R"(^[ \t]*%1[ \t]*=[ \t]*(-?\d+))").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption);
    if (const auto match = pattern.match(text); match.hasMatch())
        return match.captured(1).toInt();
    return std::nullopt;
}

// The first colour of the first `active_border…` assignment: a string, a gradient table or hyprlang's list.
std::optional<QColor> firstBorder(const QString &text, const QString &which)
{
    const QRegularExpression lua(
        QStringLiteral(R"re(^[ \t]*(?:local[ \t]+)?%1_border(?:_color)?[ \t]*=[ \t]*(?:\{[^"\n]*)?"([^"]+)")re").arg(which),
        QRegularExpression::MultilineOption);
    const QRegularExpression conf(QStringLiteral(R"(^[ \t]*col\.%1_border[ \t]*=[ \t]*(\S+))").arg(which), QRegularExpression::MultilineOption);
    for (const QRegularExpression *pattern : {&lua, &conf}) {
        if (const auto match = pattern->match(text); match.hasMatch())
            return DesktopLook::parseHyprColor(match.captured(1));
    }
    return std::nullopt;
}

}

namespace DesktopLook {
Paths Paths::current()
{
    Paths paths;
    paths.home = QDir::homePath();
    paths.config = envOr("XDG_CONFIG_HOME", paths.home + QStringLiteral("/.config"));
    paths.data = envOr("XDG_DATA_HOME", paths.home + QStringLiteral("/.local/share"));
    paths.state = paths.home + QStringLiteral("/.local/state/omarchy/current");
    paths.omarchy = envOr("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
    return paths;
}

bool Paths::lua() const
{
    return QFileInfo::exists(config + QStringLiteral("/hypr/hyprland.lua"));
}

QString Paths::looknfeel() const
{
    if (lua())
        return config + QStringLiteral("/hypr/looknfeel.lua");
    const QString conf = config + QStringLiteral("/hypr/looknfeel.conf");
    return QFileInfo::exists(conf) ? conf : config + QStringLiteral("/hypr/hyprland.conf");
}

QString Paths::shellJson() const
{
    return config + QStringLiteral("/omarchy/shell.json");
}

QString Paths::userShellToml() const
{
    return config + QStringLiteral("/omarchy/shell.toml");
}

QString Paths::themeDirectory() const
{
    return state + QStringLiteral("/theme");
}

QString Paths::themeName() const
{
    return QString::fromUtf8(contents(state + QStringLiteral("/theme.name")).value_or(QByteArray())).trimmed();
}

QString Paths::background() const
{
    return state + QStringLiteral("/background");
}

QString Paths::fontsConf() const
{
    // omarchy font set writes $HOME/.config, whatever XDG_CONFIG_HOME says.
    return home + QStringLiteral("/.config/fontconfig/fonts.conf");
}

QString Paths::wallpapers() const
{
    return data + QStringLiteral("/omastrator/wallpapers");
}

QString hyprctl()
{
    return envOr("OMASTRATOR_HYPRCTL", QStringLiteral("hyprctl"));
}

QString omarchyShell()
{
    return envOr("OMASTRATOR_OMARCHY_SHELL", QStringLiteral("omarchy-shell"));
}

QJsonObject Look::toJson() const
{
    QJsonObject palette;
    QJsonArray order;
    for (const auto &[key, colour] : colors) {
        palette[key] = hexOf(colour);
        order.append(key);
    }
    return {{"gapsIn", gapsIn},
            {"gapsOut", gapsOut},
            {"borderSize", borderSize},
            {"rounding", rounding},
            {"activeBorder", activeBorder.isValid() ? hexOf(activeBorder) : QString()},
            {"inactiveBorder", inactiveBorder.isValid() ? hexOf(inactiveBorder) : QString()},
            {"barPosition", barPosition},
            {"barTransparent", barTransparent},
            {"barHeight", barHeight},
            {"barBackground", barBackground.isValid() ? hexOf(barBackground) : QString()},
            {"barText", barText.isValid() ? hexOf(barText) : QString()},
            {"barLayout", barLayout},
            {"font", font},
            {"textSize", textSize},
            {"wallpaper", wallpaper},
            {"theme", theme},
            {"themeDirectory", themeDirectory},
            {"colors", palette},
            {"colorOrder", order}};
}

std::optional<QColor> parseHyprColor(const QString &given)
{
    const QString text = given.trimmed();
    static const QRegularExpression rgba(QStringLiteral(R"(rgba\(([0-9a-fA-F]{8})\))"));
    static const QRegularExpression rgb(QStringLiteral(R"(rgb\(([0-9a-fA-F]{6})\))"));
    static const QRegularExpression argb(QStringLiteral(R"(^0x([0-9a-fA-F]{8}))"));
    static const QRegularExpression bare(QStringLiteral(R"(^([0-9a-fA-F]{8})(\s|$))"));
    if (const auto match = rgba.match(text); match.hasMatch()) {
        const QString hex = match.captured(1);
        QColor colour(QLatin1Char('#') + hex.left(6));
        colour.setAlpha(hex.mid(6, 2).toInt(nullptr, 16));
        return colour;
    }
    if (const auto match = rgb.match(text); match.hasMatch())
        return QColor(QLatin1Char('#') + match.captured(1));
    // 0xAARRGGBB, and getoption's gradient ("b30a84ff 0deg"): alpha first.
    for (const QRegularExpression *pattern : {&argb, &bare}) {
        if (const auto match = pattern->match(text); match.hasMatch())
            return QColor(QLatin1Char('#') + match.captured(1));
    }
    if (text.startsWith(QLatin1Char('#')) || text.startsWith(QLatin1String("rgba(")) || text.startsWith(QLatin1String("rgb(")))
        return TokenFiles::parseColor(text);
    return std::nullopt;
}

QColor parseColor(const QString &text)
{
    const QString trimmed = text.trimmed();
    static const QRegularExpression cssAlpha(QStringLiteral(R"(^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})$)"));
    if (const auto match = cssAlpha.match(trimmed); match.hasMatch()) {
        QColor colour(QLatin1Char('#') + match.captured(1));
        colour.setAlpha(match.captured(2).toInt(nullptr, 16));
        return colour;
    }
    if (const auto colour = parseHyprColor(trimmed))
        return *colour;
    return TokenFiles::parseColor(trimmed).value_or(QColor());
}

QString hyprColor(const QColor &color)
{
    return QStringLiteral("rgba(%1%2)").arg(color.name(QColor::HexRgb).mid(1)).arg(color.alpha(), 2, 16, QLatin1Char('0'));
}

QStringList fontFamilies()
{
    QProcess process;
    process.start(QStringLiteral("fc-list"), {QStringLiteral(":spacing=mono"), QStringLiteral("family")});
    if (!process.waitForStarted(2000) || !process.waitForFinished(5000))
        return {};
    QSet<QString> seen;
    for (const QString &line : QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        seen.insert(line.section(QLatin1Char(','), 0, 0).trimmed());
    QStringList families(seen.begin(), seen.end());
    families.sort(Qt::CaseInsensitive);
    return families;
}

Look read(const Paths &paths, bool askHyprland)
{
    Look look;
    // Files first: Omarchy's defaults, the theme, then the user's own, as Hyprland loads them.
    for (const QString &path : {paths.omarchy + QStringLiteral("/default/hypr/looknfeel.lua"), paths.themeDirectory() + QStringLiteral("/hyprland.lua"),
                                paths.themeDirectory() + QStringLiteral("/hyprland.conf"), paths.looknfeel()}) {
        QString hypr = QString::fromUtf8(contents(path).value_or(QByteArray()));
        // In the user's file, Omastrator's block is what Hyprland ends up with.
        if (const qsizetype block = hypr.indexOf(QLatin1String("BEGIN Omastrator: desktop look")); block >= 0)
            hypr = hypr.mid(block) + QLatin1Char('\n') + hypr.left(block);
        look.gapsIn = firstNumber(hypr, QStringLiteral("gaps_in")).value_or(look.gapsIn);
        look.gapsOut = firstNumber(hypr, QStringLiteral("gaps_out")).value_or(look.gapsOut);
        look.borderSize = firstNumber(hypr, QStringLiteral("border_size")).value_or(look.borderSize);
        look.rounding = firstNumber(hypr, QStringLiteral("rounding")).value_or(look.rounding);
        look.activeBorder = firstBorder(hypr, QStringLiteral("active")).value_or(look.activeBorder);
        look.inactiveBorder = firstBorder(hypr, QStringLiteral("inactive")).value_or(look.inactiveBorder);
    }
    // Hyprland's live values win: they are what's on screen.
    const bool running = !qEnvironmentVariable("OMASTRATOR_HYPRCTL").isEmpty() || !qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty();
    if (askHyprland && running) {
        auto option = [](const QString &name) { return Hyprland::query(QStringLiteral("getoption ") + name).toObject(); };
        auto number = [&](const QString &name, int &into) {
            const QJsonObject answer = option(name);
            if (answer.contains(QLatin1String("int")))
                into = answer["int"].toInt();
            else if (answer.contains(QLatin1String("css")))
                into = answer["css"].toString().section(QLatin1Char(' '), 0, 0).toInt();
        };
        number(QStringLiteral("general:gaps_in"), look.gapsIn);
        number(QStringLiteral("general:gaps_out"), look.gapsOut);
        number(QStringLiteral("general:border_size"), look.borderSize);
        number(QStringLiteral("decoration:rounding"), look.rounding);
        for (auto [name, into] : {std::pair{QStringLiteral("general:col.active_border"), &look.activeBorder},
                                  std::pair{QStringLiteral("general:col.inactive_border"), &look.inactiveBorder}}) {
            const QJsonObject answer = option(name);
            if (const auto colour = parseHyprColor(answer["gradient"].toString(answer["str"].toString())))
                *into = *colour;
        }
    }

    // The bar: shell.json (the user's, else Omarchy's defaults), sizes and colours from shell.toml.
    const QByteArray shellJson = contents(paths.shellJson()).value_or(contents(paths.omarchy + QStringLiteral("/config/omarchy/shell.json")).value_or(QByteArray()));
    const QJsonObject bar = QJsonDocument::fromJson(shellJson).object()["bar"].toObject();
    look.barPosition = bar["position"].toString(QStringLiteral("top"));
    look.barTransparent = bar["transparent"].toBool();
    look.barLayout = bar["layout"].toObject();
    const QByteArray userToml = contents(paths.userShellToml()).value_or(QByteArray());
    const QByteArray themeToml = contents(paths.themeDirectory() + QStringLiteral("/shell.toml")).value_or(QByteArray());
    auto shellValue = [&](const QString &section, const QString &key) {
        const QString user = tomlValue(userToml, section, key);
        return user.isEmpty() ? tomlValue(themeToml, section, key) : user;
    };
    const bool vertical = look.barPosition == QLatin1String("left") || look.barPosition == QLatin1String("right");
    look.barHeight = shellValue(QStringLiteral("bar"), vertical ? QStringLiteral("size-vertical") : QStringLiteral("size-horizontal")).toInt();
    if (look.barHeight <= 0)
        look.barHeight = vertical ? 28 : 26;
    look.barBackground = parseColor(shellValue(QStringLiteral("bar"), QStringLiteral("background")));
    if (look.barBackground.isValid()) {
        bool ok = false;
        const double alpha = shellValue(QStringLiteral("bar"), QStringLiteral("background-alpha")).toDouble(&ok);
        if (ok)
            look.barBackground.setAlphaF(float(qBound(0.0, alpha, 1.0)));
    }
    look.barText = parseColor(shellValue(QStringLiteral("bar"), QStringLiteral("text")));
    look.textSize = shellValue(QStringLiteral("font"), QStringLiteral("base-size")).toInt();
    if (look.textSize <= 0)
        look.textSize = 12;

    // The font: fontconfig's monospace, as `omarchy font current` reads it.
    const QString fonts = QString::fromUtf8(contents(paths.fontsConf()).value_or(QByteArray()));
    static const QRegularExpression prepend(QStringLiteral(R"re(mode="prepend_first"[^>]*>\s*<string>([^<]+)</string>)re"));
    if (const auto match = prepend.match(fonts); match.hasMatch()) {
        look.font = match.captured(1).trimmed();
    } else {
        QProcess fcMatch;
        fcMatch.start(QStringLiteral("fc-match"), {QStringLiteral("monospace"), QStringLiteral("-f"), QStringLiteral("%{family}")});
        if (fcMatch.waitForStarted(2000) && fcMatch.waitForFinished(3000))
            look.font = QString::fromUtf8(fcMatch.readAllStandardOutput()).section(QLatin1Char(','), 0, 0).trimmed();
    }

    look.wallpaper = QFileInfo(paths.background()).symLinkTarget();
    look.theme = paths.themeName();
    look.themeDirectory = look.theme.isEmpty() ? QString() : OmarchyThemes::directoryOf(look.theme);
    static const QRegularExpression colorLine(QStringLiteral(R"re(^[ \t]*([A-Za-z0-9_]+)[ \t]*=[ \t]*"([^"]+)")re"), QRegularExpression::MultilineOption);
    const QString colors = QString::fromUtf8(contents(paths.themeDirectory() + QStringLiteral("/colors.toml")).value_or(QByteArray()));
    const qsizetype table = colors.indexOf(QRegularExpression(QStringLiteral("^\\s*\\["), QRegularExpression::MultilineOption));
    for (auto it = colorLine.globalMatch(table < 0 ? colors : colors.left(table)); it.hasNext();) {
        const auto match = it.next();
        const QColor colour = parseColor(match.captured(2));
        if (colour.isValid())
            look.colors.emplace_back(match.captured(1), colour);
    }
    return look;
}

QString validate(const QJsonObject &edits)
{
    static const QHash<QString, std::pair<int, int>> ranges{{"gapsIn", {0, 200}},  {"gapsOut", {0, 200}},  {"borderSize", {0, 40}},
                                                            {"rounding", {0, 100}}, {"barHeight", {12, 120}}, {"textSize", {6, 48}}};
    static const QSet<QString> known{"gapsIn", "gapsOut", "borderSize", "rounding", "activeBorder", "inactiveBorder", "barPosition", "barTransparent",
                                     "barHeight", "barBackground", "barText", "barLayout", "font", "textSize", "wallpaper", "colors"};
    if (edits.isEmpty())
        return QStringLiteral("There's nothing to change.");
    for (auto it = edits.begin(); it != edits.end(); ++it) {
        const QString key = it.key();
        if (!known.contains(key))
            return QStringLiteral("There's no desktop setting “%1”.").arg(key);
        if (ranges.contains(key)) {
            const auto [low, high] = ranges.value(key);
            if (!it.value().isDouble() || it.value().toDouble() < low || it.value().toDouble() > high)
                return QStringLiteral("%1 must be a number from %2 to %3.").arg(key).arg(low).arg(high);
        } else if (key.endsWith(QLatin1String("Border")) || key == QLatin1String("barBackground") || key == QLatin1String("barText")) {
            if (!parseColor(it.value().toString()).isValid())
                return QStringLiteral("“%1” isn't a colour.").arg(it.value().toString());
        } else if (key == QLatin1String("barPosition")) {
            if (!QStringList{"top", "bottom", "left", "right"}.contains(it.value().toString()))
                return QStringLiteral("The bar goes at the top, bottom, left or right.");
        } else if (key == QLatin1String("barTransparent")) {
            if (!it.value().isBool())
                return QStringLiteral("barTransparent is true or false.");
        } else if (key == QLatin1String("barLayout")) {
            const QJsonObject order = it.value().toObject();
            for (const QString &section : order.keys()) {
                if (!QStringList{"left", "center", "right"}.contains(section) || !order[section].isArray())
                    return QStringLiteral("The bar's layout has left, center and right lists of widget ids.");
            }
        } else if (key == QLatin1String("font")) {
            const QString font = it.value().toString().trimmed();
            if (font.isEmpty() || font.contains(QLatin1Char('"')) || font.contains(QLatin1Char('\n')))
                return QStringLiteral("Choose a font family.");
        } else if (key == QLatin1String("wallpaper")) {
            const QFileInfo image(it.value().toString());
            if (!image.isFile())
                return QStringLiteral("The wallpaper %1 isn't there.").arg(image.filePath());
        } else if (key == QLatin1String("colors")) {
            const QJsonObject colours = it.value().toObject();
            if (colours.isEmpty())
                return QStringLiteral("Choose a colour to change.");
            static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_]+$"));
            for (auto colour = colours.begin(); colour != colours.end(); ++colour) {
                if (!name.match(colour.key()).hasMatch() || !parseColor(colour.value().toString()).isValid())
                    return QStringLiteral("“%1” isn't a theme colour.").arg(colour.key());
            }
        }
    }
    return {};
}

QJsonObject blockValues(const QByteArray &bytes)
{
    const QString file = QString::fromUtf8(bytes);
    const qsizetype start = file.indexOf(QLatin1String("BEGIN Omastrator: desktop look"));
    const qsizetype end = file.indexOf(QLatin1String("END Omastrator: desktop look"), start);
    if (start < 0 || end < 0)
        return {};
    const QString block = file.mid(start, end - start);
    QJsonObject values;
    for (const auto &[key, name] : {std::pair{"gapsIn", "gaps_in"}, std::pair{"gapsOut", "gaps_out"}, std::pair{"borderSize", "border_size"},
                                    std::pair{"rounding", "rounding"}}) {
        if (const auto number = firstNumber(block, QLatin1String(name)))
            values[QLatin1String(key)] = *number;
    }
    for (const auto &[key, which] : {std::pair{"activeBorder", "active"}, std::pair{"inactiveBorder", "inactive"}}) {
        if (const auto colour = firstBorder(block, QLatin1String(which)))
            values[QLatin1String(key)] = hexOf(*colour);
    }
    return values;
}

QByteArray luaBlock(const QJsonObject &values)
{
    QStringList general, colours, group, decoration;
    const auto number = [&](QStringList &into, const char *key, const char *name) {
        if (values.contains(QLatin1String(key)))
            into.append(QStringLiteral("%1 = %2,").arg(QLatin1String(name)).arg(values[QLatin1String(key)].toInt()));
    };
    number(general, "gapsIn", "gaps_in");
    number(general, "gapsOut", "gaps_out");
    number(general, "borderSize", "border_size");
    number(decoration, "rounding", "rounding");
    for (const auto &[key, name, groupName] : {std::tuple{"activeBorder", "active_border", "border_active"}, std::tuple{"inactiveBorder", "inactive_border", "border_inactive"}}) {
        if (!values.contains(QLatin1String(key)))
            continue;
        const QString colour = hyprColor(parseColor(values[QLatin1String(key)].toString()));
        colours.append(QStringLiteral("%1 = \"%2\",").arg(QLatin1String(name), colour));
        group.append(QStringLiteral("%1 = \"%2\",").arg(QLatin1String(groupName), colour));
    }
    QString text = QStringLiteral("-- BEGIN Omastrator: desktop look (made in design mode; Omastrator's Revert puts this file back)\nhl.config({\n");
    if (!general.isEmpty() || !colours.isEmpty()) {
        text += QStringLiteral("  general = {\n");
        for (const QString &line : general)
            text += QStringLiteral("    ") + line + QLatin1Char('\n');
        if (!colours.isEmpty()) {
            text += QStringLiteral("    col = {\n");
            for (const QString &line : colours)
                text += QStringLiteral("      ") + line + QLatin1Char('\n');
            text += QStringLiteral("    },\n");
        }
        text += QStringLiteral("  },\n");
    }
    if (!group.isEmpty()) {
        text += QStringLiteral("  group = {\n    col = {\n");
        for (const QString &line : group)
            text += QStringLiteral("      ") + line + QLatin1Char('\n');
        text += QStringLiteral("    },\n  },\n");
    }
    if (!decoration.isEmpty())
        text += QStringLiteral("  decoration = {\n    ") + decoration.join(QStringLiteral("\n    ")) + QStringLiteral("\n  },\n");
    text += QStringLiteral("})\n-- END Omastrator: desktop look\n");
    return text.toUtf8();
}

QByteArray confBlock(const QJsonObject &values)
{
    QStringList general, decoration, group;
    const auto number = [&](QStringList &into, const char *key, const char *name) {
        if (values.contains(QLatin1String(key)))
            into.append(QStringLiteral("%1 = %2").arg(QLatin1String(name)).arg(values[QLatin1String(key)].toInt()));
    };
    number(general, "gapsIn", "gaps_in");
    number(general, "gapsOut", "gaps_out");
    number(general, "borderSize", "border_size");
    number(decoration, "rounding", "rounding");
    for (const auto &[key, name, groupName] : {std::tuple{"activeBorder", "active_border", "border_active"}, std::tuple{"inactiveBorder", "inactive_border", "border_inactive"}}) {
        if (!values.contains(QLatin1String(key)))
            continue;
        const QString colour = hyprColor(parseColor(values[QLatin1String(key)].toString()));
        general.append(QStringLiteral("col.%1 = %2").arg(QLatin1String(name), colour));
        group.append(QStringLiteral("col.%1 = %2").arg(QLatin1String(groupName), colour));
    }
    QString text = QStringLiteral("# BEGIN Omastrator: desktop look (made in design mode; Omastrator's Revert puts this file back)\n");
    for (const auto &[name, lines] : {std::pair{QStringLiteral("general"), &general}, std::pair{QStringLiteral("group"), &group},
                                      std::pair{QStringLiteral("decoration"), &decoration}}) {
        if (lines->isEmpty())
            continue;
        text += name + QStringLiteral(" {\n");
        for (const QString &line : *lines)
            text += QStringLiteral("    ") + line + QLatin1Char('\n');
        text += QStringLiteral("}\n");
    }
    text += QStringLiteral("# END Omastrator: desktop look\n");
    return text.toUtf8();
}

QByteArray withBlock(const QByteArray &bytes, const QByteArray &block, bool lua)
{
    QString file = QString::fromUtf8(bytes);
    const QString comment = lua ? QStringLiteral("--") : QStringLiteral("#");
    const qsizetype begin = file.indexOf(comment + QStringLiteral(" BEGIN Omastrator: desktop look"));
    const QString endMarker = comment + QStringLiteral(" END Omastrator: desktop look");
    const qsizetype end = begin < 0 ? -1 : file.indexOf(endMarker, begin);
    if (begin >= 0 && end >= 0) {
        qsizetype after = end + endMarker.size();
        if (after < file.size() && file.at(after) == QLatin1Char('\n'))
            ++after;
        file.replace(begin, after - begin, QString::fromUtf8(block));
        return file.toUtf8();
    }
    // At the end, so it's loaded after everything else in the file.
    if (!file.isEmpty() && !file.endsWith(QLatin1Char('\n')))
        file += QLatin1Char('\n');
    if (!file.isEmpty())
        file += QLatin1Char('\n');
    return file.toUtf8() + block;
}

QString luaEval(const QJsonObject &values)
{
    // The block's table on one line: hl.config({ general = { gaps_in = 8, }, })
    QString block = QString::fromUtf8(luaBlock(values));
    QStringList lines = block.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines.removeIf([](const QString &line) { return line.trimmed().startsWith(QLatin1String("--")); });
    for (QString &line : lines)
        line = line.trimmed();
    return lines.join(QLatin1Char(' '));
}

QString tomlValue(const QByteArray &bytes, const QString &section, const QString &key)
{
    QString current;
    const QRegularExpression pattern(QStringLiteral(R"re(^\s*%1\s*=\s*(?:"([^"]*)"|'([^']*)'|([^#\s]+)))re").arg(QRegularExpression::escape(key)));
    for (const QString &line : QString::fromUtf8(bytes).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            current = trimmed.mid(1).section(QLatin1Char(']'), 0, 0).trimmed();
            continue;
        }
        if (current != section)
            continue;
        if (const auto match = pattern.match(line); match.hasMatch())
            return !match.captured(1).isNull() ? match.captured(1) : !match.captured(2).isNull() ? match.captured(2) : match.captured(3);
    }
    return {};
}

QByteArray setTomlKey(const QByteArray &bytes, const QString &section, const QString &key, const QString &value)
{
    QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    const bool trailing = !lines.isEmpty() && lines.back().isEmpty();
    if (trailing)
        lines.removeLast();
    const QRegularExpression pattern(QStringLiteral(R"re(^(\s*%1\s*=\s*)("[^"]*"|'[^']*'|[^#\s]+)(.*)$)re").arg(QRegularExpression::escape(key)));
    QString current;
    qsizetype lastInSection = -1;
    bool inSection = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            current = trimmed.mid(1).section(QLatin1Char(']'), 0, 0).trimmed();
            inSection = current == section;
            if (inSection)
                lastInSection = i;
            continue;
        }
        if (!inSection)
            continue;
        if (const auto match = pattern.match(lines[i]); match.hasMatch()) {
            lines[i] = match.captured(1) + value + match.captured(3);
            return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
        }
        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#')))
            lastInSection = i;
    }
    const QString line = key + QStringLiteral(" = ") + value;
    if (lastInSection >= 0) {
        lines.insert(lastInSection + 1, line);
    } else {
        if (!lines.isEmpty() && !lines.back().trimmed().isEmpty())
            lines.append(QString());
        lines.append(QLatin1Char('[') + section + QLatin1Char(']'));
        lines.append(line);
    }
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

QJsonObject reorderLayout(const QJsonObject &layout, const QJsonObject &order)
{
    // Every widget by id, with its options.
    QHash<QString, QJsonObject> widgets;
    for (const QString &section : {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
        for (const QJsonValue &value : layout[section].toArray()) {
            const QJsonObject widget = value.isString() ? QJsonObject{{"id", value.toString()}} : value.toObject();
            widgets.insert(widget["id"].toString(), widget);
        }
    }
    QJsonObject result = layout;
    QSet<QString> placed;
    for (const QString &section : {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
        if (!order.contains(section))
            continue;
        QJsonArray list;
        for (const QJsonValue &id : order[section].toArray()) {
            const QString name = id.toString();
            if (name.isEmpty() || placed.contains(name))
                continue;
            placed.insert(name);
            list.append(widgets.value(name, QJsonObject{{"id", name}}));
        }
        result[section] = list;
    }
    // A section not given keeps its widgets, less any moved elsewhere.
    for (const QString &section : {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
        if (order.contains(section))
            continue;
        QJsonArray list;
        for (const QJsonValue &value : layout[section].toArray()) {
            const QString id = value.isString() ? value.toString() : value.toObject()["id"].toString();
            if (!placed.contains(id))
                list.append(value);
        }
        result[section] = list;
    }
    return result;
}
}
