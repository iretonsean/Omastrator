#include "Document/Hyphenator.h"
#include "Document/HyphenPatterns.h"
#include <QChar>
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <algorithm>

namespace {
// digits between letters are the hyphenation value at that point; ".hy3phen" reads as
// values [0,0,3,0,0,0,0] over letters ".hyphen".
QPair<QString, QVector<signed char>> parsePattern(const QString &raw)
{
    QString letters;
    QVector<signed char> values{0};
    for (const QChar &ch : raw) {
        if (ch.isDigit()) {
            values.back() = static_cast<signed char>(ch.digitValue());
        } else {
            letters += ch;
            values.push_back(0);
        }
    }
    return {letters, values};
}

const QHash<QString, QVector<signed char>> &patternTable()
{
    static const QHash<QString, QVector<signed char>> table = [] {
        QHash<QString, QVector<signed char>> made;
        for (const QString &line : QString::fromLatin1(OmastratorHyphenation::patterns).split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            const auto [letters, values] = parsePattern(trimmed);
            made[letters] = values;
        }
        return made;
    }();
    return table;
}

// "as-so-ciate" -> "assoc iate", break positions {2, 4}. Keyed by the word without hyphens.
const QHash<QString, std::vector<int>> &exceptionTable()
{
    static const QHash<QString, std::vector<int>> table = [] {
        QHash<QString, std::vector<int>> made;
        for (const QString &line : QString::fromLatin1(OmastratorHyphenation::exceptions).split(QLatin1Char('\n'))) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            QString word;
            std::vector<int> breaks;
            for (const QChar &ch : trimmed) {
                if (ch == QLatin1Char('-'))
                    breaks.push_back(int(word.size()));
                else
                    word += ch;
            }
            made[word] = breaks;
        }
        return made;
    }();
    return table;
}

int longestPattern()
{
    static const int longest = [] {
        int most = 1;
        const auto &table = patternTable();
        for (auto key = table.keyBegin(); key != table.keyEnd(); ++key)
            most = std::max(most, int(key->size()));
        return most;
    }();
    return longest;
}
}

std::vector<int> Hyphenator::breakPoints(const QString &word, int minBefore, int minAfter)
{
    minBefore = std::max(1, minBefore);
    minAfter = std::max(1, minAfter);
    if (word.size() < minBefore + minAfter)
        return {};
    for (const QChar &ch : word) {
        if (!ch.isLetter())
            return {};
    }
    const QString lower = word.toLower();
    std::vector<int> points;
    const auto exception = exceptionTable().find(lower);
    if (exception != exceptionTable().end()) {
        points = exception.value();
    } else {
        // Liang: pad with word-boundary dots, score every gap by the best-matching
        // pattern that covers it, and break where the score comes out odd.
        const QString padded = QLatin1Char('.') + lower + QLatin1Char('.');
        std::vector<int> scores(size_t(padded.size()) + 1, 0);
        const int longest = longestPattern();
        for (int start = 0; start < padded.size(); ++start) {
            for (int length = 1; length <= longest && start + length <= padded.size(); ++length) {
                const auto found = patternTable().find(padded.mid(start, length));
                if (found == patternTable().end())
                    continue;
                for (int at = 0; at <= length; ++at)
                    scores[size_t(start + at)] = std::max(scores[size_t(start + at)], int(found.value()[at]));
            }
        }
        // Position p in the word sits at padded index p + 1 (the leading dot).
        for (int p = 1; p < word.size(); ++p) {
            if (scores[size_t(p) + 1] % 2 != 0)
                points.push_back(p);
        }
    }
    std::vector<int> filtered;
    for (int p : points) {
        if (p >= minBefore && p <= word.size() - minAfter)
            filtered.push_back(p);
    }
    return filtered;
}
