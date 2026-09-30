#pragma once
#include <QString>
#include <optional>
#include <vector>

// What write-back is sure of in motion (docs/MOTION.md, section 3). Each function looks at every style file it is given
// (their text, edited in place) and writes only when exactly one place matches; anything else is left for the agent.
namespace MotionWrite {
using Files = std::vector<QString *>;

enum class Result {
    // Written.
    written,
    // Nothing matched: the motion isn't in a shape these rules read.
    none,
    // More than one place matched, or the place is not the way it was when the edit was made.
    ambiguous,
};

// `property` (a custom property) in the one rule whose selector text is `selector`: its value is replaced, or the declaration
// is added to the rule when it has none. This is what makes per-element values (--i, --delay-extra) writable, since --i is
// declared once per element.
Result customProperty(const Files &files, const QString &selector, const QString &property, const QString &value);

// The value of `property` inside the keyframe `frame` ("from", "to", "40%") of the one `@keyframes name` block.
Result keyframeValue(const Files &files, const QString &name, const QString &frame, const QString &property, const QString &value);

// The `@media (prefers-reduced-motion: reduce)` rule of the marked block `block`: `removed` is its text, cut out when `added`
// is empty; `added` is put back at the end of the block when it has none. A rule that isn't the text that was removed is left.
Result reducedMotion(const Files &files, const QString &block, const QString &removed, const QString &added);

// The reduced-motion rule inside `text` from `from` to `to` (a marked block), whole, with its own line: {start, end}.
struct Span {
    qsizetype start = 0;
    qsizetype end = 0;
};
std::optional<Span> reducedRule(const QString &text, qsizetype from, qsizetype to);
}
