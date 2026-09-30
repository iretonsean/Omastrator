#include "Live/MotionContract.h"
#include "Live/MotionCode.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

namespace MotionContract {
namespace {
QString readText(const QString &path)
{
    QFile file(path);
    return file.size() < 2'000'000 && file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// The text of every style and markup file in the folder that isn't a build product.
QList<QPair<QString, QString>> styleTexts(const QString &folder)
{
    QList<QPair<QString, QString>> all;
    const QDir dir(folder);
    static const QRegularExpression skipped(QStringLiteral("(^|/)(node_modules|dist|build|out|\\.[^/]+)/"));
    QDirIterator walk(folder, {QStringLiteral("*.css"), QStringLiteral("*.scss"), QStringLiteral("*.html"), QStringLiteral("*.astro"),
                               QStringLiteral("*.vue"), QStringLiteral("*.svelte")},
                      QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext() && all.size() < 800) {
        const QString relative = dir.relativeFilePath(walk.next());
        if (!skipped.match(relative).hasMatch())
            all.append({relative, readText(dir.filePath(relative))});
    }
    return all;
}
}

QString Report::notice() const
{
    return ok() ? QString() : QStringLiteral("Some of this motion isn't tunable here. Ask to fix it, or edit it in code.");
}

Report check(const QString &project, const QString &worktree, const QStringList &changed, bool reducedMotion)
{
    Report report;
    // A block the agent wrote or changed is one the project doesn't have as it is.
    const QList<MotionCode::Block> before = MotionCode::blocks(project);
    QList<MotionCode::Block> mine;
    for (const MotionCode::Block &block : MotionCode::blocks(worktree)) {
        const bool known = std::any_of(before.cbegin(), before.cend(), [&](const MotionCode::Block &was) {
            return was.file == block.file && was.name == block.name && was.text == block.text;
        });
        if (!known) {
            mine.append(block);
            report.blocks << block.name;
        }
    }

    // A marker that opens and never closes is not a block: the reader leaves it out, so it is looked for on its own.
    static const QRegularExpression opens(QStringLiteral(R"(/\*\s*omastrator:motion\s+(?!end\b)[^\s*]+\s*\*/)"));
    static const QRegularExpression closes(QStringLiteral(R"(/\*\s*omastrator:motion\s+end\s*\*/)"));
    for (const QString &path : changed) {
        const QString text = readText(QDir(worktree).filePath(path));
        if (text.contains(QLatin1String("omastrator:motion")) && text.count(opens) != text.count(closes))
            report.problems << QStringLiteral("A marked block in %1 isn't closed with /* omastrator:motion end */.").arg(path);
    }
    if (mine.isEmpty()) {
        if (report.problems.isEmpty())
            report.problems << QStringLiteral("The motion isn't in a block marked /* omastrator:motion name */ … /* omastrator:motion end */.");
        return report;
    }

    const QList<QPair<QString, QString>> sheets = styleTexts(worktree);
    QSet<QString> declared;
    QHash<QString, int> keyframes;
    static const QRegularExpression declaration(QStringLiteral(R"((--[\w-]+)\s*:)"));
    static const QRegularExpression keyframe(QStringLiteral(R"(@keyframes\s+([\w-]+))"));
    for (const auto &sheet : sheets) {
        for (const QRegularExpressionMatch &match : declaration.globalMatch(sheet.second))
            declared.insert(match.captured(1));
        for (const QRegularExpressionMatch &match : keyframe.globalMatch(sheet.second))
            keyframes[match.captured(1)] += 1;
    }
    static const QRegularExpression used(QStringLiteral(R"(var\(\s*(--(?:duration|ease|stagger)-[\w-]+))"));
    for (const MotionCode::Block &block : std::as_const(mine)) {
        QSet<QString> reported;
        for (const QRegularExpressionMatch &match : used.globalMatch(block.text)) {
            const QString name = match.captured(1);
            if (!declared.contains(name) && !reported.contains(name)) {
                reported.insert(name);
                report.problems << QStringLiteral("%1 uses %2 and nothing declares it.").arg(block.name, name);
            }
        }
        QSet<QString> named;
        for (const QRegularExpressionMatch &match : keyframe.globalMatch(block.text)) {
            const QString name = match.captured(1);
            if (!named.contains(name) && keyframes.value(name) != 1) {
                named.insert(name);
                report.problems << QStringLiteral("@keyframes %1 is written %2 times; a name is used once.").arg(name).arg(keyframes.value(name));
            }
        }
        const bool moves = block.text.contains(QRegularExpression(QStringLiteral(R"(\banimation(-name)?\s*:|\btransition(-property)?\s*:)")));
        if (reducedMotion && moves && !block.reducedMotion)
            report.problems << QStringLiteral("%1 has no prefers-reduced-motion rule.").arg(block.name);
    }
    return report;
}
}
