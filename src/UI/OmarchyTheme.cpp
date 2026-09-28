#include "UI/OmarchyTheme.h"
#include "UI/OmarchyStyle.h"
#include "Logging.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QRegularExpression>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

namespace {
// The file's assignments: quoted values, and keys without one.
struct Assignments {
    QMap<QString, QString> values;
    QSet<QString> malformed;

    QString value(const QString &key) const
    {
        if (malformed.contains(key))
            throw std::runtime_error("colors.toml: " + key.toStdString() + " is no quoted string");
        if (!values.contains(key))
            throw std::runtime_error("colors.toml lacks " + key.toStdString());
        return values.value(key);
    }
};

bool isQuote(QChar character)
{
    return character == QLatin1Char('"') || character == QLatin1Char('\'');
}

// A key or value with its matching quotes taken off.
QString unquoted(const QString &text)
{
    const bool quoted = text.size() >= 2 && isQuote(text.front()) && text.back() == text.front();
    return quoted ? text.mid(1, text.size() - 2) : text;
}

Assignments assignments(const QString &toml)
{
    Assignments result;
    for (const QString &each : toml.split(QLatin1Char('\n'))) {
        const QString line = each.trimmed();
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')) || equals < 0)
            continue;
        const QString key = unquoted(line.left(equals).trimmed());
        const QString rest = line.mid(equals + 1).trimmed();
        const qsizetype close = rest.isEmpty() || !isQuote(rest.front()) ? -1 : rest.indexOf(rest.front(), 1);
        const QString after = close < 0 ? QString() : rest.mid(close + 1).trimmed();
        if (close < 0 || !(after.isEmpty() || after.startsWith(QLatin1Char('#'))))
            result.malformed.insert(key);
        else
            result.values.insert(key, rest.mid(1, close - 1));
    }
    return result;
}

const QRegularExpression &hexColour()
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return hex;
}

QColor colour(const Assignments &found, const QString &key)
{
    const QString value = found.value(key);
    if (!hexColour().match(value).hasMatch())
        throw std::runtime_error("colors.toml: " + key.toStdString() + " is no #rrggbb colour: " + value.toStdString());
    return QColor(value);
}

// A component token: optional, so one missing or not #rrggbb takes its fallback.
QColor token(const Assignments &found, const QString &key, const QColor &fallback)
{
    const QString value = found.values.value(key);
    return hexColour().match(value).hasMatch() ? QColor(value) : fallback;
}

// `over` laid on `under` at `weight`.
QColor mix(const QColor &over, const QColor &under, double weight)
{
    return QColor::fromRgbF(float(over.redF() * weight + under.redF() * (1 - weight)), float(over.greenF() * weight + under.greenF() * (1 - weight)),
                            float(over.blueF() * weight + under.blueF() * (1 - weight)));
}

// The component roles from the base colours, then the theme's own tokens over them.
void fillComponents(OmarchyColors &colors, const Assignments *found)
{
    const auto pick = [found](const char *key, const QColor &fallback) {
        return found ? token(*found, QString::fromLatin1(key), fallback) : fallback;
    };
    colors.components = found && found->values.contains(QStringLiteral("surface_1"));
    colors.surface0 = pick("surface_0", colors.darkerBackground);
    colors.surface1 = pick("surface_1", colors.background);
    colors.surface2 = pick("surface_2", colors.lighterBackground);
    colors.surface3 = pick("surface_3", mix(colors.foreground, colors.background, 0.1));
    colors.lift = pick("lift", mix(colors.foreground, colors.background, 0.14));
    colors.text1 = pick("text_1", colors.brightForeground);
    colors.text2 = pick("text_2", colors.foreground);
    colors.text3 = pick("text_3", colors.lightForeground);
    colors.text4 = pick("text_4", colors.darkForeground);
    colors.accentSoft = pick("accent_soft", colors.accent);
    colors.onAccent = pick("on_accent", colors.darkerBackground);
}
}

