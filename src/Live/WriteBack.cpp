#include "Live/WriteBack.h"
#include "Agent/Setup.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <map>

namespace {
// Source files a page's markup, classes and styles are written in.
const QStringList sourceSuffixes{"html", "htm", "js", "jsx", "mjs", "cjs", "ts", "tsx", "vue", "svelte", "astro", "md", "mdx",
                                 "css", "scss", "sass", "less", "php", "erb", "liquid", "njk", "hbs", "twig", "hbs", "pug", "ejs"};
const QStringList styleSuffixes{"css", "scss", "sass", "less", "html", "htm", "vue", "svelte", "astro"};
constexpr qint64 largestSource = 2 * 1024 * 1024;

QString describe(const LiveEdit &edit)
{
    if (edit.property == QLatin1String("text"))
        return QStringLiteral("Change the text “%1” to “%2”").arg(edit.before.trimmed().left(60), edit.after.trimmed().left(60));
    if (!edit.addClass.isEmpty())
        return QStringLiteral("%1: %2 to %3").arg(edit.selector, edit.removeClass, edit.addClass);
    return QStringLiteral("%1: %2 to %3").arg(edit.selector, edit.property, edit.after);
}

int count(const QString &text, const QString &needle)
{
    return needle.isEmpty() ? 0 : int(text.count(needle));
}

// The project's source text, read once per plan and edited in place as edits are planned.
struct Sources {
    QString folder;
    QStringList paths;
    std::map<QString, QString> text;
    std::map<QString, QByteArray> original;

    QString &of(const QString &path)
    {
        auto found = text.find(path);
        if (found == text.end()) {
            QFile file(QDir(folder).filePath(path));
            QByteArray bytes;
            if (file.open(QIODevice::ReadOnly))
                bytes = file.readAll();
            original[path] = bytes;
            found = text.emplace(path, QString::fromUtf8(bytes)).first;
        }
        return found->second;
    }

    // Where `needle` occurs across `candidates`: {path, count} for each file that has it.
    std::vector<std::pair<QString, int>> find(const QString &needle, const QStringList &candidates)
    {
        std::vector<std::pair<QString, int>> found;
        for (const QString &path : candidates) {
            if (const int times = count(of(path), needle))
                found.emplace_back(path, times);
        }
        return found;
    }
};

// data-oma-src="src/App.tsx:12:5", from the element's markup: the Vite helper's precise location.
std::optional<std::pair<QString, int>> sourceLocation(const QJsonObject &element)
{
    static const QRegularExpression attribute(QStringLiteral(R"(data-oma-src="([^":]+):(\d+))"));
    const auto match = attribute.match(element["html"].toString().left(600));
    if (!match.hasMatch())
        return std::nullopt;
    return std::pair(match.captured(1), match.captured(2).toInt());
}

// Replaces the one `needle` in `path`, only between `lines` around `line` when given. False if not exactly one.
bool replaceOnce(QString &text, const QString &needle, const QString &replacement, int line = 0)
{
    if (line <= 0) {
        if (count(text, needle) != 1)
            return false;
        text.replace(text.indexOf(needle), needle.size(), replacement);
        return true;
    }
    // Two lines either side: tags often wrap.
    qsizetype start = 0;
    for (int at = 1; at < line - 2 && start >= 0; ++at)
        start = text.indexOf(QLatin1Char('\n'), start) + 1;
    qsizetype end = start;
    for (int at = 0; at < 5 && end >= 0 && end < text.size(); ++at)
        end = text.indexOf(QLatin1Char('\n'), end + 1);
    if (end < 0)
        end = text.size();
    const QString window = text.mid(start, end - start);
    if (count(window, needle) != 1)
        return false;
    text.replace(start + window.indexOf(needle), needle.size(), replacement);
    return true;
}
}

namespace WriteBack {
QString git(const QString &folder, const QStringList &arguments, QString *error, int timeoutMs)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(timeoutMs) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error && error->isEmpty())
            *error = QStringLiteral("git %1 failed.").arg(arguments.value(0));
        return {};
    }
    if (error)
        error->clear();
    return QString::fromUtf8(process.readAllStandardOutput());
}

bool isGitRepository(const QString &folder)
{
    QString error;
    return git(folder, {QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}, &error).trimmed() == QLatin1String("true");
}

QStringList trackedFiles(const QString &folder)
{
    QStringList files;
    if (isGitRepository(folder)) {
        // Tracked, plus untracked but not ignored: a new page is still the project's.
        QString error;
        files = git(folder, {QStringLiteral("ls-files"), QStringLiteral("--cached"), QStringLiteral("--others"), QStringLiteral("--exclude-standard")}, &error)
                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    } else {
        QDirIterator walk(folder, QDir::Files, QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString path = QDir(folder).relativeFilePath(walk.next());
            static const QRegularExpression skipped(QStringLiteral("(^|/)(node_modules|dist|build|\\.[^/]+)/"));
            if (!skipped.match(path).hasMatch())
                files << path;
        }
    }
    files.removeDuplicates();
    files.sort();
    return files;
}

