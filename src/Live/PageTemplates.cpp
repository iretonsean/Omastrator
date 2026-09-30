#include "Live/PageTemplates.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace PageTemplates {
namespace {
constexpr int maxFiles = 200;
constexpr qint64 maxBytes = 8 * 1024 * 1024;

QByteArray text(const QString &content)
{
    return content.toUtf8();
}

const QString gitignore = QStringLiteral("node_modules\ndist\n.astro\n*.log\n.DS_Store\n");

std::vector<File> vite(const QString &given)
{
    // The name goes into markup: a folder called "A&B" is "A&amp;B" there.
    const QString name = given.toHtmlEscaped();
    return {
        {QStringLiteral("index.html"),
         text(QStringLiteral("<!doctype html>\n<html lang=\"en\">\n  <head>\n    <meta charset=\"utf-8\" />\n"
                             "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
                             "    <title>%1</title>\n    <link rel=\"stylesheet\" href=\"/src/style.css\" />\n  </head>\n"
                             "  <body class=\"font-sans\">\n    <main>\n      <h1>%1</h1>\n    </main>\n  </body>\n</html>\n")
                  .arg(name))},
        {QStringLiteral("src/style.css"),
         text(QStringLiteral("@import \"tailwindcss\";\n\n@theme {\n  --font-sans: system-ui, sans-serif;\n}\n"))},
        {QStringLiteral("package.json"),
         text(QStringLiteral("{\n  \"name\": \"%1\",\n  \"private\": true,\n  \"version\": \"0.0.0\",\n  \"type\": \"module\",\n"
                             "  \"scripts\": {\n    \"dev\": \"vite\",\n    \"build\": \"vite build\",\n    \"preview\": \"vite preview\"\n  },\n"
                             "  \"devDependencies\": {\n    \"@tailwindcss/vite\": \"^4.1.0\",\n    \"tailwindcss\": \"^4.1.0\",\n    \"vite\": \"^6.0.0\"\n  }\n}\n")
                  .arg(slug(given).isEmpty() ? QStringLiteral("page") : slug(given)))},
        {QStringLiteral("vite.config.js"),
         text(QStringLiteral("import { defineConfig } from 'vite'\nimport tailwindcss from '@tailwindcss/vite'\n\n"
                             "export default defineConfig({\n  plugins: [tailwindcss()],\n})\n"))},
        {QStringLiteral(".gitignore"), text(gitignore)},
    };
}

std::vector<File> plain(const QString &given)
{
    const QString name = given.toHtmlEscaped();
    return {
        {QStringLiteral("index.html"),
         text(QStringLiteral("<!doctype html>\n<html lang=\"en\">\n  <head>\n    <meta charset=\"utf-8\" />\n"
                             "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
                             "    <title>%1</title>\n    <link rel=\"stylesheet\" href=\"style.css\" />\n  </head>\n"
                             "  <body>\n    <main>\n      <h1>%1</h1>\n    </main>\n  </body>\n</html>\n")
                  .arg(name))},
        {QStringLiteral("style.css"),
         text(QStringLiteral(":root {\n  --font-sans: system-ui, sans-serif;\n}\n\nbody {\n  margin: 0;\n  font-family: var(--font-sans);\n}\n"))},
    };
}

std::vector<File> astro(const QString &given)
{
    const QString name = given.toHtmlEscaped();
    return {
        {QStringLiteral("src/pages/index.astro"),
         text(QStringLiteral("---\nimport '../styles/global.css';\n---\n<!doctype html>\n<html lang=\"en\">\n  <head>\n    <meta charset=\"utf-8\" />\n"
                             "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n    <title>%1</title>\n  </head>\n"
                             "  <body>\n    <main>\n      <h1>%1</h1>\n    </main>\n  </body>\n</html>\n")
                  .arg(name))},
        {QStringLiteral("src/styles/global.css"),
         text(QStringLiteral(":root {\n  --font-sans: system-ui, sans-serif;\n}\n\nbody {\n  margin: 0;\n  font-family: var(--font-sans);\n}\n"))},
        {QStringLiteral("package.json"),
         text(QStringLiteral("{\n  \"name\": \"%1\",\n  \"private\": true,\n  \"version\": \"0.0.0\",\n  \"type\": \"module\",\n"
                             "  \"scripts\": {\n    \"dev\": \"astro dev\",\n    \"build\": \"astro build\",\n    \"preview\": \"astro preview\"\n  },\n"
                             "  \"dependencies\": {\n    \"astro\": \"^5.0.0\"\n  }\n}\n")
                  .arg(slug(given).isEmpty() ? QStringLiteral("page") : slug(given)))},
        {QStringLiteral("astro.config.mjs"),
         text(QStringLiteral("import { defineConfig } from 'astro/config';\n\nexport default defineConfig({});\n"))},
        {QStringLiteral(".gitignore"), text(gitignore)},
    };
}

QString cacheHome()
{
    const QString given = qEnvironmentVariable("XDG_CACHE_HOME");
    return given.isEmpty() ? QDir::home().filePath(QStringLiteral(".cache")) : given;
}

bool skipped(const QString &relative)
{
    static const QStringList folders{QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral("dist"), QStringLiteral(".astro")};
    const QStringList parts = relative.split(QLatin1Char('/'));
    for (int i = 0; i < parts.size() - 1; ++i)
        if (folders.contains(parts[i]))
            return true;
    return parts.last() == QLatin1String(".DS_Store");
}
}

std::vector<Stack> stacks()
{
    return {Stack::viteTailwind, Stack::plainHtml, Stack::astro};
}

