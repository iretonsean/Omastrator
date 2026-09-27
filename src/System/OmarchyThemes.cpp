#include "System/OmarchyThemes.h"
#include "System/TokenFiles.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

// Omarchy's "rgba(0a84ffb3)" and plain hex, as well as CSS.
std::optional<QColor> themeColor(const QString &value)
{
    static const QRegularExpression hyprland(QStringLiteral(R"(^rgba?\(([0-9a-fA-F]{6,8})\)$)"));
    if (const auto match = hyprland.match(value.trimmed()); match.hasMatch())
        return TokenFiles::parseColor(QLatin1Char('#') + match.captured(1));
    return TokenFiles::parseColor(value);
}

struct Line {
    QString key;
    QString value;
    qsizetype valueStart = 0;
    qsizetype valueEnd = 0;
    QString section;
};

// Key = "value" and key = number lines, with where the value sits.
std::vector<Line> assignments(const QString &toml)
{
    std::vector<Line> lines;
    QString section;
    qsizetype start = 0;
    static const QRegularExpression pattern(QStringLiteral(R"re(^\s*([A-Za-z0-9_.-]+)\s*=\s*("([^"]*)"|'([^']*)'|([-0-9.]+)))re"));
    while (start <= toml.size()) {
        qsizetype end = toml.indexOf(QLatin1Char('\n'), start);
        if (end < 0)
            end = toml.size();
        const QString line = toml.mid(start, end - start);
        if (line.trimmed().startsWith(QLatin1Char('['))) {
            section = line.trimmed().mid(1).section(QLatin1Char(']'), 0, 0);
        } else if (const auto match = pattern.match(line); match.hasMatch()) {
            const int group = match.hasCaptured(3) && !match.captured(3).isNull() ? 3 : match.hasCaptured(4) && !match.captured(4).isNull() ? 4 : 5;
            lines.push_back({match.captured(1), match.captured(group), start + match.capturedStart(group), start + match.capturedEnd(group), section});
        }
        start = end + 1;
    }
    return lines;
}

QString themeColorText(const QColor &color, const QString &was)
{
    if (was.startsWith(QLatin1String("rgba(")) && !was.contains(QLatin1Char(','))) {
        const QString hex = color.name(QColor::HexRgb).mid(1);
        return QStringLiteral("rgba(%1%2)").arg(hex, QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0')));
    }
    return TokenFiles::cssColor(color);
}

QString keyOf(const DesignToken &token)
{
    QString key = token.name.section(QLatin1Char('/'), 1);
    if (key.isEmpty())
        key = token.name;
    return key.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('-'), QLatin1Char('_'));
}
}

namespace OmarchyThemes {
QString omarchy()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_OMARCHY");
    return overridden.isEmpty() ? QStringLiteral("omarchy") : overridden;
}

QString currentDirectory()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme");
}

QString currentName()
{
    return QString::fromUtf8(contents(QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme.name")).value_or(QByteArray())).trimmed();
}

QString slug(const QString &name)
{
    QString result;
    for (const QChar c : name.trimmed().toLower()) {
        if (c.isLetterOrNumber())
            result += c;
        else if (!result.isEmpty() && !result.endsWith(QLatin1Char('-')))
            result += QLatin1Char('-');
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result;
}

QString directoryOf(const QString &name)
{
    const QString user = QDir::homePath() + QStringLiteral("/.config/omarchy/themes/") + slug(name);
    if (QFileInfo(user).isDir())
        return user;
    const QString shipped = QStringLiteral("/usr/share/omarchy/themes/") + slug(name);
    if (QFileInfo(shipped).isDir())
        return shipped;
    return user;
}

Theme read(const QString &directory, const QString &name)
{
    Theme theme{name.isEmpty() ? QFileInfo(directory).fileName() : name, directory, {}, QStringLiteral("dark")};
    const QString colors = QString::fromUtf8(contents(QDir(directory).filePath(QStringLiteral("colors.toml"))).value_or(QByteArray()));
    for (const Line &line : assignments(colors)) {
        if (line.key == QLatin1String("mode")) {
            theme.mode = line.value;
            continue;
        }
        if (!line.section.isEmpty())
            continue;
        if (const auto colour = themeColor(line.value))
            theme.tokens.push_back(DesignToken::color(QStringLiteral("color/") + QString(line.key).replace(QLatin1Char('_'), QLatin1Char('-')), *colour));
    }
    const QString spacing = QString::fromUtf8(contents(QDir(directory).filePath(QStringLiteral("shell.spacing.toml"))).value_or(QByteArray()));
    for (const Line &line : assignments(spacing)) {
        bool number = false;
        const double value = line.value.toDouble(&number);
        if (number && line.key != QLatin1String("scale"))
            theme.tokens.push_back(DesignToken::number(TokenKind::spacing, QStringLiteral("spacing/") + line.key, value));
    }
    return theme;
}

QByteArray writeColors(const QByteArray &bytes, const std::vector<DesignToken> &tokens)
{
    QString toml = QString::fromUtf8(bytes);
    const std::vector<Line> lines = assignments(toml);
    struct Edit {
        qsizetype from;
        qsizetype to;
        QString text;
    };
    std::vector<Edit> edits;
    QString added;
    for (const DesignToken &token : tokens) {
        if (token.kind != TokenKind::color)
            continue;
        const QString key = keyOf(token);
        const auto found = std::find_if(lines.begin(), lines.end(), [&](const Line &line) { return line.section.isEmpty() && line.key == key; });
        if (found == lines.end()) {
            added += QStringLiteral("%1 = \"%2\"\n").arg(key, TokenFiles::cssColor(token.value.color));
        } else if (themeColor(found->value) != token.value.color) {
            edits.push_back({found->valueStart, found->valueEnd, themeColorText(token.value.color, found->value)});
        }
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.from > b.from; });
    for (const Edit &edit : edits)
        toml.replace(edit.from, edit.to - edit.from, edit.text);
    if (!added.isEmpty()) {
        // Before the first table, where top-level keys belong.
        const qsizetype table = toml.indexOf(QRegularExpression(QStringLiteral("^\\s*\\["), QRegularExpression::MultilineOption));
        if (!toml.isEmpty() && !toml.endsWith(QLatin1Char('\n')))
            toml += QLatin1Char('\n');
        if (table < 0)
            toml += QStringLiteral("\n# Added in Omastrator.\n") + added;
        else
            toml.insert(table, QStringLiteral("# Added in Omastrator.\n") + added + QLatin1Char('\n'));
    }
    return toml.toUtf8();
}

QByteArray writeSpacing(const QByteArray &bytes, const std::vector<DesignToken> &tokens)
{
    QString toml = QString::fromUtf8(bytes);
    const std::vector<Line> lines = assignments(toml);
    std::vector<std::pair<const Line *, QString>> edits;
    for (const DesignToken &token : tokens) {
        if (token.kind != TokenKind::spacing || !token.name.startsWith(QLatin1String("spacing/")))
            continue;
        const QString key = token.name.mid(8);
        for (const Line &line : lines) {
            if (line.key == key && line.value.toDouble() != token.value.number)
                edits.emplace_back(&line, QString::number(token.value.number));
        }
    }
    std::sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) { return a.first->valueStart > b.first->valueStart; });
    for (const auto &[line, text] : edits)
        toml.replace(line->valueStart, line->valueEnd - line->valueStart, text);
    return toml.toUtf8();
}

