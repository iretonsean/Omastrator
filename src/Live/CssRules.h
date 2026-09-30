#pragma once
#include <QString>
#include <QStringList>
#include <vector>

// A small reader of CSS blocks, for the two places write-back is sure of motion (docs/MOTION.md, section 3): a custom
// property inside the rule that names one element, and a value inside one @keyframes block. It finds blocks and the
// declarations directly inside them, and understands only what it needs: comments, strings, nesting, `;` and braces.
namespace CssRules {
struct Rule {
    // What comes before the brace, with comments taken out and spaces collapsed: ".word", "@keyframes nl-rise", "40%".
    QString prelude;
    // The brace that opens the block and the one that closes it.
    qsizetype open = 0;
    qsizetype close = 0;
    // Where the prelude starts, so a whole rule can be cut out.
    qsizetype start = 0;
    // How many blocks it sits inside.
    int depth = 0;
};

// Every block in `css`, in the order they open. A block that never closes is left out.
std::vector<Rule> scan(const QString &css);

struct Declaration {
    // The value's own span, spaces and `!important` left out of it, and where the declaration begins and ends (`;` included).
    qsizetype valueStart = 0;
    qsizetype valueEnd = 0;
    qsizetype start = 0;
    qsizetype end = 0;
};
// The declarations of `property` written directly in `rule` (not in blocks nested in it).
std::vector<Declaration> declarations(const QString &css, const Rule &rule, const QString &property);

// Adds `property: value;` to the end of `rule`'s own declarations, on a line of its own when the rule has lines and inline when
// it doesn't. Returns the new text.
QString append(const QString &css, const Rule &rule, const QString &property, const QString &value);

// The rules nested one level inside `rule`, for the frames of a @keyframes block.
std::vector<Rule> children(const std::vector<Rule> &all, const Rule &rule);
}
