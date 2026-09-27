#pragma once
#include <QString>
#include <vector>

// Automatic hyphenation: Liang's algorithm over patterns vendored from hyph-utf8
// (third_party/hyph-utf8, hyph-en-us). See its LICENSE; provenance is in AGENTS.md.
namespace Hyphenator {
// Positions within `word` (1..word.length()-1, in QChar units) where a soft hyphen may
// go, at least `minBefore` letters before it and `minAfter` after. A curated exception
// (as in "as-so-ciate") wins over the computed patterns. Anything but plain letters
// (digits, punctuation, marks) leaves the word alone, so callers should split on those first.
std::vector<int> breakPoints(const QString &word, int minBefore, int minAfter);
}
