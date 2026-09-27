#include "UI/CommandPalette.h"
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>

namespace {
const QString recentKey = QStringLiteral("commandPalette/recent");

QString folded(const QString &text)
{
    QString result = text.toLower();
    result.remove(QLatin1Char('&'));
    result.remove(QChar(0x2026));
    return result.simplified();
}

QStringList wordsOf(const QString &text)
{
    static const QRegularExpression split(QStringLiteral("[^\\p{L}\\p{N}']+"));
    return text.split(split, Qt::SkipEmptyParts);
}

// Each query word starts some word of the text.
bool everyWordStarts(const QStringList &query, const QStringList &words)
{
    return std::all_of(query.begin(), query.end(), [&](const QString &part) {
        return std::any_of(words.begin(), words.end(), [&](const QString &word) { return word.startsWith(part); });
    });
}
}

std::optional<int> CommandPalette::score(const QString &query, const QString &text)
{
    const QString q = folded(query), t = folded(text);
    if (q.isEmpty())
        return 0;
    if (t == q)
        return 1000;
    if (t.startsWith(q))
        return 900 - std::min(int(t.size() - q.size()), 50);
    const QStringList queryWords = wordsOf(q), words = wordsOf(t);
    if (!queryWords.isEmpty() && everyWordStarts(queryWords, words))
        return 700 + (words.value(0).startsWith(queryWords.front()) ? 50 : 0) - std::min(int(words.size()), 10);
    if (const qsizetype at = t.indexOf(q); at >= 0)
        return 600 - std::min(int(at), 50);
    QString compact = q;
    compact.remove(QLatin1Char(' '));
    // "co" is Create Outlines: the words' first letters.
    QString initials;
    for (const QString &word : words)
        initials += word.at(0);
    if (compact.size() >= 2 && initials.startsWith(compact))
        return 550;
    // The letters in order, better at word starts and in runs.
    int result = 100, at = 0, last = -2;
    for (const QChar c : compact) {
        const qsizetype found = t.indexOf(c, at);
        if (found < 0)
            return std::nullopt;
        const bool wordStart = found == 0 || !t.at(found - 1).isLetterOrNumber();
        result += (wordStart ? 10 : 0) + (found == last + 1 ? 6 : 0) - std::min(int(found - at), 6);
        last = int(found);
        at = int(found) + 1;
    }
    // Letters strewn across unrelated words aren't a match.
    return result < 105 ? std::nullopt : std::optional(std::min(result, 499));
}

bool CommandPalette::asksForNewArt(const QString &text)
{
    QStringList words = wordsOf(text.toLower());
    // Politeness says nothing about what's wanted.
    static const QStringList polite{"please", "can", "could", "would", "will", "you", "i", "i'd", "we", "let's", "lets", "like", "to", "me", "us"};
    while (!words.isEmpty() && polite.contains(words.front()))
        words.removeFirst();
    if (words.isEmpty())
        return false;
    static const QStringList drawing{"draw", "generate", "sketch", "design", "create", "illustrate", "paint", "render", "imagine", "invent"};
    static const QStringList wanting{"make", "give", "need", "want", "get"};
    static const QStringList articles{"a", "an", "some", "new", "another", "three", "two", "few", "logo", "icon"};
    const QString first = words.front();
    if (drawing.contains(first))
        return true;
    if (wanting.contains(first))
        return words.size() > 1 && articles.contains(words.at(1));
    // A bare noun phrase: "a fox logo".
    return first == QLatin1String("a") || first == QLatin1String("an");
}

QStringList CommandPalette::recent()
{
    return QSettings().value(recentKey).toStringList();
}

void CommandPalette::remember(const QString &id)
{
    QStringList ids = recent();
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > recentLimit)
        ids.removeLast();
    QSettings().setValue(recentKey, ids);
}

std::vector<CommandPalette::Command> CommandPalette::results(const QString &query) const
{
    const QStringList lately = recent();
    const auto recency = [&](const Command &command) {
        const qsizetype index = lately.indexOf(command.id);
        return index < 0 ? 0 : 120 - int(index) * 10;
    };
    const QString trimmed = query.trimmed();
    std::vector<Command> shown;
    if (trimmed.isEmpty()) {
        // Nothing typed: the recent ones, then everything in menu order.
        for (const QString &id : lately) {
            auto found = std::find_if(m_commands.begin(), m_commands.end(), [&](const Command &command) { return command.id == id; });
            if (found != m_commands.end())
                shown.push_back(*found);
        }
        for (const Command &command : m_commands) {
            if (!lately.contains(command.id))
                shown.push_back(command);
        }
        return shown;
    }
    const bool forced = trimmed.startsWith(QLatin1Char('?'));
    const QString request = forced ? trimmed.mid(1).trimmed() : trimmed;
    std::vector<std::pair<int, const Command *>> ranked;
    int best = 0;
    if (!forced) {
        const QStringList queryWords = wordsOf(folded(trimmed));
        for (const Command &command : m_commands) {
            std::optional<int> found = score(trimmed, command.title);
            // Its menu and keywords find it too, below a match on its name; every word found still makes it a command.
            if (!found && everyWordStarts(queryWords, wordsOf(folded(command.title + QLatin1Char(' ') + command.where + QLatin1Char(' ') + command.keywords)))) {
                found = 450;
                best = std::max(best, 650);
            }
            if (!found)
                continue;
            best = std::max(best, *found);
            ranked.push_back({*found + recency(command) - (command.enabled ? 0 : 300), &command});
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first)
                return a.first > b.first;
            return a.second->title.size() < b.second->title.size();
        });
    }
    for (const auto &[rank, command] : ranked)
        shown.push_back(*command);
    if (shown.size() > 60)
        shown.resize(60);
    if (const std::optional<Command> asking = ask(request)) {
        // A sentence, or a few words no command answers to: the agent's row leads.
        const int words = int(wordsOf(request).size());
        const bool sentence = (words >= 4 && best < 700) || (words >= 2 && best < commandScore);
        if (forced || shown.empty() || sentence)
            shown.insert(shown.begin(), *asking);
        else
            shown.push_back(*asking);
    }
    return shown;
}
