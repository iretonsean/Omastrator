#include "Live/MotionStack.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace MotionStack {
namespace {
QByteArray readFile(const QString &path, qint64 limit = 400'000)
{
    QFile file(path);
    if (file.size() > limit || !file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool skipped(const QString &relative)
{
    static const QRegularExpression folders(QStringLiteral("(^|/)(node_modules|dist|build|out|\\.[^/]+)/"));
    return folders.match(relative).hasMatch();
}

// The major version in a range such as "^4.0.0", "~3.4" or "latest" (0 when it says none).
int majorOf(const QString &range)
{
    static const QRegularExpression version(QStringLiteral("(\\d+)"));
    const auto match = version.match(range);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}
}

Info detect(const QString &folder)
{
    Info info;
    const QDir dir(folder);
    QJsonObject package = QJsonDocument::fromJson(readFile(dir.filePath(QStringLiteral("package.json")))).object();
    // `value()`, not `[]`: on a non-const object `[]` adds the key it is asked for.
    const bool hasPackage = !package.isEmpty();
    QJsonObject dependencies = package.value(QStringLiteral("dependencies")).toObject();
    const QJsonObject dev = package.value(QStringLiteral("devDependencies")).toObject();
    for (auto it = dev.begin(); it != dev.end(); ++it)
        dependencies.insert(it.key(), it.value());

    for (const char *name : {"gsap", "motion", "framer-motion", "animejs", "anime.js", "@motionone/dom"})
        if (dependencies.contains(QLatin1String(name)))
            info.libraries << QLatin1String(name);

    // Every CSS file outside the folders that aren't the project's own, newest layout first (src/ before the root).
    QStringList css;
    QDirIterator walk(folder, {QStringLiteral("*.css")}, QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString relative = dir.relativeFilePath(walk.next());
        if (!skipped(relative))
            css << relative;
    }
    std::sort(css.begin(), css.end(), [](const QString &a, const QString &b) {
        const bool inSrcA = a.startsWith(QLatin1String("src/")), inSrcB = b.startsWith(QLatin1String("src/"));
        return inSrcA != inSrcB ? inSrcA : a < b;
    });

    QString theme;
    QString root;
    static const QRegularExpression tailwindImport(QStringLiteral(R"(@import\s+["']tailwindcss)"));
    for (const QString &relative : std::as_const(css)) {
        const QString text = QString::fromUtf8(readFile(dir.filePath(relative)));
        if (theme.isEmpty() && (tailwindImport.match(text).hasMatch() || text.contains(QLatin1String("@theme"))))
            theme = relative;
        if (root.isEmpty() && text.contains(QRegularExpression(QStringLiteral(R"(:root\s*\{[^}]*--)"))))
            root = relative;
    }

    const int tailwind = majorOf(dependencies.value(QStringLiteral("tailwindcss")).toString());
    info.tailwindV4 = !theme.isEmpty() && (tailwind >= 4 || tailwind == 0);
    QStringList parts;
    if (dependencies.contains(QStringLiteral("astro")))
        parts << QStringLiteral("Astro");
    else if (dependencies.contains(QStringLiteral("vite")))
        parts << QStringLiteral("Vite");
    else if (hasPackage)
        parts << QStringLiteral("A JavaScript project");
    if (info.tailwindV4)
        parts << QStringLiteral("Tailwind v4");
    else if (tailwind >= 1)
        parts << QStringLiteral("Tailwind v%1").arg(tailwind);
    info.stack = parts.isEmpty() ? QStringLiteral("Plain HTML and CSS") : parts.join(QStringLiteral(" + "));

    // The style file: the theme's for Tailwind v4, else the sheet the page links, else a usual name, else the first.
    if (info.tailwindV4) {
        info.styleFile = theme;
    } else {
        static const QRegularExpression link(QStringLiteral(R"(<link[^>]*rel=["']stylesheet["'][^>]*href=["']([^"']+\.css)["'])"), QRegularExpression::CaseInsensitiveOption);
        for (const char *page : {"index.html", "src/index.html"}) {
            const auto match = link.match(QString::fromUtf8(readFile(dir.filePath(QLatin1String(page)))));
            if (match.hasMatch() && QFileInfo::exists(QDir(QFileInfo(dir.filePath(QLatin1String(page))).absolutePath()).filePath(match.captured(1)))) {
                info.styleFile = dir.relativeFilePath(QDir(QFileInfo(dir.filePath(QLatin1String(page))).absolutePath()).filePath(match.captured(1)));
                break;
            }
        }
        if (info.styleFile.isEmpty()) {
            for (const char *usual : {"src/style.css", "src/styles/global.css", "src/index.css", "style.css", "styles.css"})
                if (css.contains(QLatin1String(usual))) {
                    info.styleFile = QLatin1String(usual);
                    break;
                }
        }
        if (info.styleFile.isEmpty() && !css.isEmpty())
            info.styleFile = css.first();
    }
    info.tokenFile = info.tailwindV4 ? theme : !root.isEmpty() ? root : info.styleFile;
    return info;
}
}
