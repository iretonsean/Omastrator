#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// The motion the agent wrote into a project, found by its markers (docs/MOTION.md, section 3):
//   /* omastrator:motion headline-reveal */ … /* omastrator:motion end */
// The Code tab shows these blocks. Reading only: nothing here writes a file.
namespace MotionCode {

struct Block {
    QString name;
    // The file, as a path inside the project, and its full path.
    QString file;
    QString path;
    // 1-based, the marker lines included.
    int firstLine = 0;
    int lastLine = 0;
    // The block's lines, the markers included.
    QString text;
    // The block holds a `@media (prefers-reduced-motion: reduce)` rule.
    bool reducedMotion = false;
};

// The blocks in the project's style and markup files, skipping node_modules, .git and build output; at most `limit` files
// are read, so a big project is never walked in full.
QList<Block> blocks(const QString &folder, int limit = 600);

// A block's motion tokens, "--duration-reveal: 480ms" as {"--duration-reveal", "480ms"}, for the inspector.
QList<QPair<QString, QString>> tokens(const Block &block);

// The blocks that mention any of `names` (a keyframes name) or `selectors`; all of them when nothing is named.
QList<Block> relevant(const QList<Block> &all, const QStringList &names, const QStringList &selectors);

}
