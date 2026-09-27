#include "System/AppStyle.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>

// Toolkits, selectors and the text of GTK CSS, Qt stylesheets and qt5ct/qt6ct
// files. The plans and previews are in AppStyle+Plans.cpp.

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

QString cssHex(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(QString::number(color.alphaF(), 'g', 3));
}

QColor readable(const QColor &background)
{
    return background.lightnessF() > 0.6 ? QColor(0x14, 0x14, 0x16) : QColor(0xf5, 0xf5, 0xf7);
}

// The rules of Omastrator's block: named colours and custom properties, then a declaration list per selector.
struct Rules {
    QStringList colorNames;
    QHash<QString, QString> colors;
    QStringList selectors;
    QHash<QString, QStringList> properties;
    QHash<QString, QHash<QString, QString>> values;

    void setColor(const QString &name, const QString &value)
    {
        if (!colors.contains(name))
            colorNames.append(name);
        colors[name] = value;
    }
    void set(const QString &selector, const QString &property, const QString &value)
    {
        if (!values.contains(selector))
            selectors.append(selector);
        if (!values[selector].contains(property))
            properties[selector].append(property);
        values[selector][property] = value;
    }
    QString text() const
    {
        QString out;
        for (const QString &name : colorNames)
            out += QStringLiteral("@define-color %1 %2;\n").arg(name, colors.value(name));
        for (const QString &selector : selectors) {
            out += selector + QStringLiteral(" {\n");
            for (const QString &property : properties.value(selector))
                out += QStringLiteral("  %1: %2;\n").arg(property, values.value(selector).value(property));
            out += QStringLiteral("}\n");
        }
        return out;
    }
    static Rules parse(const QString &block)
    {
        Rules rules;
        static const QRegularExpression define(QStringLiteral(R"(@define-color\s+([A-Za-z0-9_-]+)\s+([^;]+);)"));
        for (auto it = define.globalMatch(block); it.hasNext();) {
            const auto match = it.next();
            rules.setColor(match.captured(1), match.captured(2).trimmed());
        }
        static const QRegularExpression rule(QStringLiteral(R"(([^{};/]+)\{([^}]*)\})"));
        for (auto it = rule.globalMatch(block); it.hasNext();) {
            const auto match = it.next();
            const QString selector = match.captured(1).trimmed();
            for (const QString &declaration : match.captured(2).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                const qsizetype colon = declaration.indexOf(QLatin1Char(':'));
                if (colon > 0 && !declaration.trimmed().isEmpty())
                    rules.set(selector, declaration.left(colon).trimmed(), declaration.mid(colon + 1).trimmed());
            }
        }
        return rules;
    }
};

const QString beginGtk = QStringLiteral("/* BEGIN Omastrator: restyle (made in design mode; Omastrator's Revert puts this file back) */");
const QString endGtk = QStringLiteral("/* END Omastrator: restyle */");
}

