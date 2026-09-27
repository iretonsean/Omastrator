#include "Live/AgentWork.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString dataHome()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    return given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
}

std::optional<QByteArray> read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}
}

QString AgentWork::prepare()
{
    if (!WriteBack::isGitRepository(project))
        return QStringLiteral("Asking an agent to change the code needs the project in git, so its work can be kept apart and undone.");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    branch = QStringLiteral("omastrator/live-%1").arg(stamp);
    worktree = QDir(dataHome()).filePath(QStringLiteral("omastrator/worktrees/%1-%2").arg(QFileInfo(project).fileName(), stamp));
    QDir().mkpath(QFileInfo(worktree).absolutePath());
    QString error;
    WriteBack::git(project, {QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-b"), branch, worktree, QStringLiteral("HEAD")}, &error, 120'000);
    if (!error.isEmpty())
        return QStringLiteral("Couldn't make a worktree for the agent: %1").arg(error);
    return {};
}

std::vector<WriteBack::FileChange> AgentWork::collect(QString *error)
{
    std::vector<WriteBack::FileChange> changes;
    // Everything the agent did, new files included, against the commit it started from.
    WriteBack::git(worktree, {QStringLiteral("add"), QStringLiteral("-A")}, error);
    if (!error->isEmpty())
        return {};
    const QStringList changed = WriteBack::git(worktree, {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only"), QStringLiteral("HEAD")}, error)
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (!error->isEmpty() || changed.isEmpty())
        return {};
    const QStringList dirty = WriteBack::dirtyFiles(project, changed);
    if (!dirty.isEmpty() && !confirmedDirty) {
        *error = QStringLiteral("The agent changed files you have also changed and not committed: %1. Commit or stash them, "
                                "or confirm to merge the agent's changes into yours.")
                     .arg(dirty.join(QStringLiteral(", ")));
        return {};
    }
    for (const QString &path : changed) {
        const QString mine = QDir(project).filePath(path);
        WriteBack::FileChange change{mine, read(mine), read(QDir(worktree).filePath(path))};
        const std::optional<QByteArray> base = WriteBack::committed(worktree, path);
        if (change.before != base && change.before && change.after && base) {
            // The file as it is now (the user's edits, or Omastrator's own) and the agent's, merged against the commit it started from.
            const auto merged = WriteBack::merge(*base, *change.before, *change.after);
            if (!merged) {
                *error = QStringLiteral("The agent's changes to %1 clash with your uncommitted ones. Commit or stash yours, then try again.").arg(path);
                return {};
            }
            change.after = merged;
        }
        changes.push_back(change);
    }
    return changes;
}

void AgentWork::cleanup()
{
    if (worktree.isEmpty())
        return;
    QString error;
    WriteBack::git(project, {QStringLiteral("worktree"), QStringLiteral("remove"), QStringLiteral("--force"), worktree}, &error);
    WriteBack::git(project, {QStringLiteral("branch"), QStringLiteral("-D"), branch}, &error);
    worktree.clear();
}

QString AgentWork::prompt(const Brief &brief) const
{
    QString text = QStringLiteral(
                       "This is an Omastrator Live task (request %1). You are changing a web project's source code.\n\n"
                       "Work only in this folder, a git worktree on its own branch (%2): %3\n"
                       "Don't commit, push, deploy or start a dev server. Omastrator writes your change into the project and commits "
                       "it; the user can review or discard it later.\n\n"
                       "The page: %4\n")
                       .arg(requestId, branch, worktree, brief.url);
    if (!brief.instruction.isEmpty())
        text += QStringLiteral("\nThe user asked, about the selected elements: %1\n").arg(brief.instruction);
    if (!brief.edits.empty()) {
        text += QStringLiteral("\nThe user changed these on the live page. Make the source produce the same result:\n");
        for (const LiveEdit &edit : brief.edits) {
            text += edit.property == QLatin1String("text")
                        ? QStringLiteral("- %1: text \"%2\" becomes \"%3\"\n").arg(edit.selector, edit.before, edit.after)
                        : QStringLiteral("- %1: %2 %3 → %4%5\n")
                              .arg(edit.selector, edit.property, edit.before.isEmpty() ? QStringLiteral("(unset)") : edit.before, edit.after,
                                   edit.token.isEmpty() ? QString() : QStringLiteral(" (the project's token %1)").arg(edit.token));
        }
    }
    if (!brief.elements.isEmpty()) {
        text += QStringLiteral("\nThe selected elements, as the browser sees them (selector, classes, computed styles, markup):\n");
        text += QString::fromUtf8(QJsonDocument(brief.elements).toJson(QJsonDocument::Indented)).left(40'000);
    }
    if (!brief.screenshot.isEmpty())
        text += QStringLiteral("\nA screenshot of the selection: %1 (look at it).\n").arg(brief.screenshot);
    text += QStringLiteral(
                "\nUse the project's own tokens: its Tailwind classes, CSS custom properties or design tokens, the way the "
                "surrounding code does. Keep the change as small as it can be.\n\n"
                "When you're done, run:\n  %1 agent live '{\"action\": \"agentDone\", \"requestId\": \"%2\", \"summary\": \"<one line on what changed>\"}'\n"
                "If you can't do it, run the same with a summary that says why, and change nothing.\n")
                .arg(brief.command, requestId);
    return text;
}

QString AgentWork::handoffPrompt(const QString &instruction, const QString &png, const QString &svg, const QString &command) const
{
    return QStringLiteral(
               "This is an Omastrator hand-off (request %1). The user redesigned part of this app's interface in Omastrator and "
               "wants the source changed to match.\n\n"
               "Work only in this folder, a git worktree on its own branch (%2): %3\n"
               "Don't commit, push or deploy. Omastrator writes your change into the project; the user can review or discard "
               "it later.\n\n"
               "The mockup: %4 (look at it), and the same as SVG: %5\n"
               "%6\n"
               "Find where this interface is built (widgets, QML, GTK builder files, CSS, or web views), and change it to match the "
               "mockup using the toolkit's own styling and the project's conventions. Keep the change as small as it can be.\n\n"
               "When you're done, run:\n  %7 agent live '{\"action\": \"agentDone\", \"requestId\": \"%1\", \"summary\": \"<one line on what changed>\"}'\n"
               "If you can't do it, run the same with a summary that says why, and change nothing.\n")
        .arg(requestId, branch, worktree, png, svg,
             instruction.isEmpty() ? QString() : QStringLiteral("The user says: %1\n").arg(instruction), command);
}