QString label(Stack stack)
{
    switch (stack) {
    case Stack::viteTailwind:
        return QStringLiteral("Vite + Tailwind");
    case Stack::plainHtml:
        return QStringLiteral("Plain HTML");
    case Stack::astro:
        return QStringLiteral("Astro");
    }
    return {};
}

QString id(Stack stack)
{
    switch (stack) {
    case Stack::viteTailwind:
        return QStringLiteral("vite");
    case Stack::plainHtml:
        return QStringLiteral("html");
    case Stack::astro:
        return QStringLiteral("astro");
    }
    return {};
}

std::optional<Stack> fromId(const QString &wanted)
{
    for (const Stack stack : stacks())
        if (id(stack) == wanted.toLower())
            return stack;
    return std::nullopt;
}

std::vector<File> files(Stack stack, const QString &name)
{
    switch (stack) {
    case Stack::viteTailwind:
        return vite(name);
    case Stack::plainHtml:
        return plain(name);
    case Stack::astro:
        return astro(name);
    }
    return {};
}

QString tokenFile(Stack stack)
{
    switch (stack) {
    case Stack::viteTailwind:
        return QStringLiteral("src/style.css");
    case Stack::plainHtml:
        return QStringLiteral("style.css");
    case Stack::astro:
        return QStringLiteral("src/styles/global.css");
    }
    return {};
}

bool tokensAreTailwind(Stack stack)
{
    return stack == Stack::viteTailwind;
}

QStringList runLines(Stack stack)
{
    if (stack == Stack::plainHtml)
        return {QStringLiteral("Omastrator's static server, on the folder")};
    return {QStringLiteral("npm install"), QStringLiteral("npm run dev")};
}

QString describe(Stack stack)
{
    switch (stack) {
    case Stack::viteTailwind:
        return QStringLiteral("Vite with Tailwind CSS v4: markup in index.html, styles in src/style.css (its @theme block holds the design tokens).");
    case Stack::plainHtml:
        return QStringLiteral("Plain HTML and CSS with no build step: index.html and style.css (custom properties on :root hold the design tokens).");
    case Stack::astro:
        return QStringLiteral("Astro: pages in src/pages, styles in src/styles/global.css (custom properties on :root hold the design tokens).");
    }
    return {};
}

QString slug(const QString &input)
{
    static const QStringList filler{QStringLiteral("a"),       QStringLiteral("an"),       QStringLiteral("the"),      QStringLiteral("for"),
                                    QStringLiteral("of"),      QStringLiteral("and"),      QStringLiteral("page"),     QStringLiteral("landing"),
                                    QStringLiteral("website"), QStringLiteral("site"),     QStringLiteral("homepage"), QStringLiteral("home"),
                                    QStringLiteral("small"),   QStringLiteral("simple"),   QStringLiteral("new"),      QStringLiteral("my"),
                                    QStringLiteral("our"),     QStringLiteral("with"),     QStringLiteral("to"),       QStringLiteral("that"),
                                    QStringLiteral("is"),      QStringLiteral("in"),       QStringLiteral("on"),       QStringLiteral("some"),
                                    QStringLiteral("bold"),    QStringLiteral("clean"),    QStringLiteral("modern")};
    static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
    QStringList words;
    for (const QString &word : input.toLower().split(separators, Qt::SkipEmptyParts)) {
        if (!filler.contains(word))
            words << word;
        if (words.size() == 3)
            break;
    }
    return words.join(QLatin1Char('-'));
}

QString stagingRoot()
{
    return QDir(cacheHome()).filePath(QStringLiteral("omastrator/generate"));
}

QString makeStaging(QString *error)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"));
    const QString folder = QDir(stagingRoot()).filePath(stamp);
    if (!QDir().mkpath(folder)) {
        *error = QStringLiteral("Couldn't make a folder for the page at %1.").arg(folder);
        return {};
    }
    return QFileInfo(folder).canonicalFilePath();
}

QString write(const QString &folder, const std::vector<File> &list)
{
    for (const File &file : list) {
        const QString path = QDir(folder).filePath(file.path);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(file.bytes) != file.bytes.size() || !out.commit())
            return QStringLiteral("Couldn't write %1: %2").arg(path, out.errorString());
    }
    return {};
}

std::vector<File> collect(const QString &folder, QString *problem)
{
    std::vector<File> list;
    qint64 total = 0;
    const QDir root(folder);
    QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString relative = root.relativeFilePath(it.filePath());
        if (skipped(relative))
            continue;
        if (int(list.size()) >= maxFiles) {
            *problem = QStringLiteral("The agent wrote more than %1 files, which is too many for a page.").arg(maxFiles);
            return {};
        }
        QFile file(it.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            *problem = QStringLiteral("Couldn't read %1.").arg(relative);
            return {};
        }
        total += file.size();
        if (total > maxBytes) {
            *problem = QStringLiteral("The agent's files add up to more than %1 MB, which is too much for a page.").arg(maxBytes / (1024 * 1024));
            return {};
        }
        list.push_back({relative, file.readAll()});
    }
    std::sort(list.begin(), list.end(), [](const File &a, const File &b) { return a.path < b.path; });
    return list;
}

QString folderProblem(const QString &folder)
{
    if (folder.trimmed().isEmpty())
        return QStringLiteral("Choose a folder for the new project.");
    const QFileInfo info(folder);
    if (info.exists() && !info.isDir())
        return QStringLiteral("%1 is a file, not a folder.").arg(folder);
    if (info.isDir() && !QDir(folder).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot))
        return QStringLiteral("This folder isn't empty. Generate a page writes a new project.");
    return {};
}
}
