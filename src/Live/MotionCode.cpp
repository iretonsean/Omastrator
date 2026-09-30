#include "Live/MotionCode.h"
#include "Live/CssRules.h"
#include "Live/MotionWrite.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

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

namespace {
// The readable files under `folder`, without ever entering a skipped folder (node_modules alone can hold a hundred thousand).
void walk(const QDir &dir, int limit, QList<QFileInfo> &files)
{
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (files.size() >= limit)
            return;
        if (info.isDir()) {
            if (!skipped(info.fileName()) && !info.isSymLink())
                walk(QDir(info.absoluteFilePath()), limit, files);
        } else if (readable(info.suffix())) {
            files.append(info);
        }
    }
}

struct Cached {
    QString signature;
    QList<Block> blocks;
};
}

QList<Block> blocks(const QString &folder, int limit)
{
    QList<Block> out;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return out;
    QList<QFileInfo> files;
    walk(QDir(folder), limit, files);
    // The Code tab asks on every motion list and every row picked; the files are read again only when one has changed.
    static QHash<QString, Cached> cache;
    QString signature;
    for (const QFileInfo &info : std::as_const(files))
        signature += QStringLiteral("%1|%2|%3\n").arg(info.absoluteFilePath()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    const QString key = QStringLiteral("%1|%2").arg(folder).arg(limit);
    if (const auto found = cache.constFind(key); found != cache.constEnd() && found->signature == signature)
        return found->blocks;
    for (const QFileInfo &info : std::as_const(files))
        read(folder, info.absoluteFilePath(), out);
    std::sort(out.begin(), out.end(), [](const Block &a, const Block &b) { return a.file == b.file ? a.firstLine < b.firstLine : a.file < b.file; });
    cache.insert(key, {signature, out});
    return out;
}

bool mentions(const Block &block, const QString &name)
{
    if (name.isEmpty())
        return false;
    const QRegularExpression named(QStringLiteral(R"((?<![\w-])%1(?![\w-]))").arg(QRegularExpression::escape(name)));
    return block.text.contains(named);
}

QList<QPair<QString, QString>> tokens(const Block &block)
{
    static const QRegularExpression declaration(QStringLiteral(R"((--(?:duration|ease|stagger)-[\w-]+)\s*:\s*([^;}]+))"));
    QList<QPair<QString, QString>> out;
    for (const QRegularExpressionMatch &match : declaration.globalMatch(block.text))
        out.append({match.captured(1), match.captured(2).trimmed()});
    return out;
}

Bindings bindings(const Block &block, const QString &animation)
{
    Bindings found;
    if (animation.isEmpty())
        return found;
    static const QRegularExpression duration(QStringLiteral(R"(var\(\s*(--duration-[\w-]+))"));
    static const QRegularExpression easing(QStringLiteral(R"(var\(\s*(--ease-[\w-]+))"));
    static const QRegularExpression stagger(QStringLiteral(R"(var\(\s*(--stagger-[\w-]+))"));
    static const QRegularExpression index(QStringLiteral(R"(var\(\s*--i\b)"));
    static const QRegularExpression extra(QStringLiteral(R"(var\(\s*--delay-extra\b)"));
    const std::vector<CssRules::Rule> rules = CssRules::scan(block.text);
    for (const CssRules::Rule &rule : rules) {
        if (rule.prelude.startsWith(QLatin1Char('@')))
            continue;
        // The rule that runs the animation names it in `animation` or `animation-name`.
        bool runs = false;
        for (const char *property : {"animation", "animation-name"}) {
            for (const CssRules::Declaration &each : CssRules::declarations(block.text, rule, QLatin1String(property)))
                runs = runs || QRegularExpression(QStringLiteral(R"((^|[\s,])%1($|[\s,;]))").arg(QRegularExpression::escape(animation)))
                                   .match(block.text.mid(each.valueStart, each.valueEnd - each.valueStart)).hasMatch();
        }
        if (!runs)
            continue;
        for (const char *property : {"animation", "animation-duration", "animation-timing-function", "animation-delay"}) {
            for (const CssRules::Declaration &each : CssRules::declarations(block.text, rule, QLatin1String(property))) {
                const QString value = block.text.mid(each.valueStart, each.valueEnd - each.valueStart);
                if (found.duration.isEmpty() && duration.match(value).hasMatch())
                    found.duration = duration.match(value).captured(1);
                if (found.easing.isEmpty() && easing.match(value).hasMatch())
                    found.easing = easing.match(value).captured(1);
                if (found.stagger.isEmpty() && stagger.match(value).hasMatch())
                    found.stagger = stagger.match(value).captured(1);
                if (QLatin1String(property) != QLatin1String("animation-duration") && index.match(value).hasMatch())
                    found.indexed = true;
                if (extra.match(value).hasMatch())
                    found.extraDelay = true;
            }
        }
        return found;
    }
    return found;
}

QString reducedRule(const Block &block)
{
    // The block's own text, the markers in it: the rule is found between them.
    const auto span = MotionWrite::reducedRule(block.text, 0, block.text.size());
    return span ? block.text.mid(span->start, span->end - span->start).trimmed() : QString();
}

QString defaultReducedRule(const Block &block)
{
    QStringList selectors;
    for (const CssRules::Rule &rule : CssRules::scan(block.text)) {
        if (rule.prelude.startsWith(QLatin1Char('@')) || rule.depth != 0)
            continue;
        if (!CssRules::declarations(block.text, rule, QStringLiteral("animation")).empty() && !selectors.contains(rule.prelude))
            selectors << rule.prelude;
    }
    if (selectors.isEmpty())
        return {};
    return QStringLiteral("@media (prefers-reduced-motion: reduce) { %1 { animation: none; } }").arg(selectors.join(QStringLiteral(", ")));
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
