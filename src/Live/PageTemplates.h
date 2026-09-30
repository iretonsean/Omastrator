#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// The starting files of Generate a page (docs/MOTION.md, section 4): one small template per stack, kept as text in the
// code, and the reading of a staging folder after the agent has written into it.
namespace PageTemplates {
enum class Stack { viteTailwind, plainHtml, astro };

struct File {
    // Relative to the project folder, with forward slashes.
    QString path;
    QByteArray bytes;
};

// The sheet's choices, in its order.
std::vector<Stack> stacks();
// "Vite + Tailwind", "Plain HTML", "Astro".
QString label(Stack stack);
// "vite", "html", "astro": what the agent and the CLI call it.
QString id(Stack stack);
std::optional<Stack> fromId(const QString &id);

// The template's files for a project called `name`. Every file is text.
std::vector<File> files(Stack stack, const QString &name);
// The file whose tokens the design system's go into, and whether it is a Tailwind v4 `@theme` file (else `:root` variables).
QString tokenFile(Stack stack);
bool tokensAreTailwind(Stack stack);
// What the plan says it runs after the files are written, one line each.
QStringList runLines(Stack stack);
// The way the agent should think of the stack, in one sentence.
QString describe(Stack stack);

// "A landing page for a small coffee roaster" → "coffee-roaster": a folder name from the words that name the thing.
QString slug(const QString &text);

// $XDG_CACHE_HOME/omastrator/generate, where each run stages its files.
QString stagingRoot();
// A new empty folder under stagingRoot(); empty with `error` set when it couldn't be made.
QString makeStaging(QString *error);
// Writes `files` under `folder`. Returns why it couldn't, or empty.
QString write(const QString &folder, const std::vector<File> &files);
// What the agent left in `folder`, sorted by path, without .git, node_modules, build output or links. `problem` says why the
// result can't be used (too many files, too large); an empty list with no problem is an empty folder.
std::vector<File> collect(const QString &folder, QString *problem);
// Why a new project can't go in `folder` (it isn't empty, or it is a file), or empty when it can.
QString folderProblem(const QString &folder);
}