namespace AppStyle {
Toolkit fromMaps(const QByteArray &maps, bool *widgets)
{
    if (widgets)
        *widgets = maps.contains("libQt6Widgets") || maps.contains("libQt5Widgets");
    if (maps.contains("libgtk-4.so"))
        return Toolkit::gtk4;
    if (maps.contains("libgtk-3.so"))
        return Toolkit::gtk3;
    if (maps.contains("libQt6Gui") || maps.contains("libQt6Core") || maps.contains("libQt6Widgets"))
        return Toolkit::qt6;
    if (maps.contains("libQt5Gui") || maps.contains("libQt5Core") || maps.contains("libQt5Widgets"))
        return Toolkit::qt5;
    return Toolkit::unknown;
}

QString name(Toolkit toolkit)
{
    switch (toolkit) {
    case Toolkit::gtk3:
        return QStringLiteral("GTK 3");
    case Toolkit::gtk4:
        return QStringLiteral("GTK 4");
    case Toolkit::qt5:
        return QStringLiteral("Qt 5");
    case Toolkit::qt6:
        return QStringLiteral("Qt 6");
    case Toolkit::unknown:
        break;
    }
    return QStringLiteral("an unknown toolkit");
}

bool isQt(Toolkit toolkit)
{
    return toolkit == Toolkit::qt5 || toolkit == Toolkit::qt6;
}

bool App::usesQtct() const
{
    return isQt(toolkit) && (platformTheme == QLatin1String("qt6ct") || platformTheme == QLatin1String("qt5ct"));
}

App App::describe(qint64 pid, const QString &className, const QString &proc)
{
    App app;
    app.pid = pid;
    app.className = className;
    const QString folder = QStringLiteral("%1/%2").arg(proc).arg(pid);
    for (const QByteArray &word : contents(folder + QStringLiteral("/cmdline")).value_or(QByteArray()).split('\0')) {
        if (!word.isEmpty())
            app.command.append(QString::fromLocal8Bit(word));
    }
    for (const QByteArray &entry : contents(folder + QStringLiteral("/environ")).value_or(QByteArray()).split('\0')) {
        if (entry.startsWith("QT_QPA_PLATFORMTHEME="))
            app.platformTheme = QString::fromLocal8Bit(entry.mid(21));
    }
    app.toolkit = fromMaps(contents(folder + QStringLiteral("/maps")).value_or(QByteArray()), &app.widgets);
    // Its launcher entry: by the window class, the entry's own name, or the program it runs.
    const QString program = app.command.isEmpty() ? QString() : QFileInfo(app.command.front()).fileName();
    const QString shortClass = className.section(QLatin1Char('.'), -1);
    QStringList folders = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    QString fallback;
    for (const QString &applications : folders) {
        const QFileInfoList entries = QDir(applications).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QFileInfo &entry : entries) {
            const QString text = QString::fromUtf8(contents(entry.filePath()).value_or(QByteArray()));
            static const QRegularExpression wmClass(QStringLiteral(R"(^StartupWMClass=(.+)$)"), QRegularExpression::MultilineOption);
            static const QRegularExpression exec(QStringLiteral(R"(^Exec="?([^" ]+))"), QRegularExpression::MultilineOption);
            const QString startup = wmClass.match(text).captured(1).trimmed();
            const QString base = entry.completeBaseName();
            if (!className.isEmpty()
                && (startup.compare(className, Qt::CaseInsensitive) == 0 || base.compare(className, Qt::CaseInsensitive) == 0
                    || base.compare(shortClass, Qt::CaseInsensitive) == 0)) {
                app.desktopFile = entry.filePath();
                return app;
            }
            if (fallback.isEmpty() && !program.isEmpty() && QFileInfo(exec.match(text).captured(1)).fileName() == program)
                fallback = entry.filePath();
        }
    }
    app.desktopFile = fallback;
    return app;
}

Style Style::fromJson(const QJsonObject &json)
{
    Style style;
    const auto colour = [&](const char *key) { return json.contains(QLatin1String(key)) ? DesktopLook::parseColor(json[QLatin1String(key)].toString()) : QColor(); };
    style.accent = colour("accent");
    style.background = colour("background");
    style.foreground = colour("foreground");
    style.font = json["font"].toString().trimmed();
    style.fontSize = json["fontSize"].toDouble();
    style.radius = json.contains(QLatin1String("radius")) ? json["radius"].toInt() : -1;
    style.role = json["role"].toString();
    return style;
}

QJsonObject Style::toJson() const
{
    QJsonObject json;
    if (accent.isValid())
        json["accent"] = accent.name(QColor::HexRgb);
    if (background.isValid())
        json["background"] = background.name(QColor::HexRgb);
    if (foreground.isValid())
        json["foreground"] = foreground.name(QColor::HexRgb);
    if (!font.isEmpty())
        json["font"] = font;
    if (fontSize > 0)
        json["fontSize"] = fontSize;
    if (radius >= 0)
        json["radius"] = radius;
    if (!role.isEmpty())
        json["role"] = role;
    return json;
}

bool Style::isEmpty() const
{
    return !accent.isValid() && !background.isValid() && !foreground.isValid() && font.isEmpty() && fontSize <= 0 && radius < 0;
}

QString validate(const Style &style)
{
    if (style.isEmpty())
        return QStringLiteral("Choose a colour, font or corner radius to change.");
    if (style.font.contains(QLatin1Char('"')) || style.font.contains(QLatin1Char(';')) || style.font.contains(QLatin1Char('\n')))
        return QStringLiteral("That font name can't be used.");
    if (style.fontSize < 0 || style.fontSize > 72)
        return QStringLiteral("The font size goes from 1 to 72 points.");
    if (style.radius > 64)
        return QStringLiteral("The corner radius goes up to 64 pixels.");
    return {};
}

QString gtkSelector(const QString &role)
{
    static const QHash<QString, QString> nodes{{"push button", "button"},    {"toggle button", "button"}, {"button", "button"},
                                               {"text", "entry"},            {"entry", "entry"},          {"password text", "entry"},
                                               {"label", "label"},           {"menu bar", "menubar"},     {"menu item", "menuitem"},
                                               {"check box", "checkbutton"}, {"radio button", "radiobutton"}, {"combo box", "combobox"},
                                               {"list item", "row"},         {"list", "list"},            {"tool bar", "toolbar"},
                                               {"scroll bar", "scrollbar"},  {"slider", "scale"},         {"page tab", "tab"},
                                               {"table", "treeview"},        {"tree table", "treeview"},  {"header", "headerbar"},
                                               {"heading", "label"},         {"switch", "switch"},        {"spin button", "spinbutton"}};
    return nodes.value(role.toLower(), QString());
}

QString qtSelector(const QString &role)
{
    static const QHash<QString, QString> classes{{"push button", "QPushButton"}, {"toggle button", "QPushButton"}, {"button", "QPushButton"},
                                                 {"text", "QLineEdit"},          {"entry", "QLineEdit"},          {"password text", "QLineEdit"},
                                                 {"label", "QLabel"},            {"menu bar", "QMenuBar"},        {"menu item", "QMenu::item"},
                                                 {"check box", "QCheckBox"},     {"radio button", "QRadioButton"}, {"combo box", "QComboBox"},
                                                 {"list item", "QAbstractItemView::item"}, {"list", "QAbstractItemView"},
                                                 {"tool bar", "QToolBar"},       {"scroll bar", "QScrollBar"},    {"slider", "QSlider"},
                                                 {"page tab", "QTabBar::tab"},   {"table", "QTableView"},         {"tree table", "QTreeView"},
                                                 {"spin button", "QSpinBox"}};
    return classes.value(role.toLower(), QString());
}

QByteArray withGtkStyle(const QByteArray &bytes, const Style &style, Toolkit toolkit)
{
    QString css = QString::fromUtf8(bytes);
    Rules rules;
    const qsizetype begin = css.indexOf(beginGtk);
    const qsizetype end = begin < 0 ? -1 : css.indexOf(endGtk, begin);
    if (begin >= 0 && end >= 0) {
        rules = Rules::parse(css.mid(begin + beginGtk.size(), end - begin - beginGtk.size()));
        qsizetype after = end + endGtk.size();
        if (after < css.size() && css.at(after) == QLatin1Char('\n'))
            ++after;
        css.remove(begin, after - begin);
    }
    const QString widget = gtkSelector(style.role);
    const bool gtk4 = toolkit == Toolkit::gtk4;
    // The accent is the theme's, so it's named for every widget; the rest goes on the widget pointed at.
    if (style.accent.isValid()) {
        const QString accent = cssHex(style.accent);
        rules.setColor(QStringLiteral("accent_bg_color"), accent);
        rules.setColor(QStringLiteral("accent_color"), accent);
        rules.setColor(QStringLiteral("theme_selected_bg_color"), accent);
        if (gtk4) {
            rules.set(QStringLiteral(":root"), QStringLiteral("--accent-bg-color"), accent);
            rules.set(QStringLiteral(":root"), QStringLiteral("--accent-color"), accent);
        }
    }
    if (widget.isEmpty()) {
        if (style.background.isValid()) {
            rules.setColor(QStringLiteral("window_bg_color"), cssHex(style.background));
            rules.setColor(QStringLiteral("theme_bg_color"), cssHex(style.background));
            if (gtk4)
                rules.set(QStringLiteral(":root"), QStringLiteral("--window-bg-color"), cssHex(style.background));
        }
        if (style.foreground.isValid()) {
            rules.setColor(QStringLiteral("window_fg_color"), cssHex(style.foreground));
            rules.setColor(QStringLiteral("theme_fg_color"), cssHex(style.foreground));
            if (gtk4)
                rules.set(QStringLiteral(":root"), QStringLiteral("--window-fg-color"), cssHex(style.foreground));
        }
    } else {
        if (style.background.isValid()) {
            rules.set(widget, QStringLiteral("background-color"), cssHex(style.background));
            rules.set(widget, QStringLiteral("background-image"), QStringLiteral("none"));
        }
        if (style.foreground.isValid())
            rules.set(widget, QStringLiteral("color"), cssHex(style.foreground));
    }
    const QString textTarget = widget.isEmpty() ? QStringLiteral("window") : widget;
    if (!style.font.isEmpty())
        rules.set(textTarget, QStringLiteral("font-family"), QLatin1Char('"') + style.font + QLatin1Char('"'));
    if (style.fontSize > 0)
        rules.set(textTarget, QStringLiteral("font-size"), QString::number(style.fontSize, 'g', 4) + QStringLiteral("pt"));
    if (style.radius >= 0)
        rules.set(widget.isEmpty() ? QStringLiteral("button, entry") : widget, QStringLiteral("border-radius"), QString::number(style.radius) + QStringLiteral("px"));
    // At the end, so it wins over the rules above it.
    if (!css.isEmpty() && !css.endsWith(QLatin1Char('\n')))
        css += QLatin1Char('\n');
    if (!css.trimmed().isEmpty() && !css.endsWith(QStringLiteral("\n\n")))
        css += QLatin1Char('\n');
    css += beginGtk + QLatin1Char('\n') + rules.text() + endGtk + QLatin1Char('\n');
    return css.toUtf8();
}

QByteArray qss(const Style &style)
{
    const QString widget = qtSelector(style.role);
    QStringList lines;
    const auto rule = [&](const QString &selector, const QStringList &declarations) {
        if (!declarations.isEmpty())
            lines.append(selector + QStringLiteral(" {\n    ") + declarations.join(QStringLiteral(";\n    ")) + QStringLiteral(";\n}"));
    };
    QStringList global, target;
    if (style.accent.isValid())
        global << QStringLiteral("selection-background-color: %1").arg(style.accent.name()) << QStringLiteral("selection-color: %1").arg(readable(style.accent).name());
    QStringList &colours = widget.isEmpty() ? global : target;
    if (style.foreground.isValid())
        colours << QStringLiteral("color: %1").arg(style.foreground.name());
    if (style.background.isValid() && !widget.isEmpty())
        target << QStringLiteral("background-color: %1").arg(style.background.name());
    if (!style.font.isEmpty())
        colours << QStringLiteral("font-family: \"%1\"").arg(style.font);
    if (style.fontSize > 0)
        colours << QStringLiteral("font-size: %1pt").arg(QString::number(style.fontSize, 'g', 4));
    rule(QStringLiteral("*"), global);
    if (style.background.isValid() && widget.isEmpty())
        rule(QStringLiteral("QMainWindow, QDialog"), {QStringLiteral("background-color: %1").arg(style.background.name())});
    if (style.radius >= 0) {
        // Qt draws a radius only on a styled border.
        const QString edge = (style.foreground.isValid() ? style.foreground : QColor(0x80, 0x80, 0x80)).name();
        const QString selector = widget.isEmpty() ? QStringLiteral("QPushButton, QLineEdit, QComboBox") : widget;
        rule(selector, target + QStringList{QStringLiteral("border: 1px solid %1").arg(edge), QStringLiteral("border-radius: %1px").arg(style.radius),
                                            QStringLiteral("padding: 3px 8px")});
    } else {
        rule(widget, target);
    }
    return (QStringLiteral("/* Made in Omastrator's design mode; Omastrator's Revert puts this file back. */\n") + lines.join(QStringLiteral("\n\n"))
            + QLatin1Char('\n'))
        .toUtf8();
}

QByteArray qtctScheme(const Style &style, const QPalette &base, int roles)
{
    QPalette palette = base;
    const auto setAll = [&](QPalette::ColorRole role, const QColor &color) {
        palette.setColor(QPalette::Active, role, color);
        palette.setColor(QPalette::Inactive, role, color);
        palette.setColor(QPalette::Disabled, role, color);
    };
    if (style.background.isValid()) {
        for (const QPalette::ColorRole role : {QPalette::Window, QPalette::Base, QPalette::Button, QPalette::ToolTipBase})
            setAll(role, style.background);
        setAll(QPalette::AlternateBase, style.background.lightnessF() > 0.5 ? style.background.darker(106) : style.background.lighter(115));
        setAll(QPalette::Light, style.background.lighter(150));
        setAll(QPalette::Midlight, style.background.lighter(125));
        setAll(QPalette::Mid, style.background.darker(130));
        setAll(QPalette::Dark, style.background.darker(160));
        setAll(QPalette::Shadow, style.background.darker(300));
    }
    if (style.foreground.isValid()) {
        for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText, QPalette::BrightText})
            setAll(role, style.foreground);
        QColor faded = style.foreground;
        faded.setAlphaF(0.5f);
        setAll(QPalette::PlaceholderText, faded);
        for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            palette.setColor(QPalette::Disabled, role, faded);
    }
    if (style.accent.isValid()) {
        setAll(QPalette::Highlight, style.accent);
        setAll(QPalette::HighlightedText, readable(style.accent));
        setAll(QPalette::Link, style.accent);
        setAll(QPalette::LinkVisited, style.accent.darker(120));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        setAll(QPalette::Accent, style.accent);
#endif
    }
    const auto group = [&](QPalette::ColorGroup which) {
        QStringList colours;
        for (int role = 0; role < roles; ++role)
            colours.append(palette.color(which, QPalette::ColorRole(role)).name(QColor::HexArgb));
        return colours.join(QStringLiteral(", "));
    };
    return QStringLiteral("[ColorScheme]\nactive_colors=%1\ndisabled_colors=%2\ninactive_colors=%3\n")
        .arg(group(QPalette::Active), group(QPalette::Disabled), group(QPalette::Inactive))
        .toUtf8();
}

