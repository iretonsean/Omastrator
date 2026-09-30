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

// Whether the block is about the animation `name`, as a whole word: "rise" is not "sunrise" or "nl-rise-2".
bool mentions(const Block &block, const QString &name);

// A block's motion tokens, "--duration-reveal: 480ms" as {"--duration-reveal", "480ms"}, for the inspector.
QList<QPair<QString, QString>> tokens(const Block &block);

// The custom properties a row's motion takes its values from: what the rule that runs the animation `animation` names with
// var(): --duration-x, --ease-y and --stagger-z. Empty where a value is written out and is not a token.
struct Bindings {
    QString duration;
    QString easing;
    QString stagger;
    // The rule's `animation-delay` reads each element's `--i`: a group whose order is the indices, which Order can rewrite.
    bool indexed = false;
    // The rule's delay reads `--delay-extra`, so one element's extra delay changes the page (and Save has somewhere to put it).
    bool extraDelay = false;
};
Bindings bindings(const Block &block, const QString &animation);

// The block's `@media (prefers-reduced-motion: reduce)` rule, whole and on its own lines; empty when it has none.
QString reducedRule(const Block &block);

// The rule a block would have to switch its animations off for reduced motion, when it has none:
// `@media (prefers-reduced-motion: reduce) { .word, .lede { animation: none; } }`. Empty when nothing in it runs an animation.
QString defaultReducedRule(const Block &block);

// The blocks that mention any of `names` (a keyframes name) or `selectors`; all of them when nothing is named.
QList<Block> relevant(const QList<Block> &all, const QStringList &names, const QStringList &selectors);

}