OmarchyColors OmarchyColors::parse(const QString &toml)
{
    // Flat `key = "value"` lines; recognised keys well formed.
    const Assignments values = assignments(toml);
    const QString mode = values.value(QStringLiteral("mode"));
    if (mode != QLatin1String("dark") && mode != QLatin1String("light"))
        throw std::runtime_error("colors.toml: mode must be dark or light, not " + mode.toStdString());
    OmarchyColors result{.dark = mode == QLatin1String("dark"),
            .accent = colour(values, QStringLiteral("accent")),
            .selection = colour(values, QStringLiteral("selection")),
            .muted = colour(values, QStringLiteral("muted")),
            .background = colour(values, QStringLiteral("background")),
            .darkBackground = colour(values, QStringLiteral("dark_background")),
            .darkerBackground = colour(values, QStringLiteral("darker_background")),
            .lighterBackground = colour(values, QStringLiteral("lighter_background")),
            .foreground = colour(values, QStringLiteral("foreground")),
            .darkForeground = colour(values, QStringLiteral("dark_foreground")),
            .lightForeground = colour(values, QStringLiteral("light_foreground")),
            .brightForeground = colour(values, QStringLiteral("bright_foreground")),
            .red = colour(values, QStringLiteral("red")),
            .warning = colour(values, values.values.contains(QStringLiteral("orange")) || values.malformed.contains(QStringLiteral("orange"))
                                          ? QStringLiteral("orange")
                                          : QStringLiteral("yellow"))};
    fillComponents(result, &values);
    return result;
}

OmarchyColors OmarchyColors::builtInDark()
{
    // Swift's grays: the window at 0.14, fields at 0.105.
    OmarchyColors result{.dark = true,
            .accent = QColor(0x0a, 0x84, 0xff),
            .selection = QColor(0x3a, 0x3a, 0x3a),
            .muted = QColor(0x4a, 0x4a, 0x4a),
            .background = QColor(0x24, 0x24, 0x24),
            .darkBackground = QColor(0x1e, 0x1e, 0x1e),
            .darkerBackground = QColor(0x1b, 0x1b, 0x1b),
            .lighterBackground = QColor(0x30, 0x30, 0x30),
            .foreground = QColor(0xe5, 0xe5, 0xe5),
            .darkForeground = QColor(0x8a, 0x8a, 0x8a),
            .lightForeground = QColor(0xcc, 0xcc, 0xcc),
            .brightForeground = QColor(0xff, 0xff, 0xff),
            .red = QColor(0xff, 0x45, 0x3a),
            .warning = QColor(0xff, 0x9f, 0x0a)};
    fillComponents(result, nullptr);
    return result;
}

QString OmarchyColors::labelFamily() const
{
    // Graphite sets labels in SF Pro and values in SF Mono (its theme-direction brief).
    return components && QFontDatabase::hasFamily(QStringLiteral("SF Pro Text")) ? QStringLiteral("SF Pro Text") : QString();
}

QString OmarchyColors::valueFamily() const
{
    return components && QFontDatabase::hasFamily(QStringLiteral("SFMono Nerd Font Mono")) ? QStringLiteral("SFMono Nerd Font Mono") : QString();
}

QPalette OmarchyColors::palette() const
{
    QPalette result;
    result.setColor(QPalette::Window, background);
    result.setColor(QPalette::WindowText, foreground);
    result.setColor(QPalette::Base, darkerBackground);
    result.setColor(QPalette::AlternateBase, darkBackground);
    result.setColor(QPalette::Text, foreground);
    result.setColor(QPalette::Button, lighterBackground);
    result.setColor(QPalette::ButtonText, foreground);
    result.setColor(QPalette::Highlight, accent);
    result.setColor(QPalette::HighlightedText, background);
    result.setColor(QPalette::Link, accent);
    result.setColor(QPalette::PlaceholderText, darkForeground);
    result.setColor(QPalette::BrightText, warning);
    result.setColor(QPalette::Mid, muted);
    result.setColor(QPalette::Dark, darkerBackground);
    result.setColor(QPalette::Light, lighterBackground);
    result.setColor(QPalette::ToolTipBase, darkBackground);
    result.setColor(QPalette::ToolTipText, foreground);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        result.setColor(QPalette::Disabled, role, muted);
    if (!components)
        return result;
    // A theme with components: the panel tone for the chrome, the group tone for controls, text in its tones.
    result.setColor(QPalette::Window, surface1);
    result.setColor(QPalette::Base, surface1);
    result.setColor(QPalette::AlternateBase, surface2);
    result.setColor(QPalette::Button, surface2);
    result.setColor(QPalette::Light, lift);
    result.setColor(QPalette::Midlight, surface3);
    result.setColor(QPalette::Mid, surface3);
    result.setColor(QPalette::Dark, surface0);
    result.setColor(QPalette::WindowText, text2);
    result.setColor(QPalette::Text, text2);
    result.setColor(QPalette::ButtonText, text2);
    result.setColor(QPalette::PlaceholderText, text4);
    result.setColor(QPalette::HighlightedText, onAccent);
    result.setColor(QPalette::ToolTipBase, lift);
    result.setColor(QPalette::ToolTipText, text1);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        result.setColor(QPalette::Disabled, role, text4);
    return result;
}

