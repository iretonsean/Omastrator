#pragma once
#include "Live/PageTemplates.h"
#include "System/SyncPlan.h"
#include <vector>

// The plan Generate a page shows before it makes a project (docs/MOTION.md, section 4): the new folder, every file with its
// line count, the repository it starts with one commit, and what runs afterwards. Nothing exists until it is confirmed.
namespace PagePlan {
// "First page from Omastrator", the commit's message.
QString commitMessage();
// `frame` names the Browser View in "In Omastrator". `apply` runs last (it starts the dev server); it is left empty here.
SyncPlan make(const QString &folder, PageTemplates::Stack stack, const std::vector<PageTemplates::File> &files, const QString &frame);
}