QStringList dirtyFiles(const QString &folder, const QStringList &paths)
{
    if (!isGitRepository(folder))
        return {};
    QStringList arguments{QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("--untracked-files=no"), QStringLiteral("--")};
    arguments << paths;
    QStringList dirty;
    for (const QString &line : git(folder, arguments).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        dirty << line.mid(3).trimmed();
    return dirty;
}

QString Review::diff(const QString &folder) const
{
    QString text;
    for (const FileChange &change : changes)
        text += Setup::unifiedDiff(QDir(folder).relativeFilePath(change.path), change.before, change.after);
    return text;
}

Plan plan(const QString &folder, const std::vector<LiveEdit> &edits)
{
    Plan result;
    Sources sources{folder, {}, {}, {}};
    for (const QString &path : trackedFiles(folder)) {
        const QFileInfo info(QDir(folder).filePath(path));
        if (sourceSuffixes.contains(info.suffix().toLower()) && info.size() <= largestSource)
            sources.paths << path;
    }
    QStringList styleFiles;
    for (const QString &path : std::as_const(sources.paths))
        if (styleSuffixes.contains(QFileInfo(path).suffix().toLower()))
            styleFiles << path;

    // Each element's class swaps become one change to its class attribute, made in place.
    std::vector<QString> order;
    std::map<QString, std::vector<const LiveEdit *>> classEdits;
    for (const LiveEdit &edit : edits) {
        if (edit.property == QLatin1String("text")) {
            const QString before = edit.before.trimmed();
            const QString after = edit.after.trimmed();
            static const QRegularExpression markup(QStringLiteral("[<>&{}]"));
            const auto location = sourceLocation(edit.element);
            bool written = false;
            if (!before.isEmpty() && !markup.match(before + after).hasMatch()) {
                if (location && sources.paths.contains(location->first))
                    written = replaceOnce(sources.of(location->first), before, after, location->second);
                else if (const auto found = sources.find(before, sources.paths); found.size() == 1 && found.front().second == 1)
                    written = replaceOnce(sources.of(found.front().first), before, after);
            }
            if (written)
                result.done << describe(edit);
            else
                result.unresolved.push_back(edit);
        } else if (edit.property.startsWith(QLatin1String("--"))) {
            // A custom property's value, declared in exactly one stylesheet.
            const QRegularExpression declaration(QStringLiteral("(%1\\s*:\\s*)([^;}\\n]+)").arg(QRegularExpression::escape(edit.property)));
            QString only;
            int total = 0;
            for (const QString &path : std::as_const(styleFiles)) {
                const int times = int(sources.of(path).count(declaration));
                total += times;
                if (times)
                    only = path;
            }
            if (total == 1) {
                QString &text = sources.of(only);
                const auto match = declaration.match(text);
                text.replace(match.capturedStart(2), match.capturedLength(2), edit.after);
                result.done << QStringLiteral("%1 to %2").arg(edit.property, edit.after);
            } else {
                result.unresolved.push_back(edit);
            }
        } else if (!edit.removeClass.isEmpty() && !edit.addClass.isEmpty()) {
            if (!classEdits.count(edit.selector))
                order.push_back(edit.selector);
            classEdits[edit.selector].push_back(&edit);
        } else {
            result.unresolved.push_back(edit);
        }
    }
    for (const QString &selector : order) {
        const std::vector<const LiveEdit *> &swaps = classEdits[selector];
        // The class attribute as the source has it, before any of this session's swaps.
        const QString original = swaps.front()->element["classes"].toString();
        QStringList classes = original.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const LiveEdit *edit : swaps) {
            const qsizetype at = classes.indexOf(edit->removeClass);
            if (at >= 0)
                classes[at] = edit->addClass;
        }
        const QString replacement = classes.join(QLatin1Char(' '));
        bool written = false;
        const auto location = sourceLocation(swaps.front()->element);
        // Quoted, so "p-4" inside a longer class string elsewhere doesn't count.
        for (const QChar quote : {QLatin1Char('"'), QLatin1Char('\''), QLatin1Char('`')}) {
            const QString needle = quote + original + quote;
            if (location && sources.paths.contains(location->first))
                written = replaceOnce(sources.of(location->first), needle, quote + replacement + quote, location->second);
            else if (const auto found = sources.find(needle, sources.paths); found.size() == 1 && found.front().second == 1)
                written = replaceOnce(sources.of(found.front().first), needle, quote + replacement + quote);
            if (written)
                break;
        }
        for (const LiveEdit *edit : swaps) {
            if (written)
                result.done << describe(*edit);
            else
                result.unresolved.push_back(*edit);
        }
    }
    for (const auto &[path, text] : sources.text) {
        const QByteArray after = text.toUtf8();
        if (after != sources.original[path])
            result.changes.push_back({QDir(folder).filePath(path), sources.original[path], after});
    }
    return result;
}

QString apply(const std::vector<FileChange> &changes)
{
    for (const FileChange &change : changes) {
        if (!change.after) {
            if (QFileInfo::exists(change.path) && !QFile::remove(change.path))
                return QStringLiteral("Could not delete %1.").arg(change.path);
            continue;
        }
        QDir().mkpath(QFileInfo(change.path).absolutePath());
        QSaveFile file(change.path);
        if (!file.open(QIODevice::WriteOnly) || file.write(*change.after) != change.after->size() || !file.commit())
            return QStringLiteral("Could not write %1: %2").arg(change.path, file.errorString());
    }
    return {};
}

QString restore(const std::vector<FileChange> &changes)
{
    std::vector<FileChange> reversed;
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        reversed.push_back({it->path, it->after, it->before});
    return apply(reversed);
}

QString commitMessage(const QStringList &done)
{
    if (done.isEmpty())
        return QStringLiteral("Live edits from Omastrator");
    QString message = done.size() == 1 ? done.front() : QStringLiteral("Live edits from Omastrator");
    if (message.size() > 72)
        message = message.left(71) + QStringLiteral("…");
    if (done.size() > 1) {
        message += QStringLiteral("\n\n");
        for (const QString &line : done)
            message += QStringLiteral("- %1\n").arg(line);
    }
    return message;
}
}