QString OmarchyTheme::defaultDirectory()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

OmarchyTheme::OmarchyTheme(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory), m_colors(OmarchyColors::builtInDark()), m_style(new OmarchyStyle), m_watcher(this), m_settle(this)
{
    // Fusion underneath follows the palette on every desktop; the application owns the style.
    QApplication::setStyle(m_style);
    // A switch removes, moves, writes: one load after the last.
    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, &OmarchyTheme::apply);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    // The built-in dark stands until a file is read whole.
    use(m_colors);
    apply();
}

void OmarchyTheme::apply()
{
    const QString path = m_directory + QStringLiteral("/theme/colors.toml");
    watch();
    QFile file(path);
    OmarchyColors loaded = OmarchyColors::builtInDark();
    // Only a path leading nowhere is absent; barred keeps.
    struct stat status;
    if (::stat(QFile::encodeName(path).constData(), &status) != 0 && (errno == ENOENT || errno == ENOTDIR)) {
        qCInfo(lcApp).noquote() << "no Omarchy theme at" << path << "- built-in dark";
    } else if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette:" << file.errorString() << path;
        return;
    } else {
        // A read cut short must not pass for the file.
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError || bytes.size() != file.size()) {
            qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette: read" << bytes.size() << "of" << file.size() << "bytes" << path;
            return;
        }
        try {
            loaded = OmarchyColors::parse(QString::fromUtf8(bytes));
        } catch (const std::runtime_error &error) {
            qCWarning(lcApp).noquote() << "Omarchy theme unreadable, keeping the palette:" << error.what();
            return;
        }
        QFile name(m_directory + QStringLiteral("/theme.name"));
        const QString called = name.open(QIODevice::ReadOnly) ? QString::fromUtf8(name.readAll()).trimmed() : QStringLiteral("(unnamed)");
        qCInfo(lcApp).noquote() << "Omarchy theme" << called << (loaded.dark ? "dark" : "light") << "from" << path;
    }
    if (loaded == m_colors)
        return;
    m_colors = loaded;
    use(m_colors);
}

void OmarchyTheme::use(const OmarchyColors &colors)
{
    m_style->setColors(colors);
    // The theme's type where it names one, at the size its rows use; the font the app started with otherwise.
    static const QFont started = QApplication::font();
    QFont font = started;
    if (const QString family = colors.labelFamily(); !family.isEmpty()) {
        font.setFamilies({family});
        font.setPixelSize(13);
    }
    QApplication::setFont(font);
    QApplication::setPalette(colors.palette());
}

void OmarchyTheme::watch()
{
    // Removed paths leave the watcher: add them back each load.
    QStringList wanted{m_directory, m_directory + QStringLiteral("/theme"), m_directory + QStringLiteral("/theme/colors.toml")};
    // A file's watch follows its inode: renew it each load.
    if (m_watcher.files().contains(wanted.last()))
        m_watcher.removePath(wanted.last());
    for (const QString &path : wanted) {
        if (!m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }
    // Missing or barred, the state directory's nearest watchable ancestor tells.
    QString ancestor = m_directory;
    while (!m_watcher.directories().contains(m_directory) && ancestor.contains(QLatin1Char('/'))) {
        ancestor = ancestor.section(QLatin1Char('/'), 0, -2);
        const QString candidate = ancestor.isEmpty() ? QStringLiteral("/") : ancestor;
        if (m_watcher.directories().contains(candidate) || m_watcher.addPath(candidate)) {
            wanted.prepend(candidate);
            break;
        }
    }
    for (const QString &stale : m_watcher.directories()) {
        if (!wanted.contains(stale))
            m_watcher.removePath(stale);
    }
}
