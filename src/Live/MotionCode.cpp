#include "Live/MotionCode.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace MotionCode {
namespace {
const QRegularExpression &startMarker()
{
    static const QRegularExpression pattern(QStringLiteral(R"(/\*\s*omastrator:motion\s+([^\s*]+)\s*\*/)"));
    return pattern;
}

const QRegularExpression &endMarker()
{
    static const QRegularExpression pattern(QStringLiteral(R"(/\*\s*omastrator:motion\s+end\s*\*/)"));
    return pattern;
}

bool skipped(const QString &name)
{
    return name == QLatin1String("node_modules") || name == QLatin1String(".git") || name == QLatin1String("dist") || name == QLatin1String("build")
        || name == QLatin1String(".next") || name == QLatin1String(".astro") || name == QLatin1String(".svelte-kit") || name == QLatin1String("out");
}

bool readable(const QString &suffix)
{
    static const QStringList suffixes{"css", "scss", "html", "astro", "vue", "svelte", "js", "ts", "jsx", "tsx", "mjs"};
    return suffixes.contains(suffix.toLower());
}

void read(const QString &folder, const QString &path, QList<Block> &out)
{
    QFile file(path);
    // A generated bundle is not where anyone writes motion.
    if (file.size() > 1'000'000 || !file.open(QIODevice::ReadOnly))
        return;
    const QString content = QString::fromUtf8(file.readAll());
    if (!content.contains(QLatin1String("omastrator:motion")))
        return;
    const QStringList lines = content.split(QLatin1Char('\n'));
    int open = -1;
    QString name;
    for (int i = 0; i < lines.size(); ++i) {
        if (open < 0) {
            const QRegularExpressionMatch match = startMarker().match(lines[i]);
            // "end" is the closing marker, never a block's name.
            if (match.hasMatch() && match.captured(1) != QLatin1String("end")) {
                open = i;
                name = match.captured(1);
            }
        } else if (endMarker().match(lines[i]).hasMatch()) {
            Block block;
            block.name = name;
            block.path = path;
            block.file = QDir(folder).relativeFilePath(path);
            block.firstLine = open + 1;
            block.lastLine = i + 1;
            block.text = lines.mid(open, i - open + 1).join(QLatin1Char('\n'));
            block.reducedMotion = block.text.contains(QLatin1String("prefers-reduced-motion"));
            out.append(block);
            open = -1;
        }
    }
}
}

QList<Block> blocks(const QString &folder, int limit)
{
    QList<Block> out;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return out;
    int seen = 0;
    QDirIterator it(folder, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    // Folders that are skipped are still entered by the iterator; their files are left by their path.
    while (it.hasNext() && seen < limit) {
        const QString path = it.next();
        const QString relative = QDir(folder).relativeFilePath(path);
        const QStringList parts = relative.split(QLatin1Char('/'));
        bool inside = false;
        for (const QString &part : parts)
            inside = inside || skipped(part);
        if (inside || it.fileInfo().isDir() || !readable(it.fileInfo().suffix()))
            continue;
        ++seen;
        read(folder, path, out);
    }
    std::sort(out.begin(), out.end(), [](const Block &a, const Block &b) { return a.file == b.file ? a.firstLine < b.firstLine : a.file < b.file; });
    return out;
}

QList<QPair<QString, QString>> tokens(const Block &block)
{
    static const QRegularExpression declaration(QStringLiteral(R"((--(?:duration|ease|stagger)-[\w-]+)\s*:\s*([^;}]+))"));
    QList<QPair<QString, QString>> out;
    for (const QRegularExpressionMatch &match : declaration.globalMatch(block.text))
        out.append({match.captured(1), match.captured(2).trimmed()});
    return out;
}

QList<Block> relevant(const QList<Block> &all, const QStringList &names, const QStringList &selectors)
{
    QStringList wanted;
    for (const QString &name : names)
        if (!name.isEmpty())
            wanted << name;
    // A selector such as "#headline > span:nth-of-type(2)" is found by its ids and classes.
    static const QRegularExpression simple(QStringLiteral(R"([#.][\w-]+)"));
    for (const QString &selector : selectors)
        for (const QRegularExpressionMatch &match : simple.globalMatch(selector))
            wanted << match.captured(0);
    if (wanted.isEmpty())
        return all;
    QList<Block> out;
    for (const Block &block : all)
        for (const QString &word : std::as_const(wanted))
            if (block.text.contains(word)) {
                out.append(block);
                break;
            }
    return out;
}

}
