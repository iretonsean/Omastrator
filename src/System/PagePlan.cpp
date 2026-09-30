#include "System/PagePlan.h"
#include <QDir>

namespace PagePlan {
QString commitMessage()
{
    return QStringLiteral("First page from Omastrator");
}

SyncPlan make(const QString &folder, PageTemplates::Stack stack, const std::vector<PageTemplates::File> &files, const QString &frame)
{
    const QString where = QDir::cleanPath(QDir(folder).absolutePath());
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::push;
    plan.title = QStringLiteral("Create a new project?");
    plan.destinationLabel = QStringLiteral("Creates");
    plan.destination = where;
    for (const PageTemplates::File &file : files)
        plan.writes.emplace_back(QDir(where).filePath(file.path), std::nullopt, file.bytes);
    plan.git = GitTarget{where, QStringLiteral("main"), true, commitMessage(), true};
    plan.runsAfter = PageTemplates::runLines(stack);
    plan.inApp = QStringLiteral("%1 shows the page from its dev server. One undo step.").arg(frame);
    plan.confirmLabel = QStringLiteral("Create Project");
    plan.problem = PageTemplates::folderProblem(where);
    return plan;
}
}