QByteArray qtctConf(const QByteArray &conf, const QString &schemePath, const QString &qssPath, const Style &style)
{
    QByteArray result = conf;
    result = DesktopLook::setTomlKey(result, QStringLiteral("Appearance"), QStringLiteral("custom_palette"), QStringLiteral("true"));
    result = DesktopLook::setTomlKey(result, QStringLiteral("Appearance"), QStringLiteral("color_scheme_path"), schemePath);
    result = DesktopLook::setTomlKey(result, QStringLiteral("Interface"), QStringLiteral("stylesheets"), qssPath);
    if (!style.font.isEmpty() || style.fontSize > 0) {
        QFont font(style.font.isEmpty() ? QStringLiteral("Sans Serif") : style.font);
        font.setPointSizeF(style.fontSize > 0 ? style.fontSize : 10);
        result = DesktopLook::setTomlKey(result, QStringLiteral("Fonts"), QStringLiteral("general"), QLatin1Char('"') + font.toString() + QLatin1Char('"'));
    }
    return result;
}

QByteArray withStylesheet(const QByteArray &entry, const QString &qssPath)
{
    QStringList lines = QString::fromUtf8(entry).split(QLatin1Char('\n'));
    const QString argument = qssPath.contains(QLatin1Char(' ')) ? QStringLiteral("-stylesheet \"%1\"").arg(qssPath) : QStringLiteral("-stylesheet ") + qssPath;
    static const QRegularExpression existing(QStringLiteral(R"( -stylesheet(=| )("[^"]*"|\S+))"));
    static const QRegularExpression program(QStringLiteral(R"(^(Exec=("[^"]*"|\S+)))"));
    for (QString &line : lines) {
        if (!line.startsWith(QLatin1String("Exec=")))
            continue;
        line.remove(existing);
        const auto match = program.match(line);
        if (match.hasMatch())
            line.insert(match.capturedEnd(1), QLatin1Char(' ') + argument);
    }
    return lines.join(QLatin1Char('\n')).toUtf8();
}

QString gtkCss(const DesktopLook::Paths &paths, Toolkit toolkit)
{
    return paths.config + (toolkit == Toolkit::gtk4 ? QStringLiteral("/gtk-4.0/gtk.css") : QStringLiteral("/gtk-3.0/gtk.css"));
}

QString qtctFolder(const DesktopLook::Paths &paths, Toolkit toolkit)
{
    return paths.config + (toolkit == Toolkit::qt5 ? QStringLiteral("/qt5ct") : QStringLiteral("/qt6ct"));
}

QString appQss(const DesktopLook::Paths &paths, const App &app)
{
    QString name = app.className.isEmpty() && !app.command.isEmpty() ? QFileInfo(app.command.front()).fileName() : app.className;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("-"));
    return paths.config + QStringLiteral("/omastrator/styles/") + name + QStringLiteral(".qss");
}

QString userDesktopFile(const DesktopLook::Paths &paths, const App &app)
{
    if (app.desktopFile.isEmpty())
        return {};
    return paths.data + QStringLiteral("/applications/") + QFileInfo(app.desktopFile).fileName();
}
}