SyncPlan savePlan(const QString &source, const QString &name, const std::vector<DesignToken> &tokens, bool apply)
{
    SyncPlan plan;
    plan.title = apply ? QStringLiteral("Save and Apply Omarchy Theme") : QStringLiteral("Save Omarchy Theme");
    const QString themeSlug = slug(name);
    if (themeSlug.isEmpty()) {
        plan.problem = QStringLiteral("Give the theme a name.");
        return plan;
    }
    const QString target = QDir::homePath() + QStringLiteral("/.config/omarchy/themes/") + themeSlug;
    const QString from = QFileInfo(source).canonicalFilePath();
    if (from.isEmpty() || !QFileInfo(from).isDir()) {
        plan.problem = QStringLiteral("The theme to start from, %1, isn't there.").arg(source);
        return plan;
    }
    const bool inPlace = QFileInfo(target).canonicalFilePath() == from;
    if (!inPlace && QFileInfo(target).exists()) {
        plan.problem = QStringLiteral("There's already a theme at %1. Choose another name, or edit that theme.").arg(target);
        return plan;
    }
    // Everything else in the theme comes along as it is.
    QDirIterator walk(from, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        const QString relative = QDir(from).relativeFilePath(path);
        const QString destination = QDir(target).filePath(relative);
        if (relative == QLatin1String("colors.toml")) {
            const QByteArray before = contents(path).value_or(QByteArray());
            plan.writes.push_back({destination, inPlace ? std::optional(before) : std::nullopt, writeColors(before, tokens), {}});
        } else if (relative == QLatin1String("shell.spacing.toml")) {
            const QByteArray before = contents(path).value_or(QByteArray());
            plan.writes.push_back({destination, inPlace ? std::optional(before) : std::nullopt, writeSpacing(before, tokens), {}});
        } else if (!inPlace && !QFileInfo(path).isSymLink()) {
            plan.writes.push_back({destination, std::nullopt, {}, path});
        }
    }
    if (!std::any_of(plan.writes.begin(), plan.writes.end(), [](const FileWrite &w) { return w.path.endsWith(QLatin1String("/colors.toml")); }))
        plan.writes.push_back({QDir(target).filePath(QStringLiteral("colors.toml")), std::nullopt, writeColors({}, tokens), {}});
    if (apply)
        plan.command = {omarchy(), QStringLiteral("theme"), QStringLiteral("set"), themeSlug};
    plan.destination = (inPlace ? QStringLiteral("The Omarchy theme “%1”, changed in place at %2.") : QStringLiteral("A new Omarchy theme “%1” at %2."))
                           .arg(name.trimmed(), target)
        + (apply ? QStringLiteral(" Then the whole desktop switches to it.") : QStringLiteral(" The desktop keeps its current theme."))
        + QStringLiteral(" Nothing is committed or published.");
    return plan;
}

SyncPlan pullPlan(const QString &directory, const QString &name, const QString &documentName, std::function<QString(const Theme &)> apply)
{
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::pull;
    plan.title = QStringLiteral("Use Omarchy Theme");
    const Theme theme = read(directory, name);
    if (theme.tokens.empty()) {
        plan.problem = QStringLiteral("No colours were found in %1/colors.toml.").arg(directory);
        return plan;
    }
    plan.reads = {QDir(directory).filePath(QStringLiteral("colors.toml"))};
    if (QFileInfo(QDir(directory).filePath(QStringLiteral("shell.spacing.toml"))).exists())
        plan.reads.append(QDir(directory).filePath(QStringLiteral("shell.spacing.toml")));
    plan.destination = QStringLiteral("This document, %1. No files are written, nothing is committed and the desktop isn't changed.").arg(documentName);
    plan.inApp = QStringLiteral("Merges %1 tokens from “%2” by name into %3 as one undo step.").arg(theme.tokens.size()).arg(theme.name, documentName);
    plan.apply = [apply, theme] { return apply(theme); };
    return plan;
}
}
