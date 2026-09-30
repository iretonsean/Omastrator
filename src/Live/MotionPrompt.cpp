#include "Live/MotionPrompt.h"
#include <QJsonDocument>
#include <QRegularExpression>

namespace MotionPrompt {
QString startMarker(const QString &name)
{
    return QStringLiteral("/* omastrator:motion %1 */").arg(name);
}

QString endMarker()
{
    return QStringLiteral("/* omastrator:motion end */");
}

QString keyframePrefix(const QString &siteName)
{
    QString words = siteName.toLower();
    words.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral(" "));
    const QStringList parts = words.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        QString letters;
        for (const QString &part : parts)
            letters += part.at(0);
        return letters.left(3);
    }
    // One word: its first two letters.
    const QString word = parts.value(0);
    return word.isEmpty() ? QStringLiteral("oma") : word.left(2);
}

QString animate(const AgentWork &work, const Brief &brief)
{
    const MotionStack::Info &stack = brief.stack;
    QString text = QStringLiteral(
                       "This is an Omastrator motion task (request %1). You are writing animation for a web project, as real CSS in its source.\n\n"
                       "Work only in this folder, a git worktree on its own branch (%2): %3\n"
                       "Don't commit, push, deploy or start a dev server. Omastrator serves this folder as a preview, and the designer decides "
                       "whether to keep it; nothing reaches the project until they save.\n\n"
                       "The page: %4\n")
                       .arg(work.requestId, work.branch, work.worktree, brief.url);
    if (!brief.width.isEmpty())
        text += brief.width + QLatin1Char('\n');
    text += QStringLiteral("\nThe designer asked, %1: %2\n")
                .arg(brief.together ? QStringLiteral("about these elements, to move as one group") : QStringLiteral("about the selected element"),
                     brief.instruction.isEmpty() ? QStringLiteral("(nothing more than \"animate it\")") : brief.instruction);
    text += QStringLiteral("\nThe project: %1.%2%3\n")
                .arg(stack.stack,
                     stack.styleFile.isEmpty() ? QString() : QStringLiteral(" Its styles are in %1.").arg(stack.styleFile),
                     stack.tokenFile.isEmpty() || stack.tokenFile == stack.styleFile ? QString()
                                                                                     : QStringLiteral(" Its tokens are in %1.").arg(stack.tokenFile));
    if (!stack.libraries.isEmpty())
        text += QStringLiteral("It has %1 in package.json; don't add another library, and leave what already uses it alone.\n").arg(stack.libraries.join(QStringLiteral(", ")));

    text += QStringLiteral("\nThe selected elements, as the browser sees them (selector, classes, computed styles, markup):\n");
    text += QString::fromUtf8(QJsonDocument(brief.elements).toJson(QJsonDocument::Indented)).left(30'000);
    if (!brief.tokens.isEmpty())
        text += QStringLiteral("\nThe page's tokens (use them; add a motion token only where none fits):\n") + QString::fromUtf8(QJsonDocument(brief.tokens).toJson(QJsonDocument::Compact)).left(12'000) + QLatin1Char('\n');
    if (!brief.motion.isEmpty())
        text += QStringLiteral("\nMotion already on these elements (change it in place rather than adding a second animation on top):\n")
                + QString::fromUtf8(QJsonDocument(brief.motion).toJson(QJsonDocument::Compact)).left(12'000) + QLatin1Char('\n');
    if (!brief.keyframeNames.isEmpty())
        text += QStringLiteral("\n@keyframes names in use on the page (don't reuse one for something else): %1\n").arg(brief.keyframeNames.join(QStringLiteral(", ")));
    if (!brief.screenshot.isEmpty())
        text += QStringLiteral("\nA screenshot of the page: %1 (look at it).\n").arg(brief.screenshot);

    const QString name = brief.prefix.isEmpty() ? QStringLiteral("oma") : brief.prefix;
    text += QStringLiteral(
                "\nThe output contract. Omastrator reads what you write back, gives the designer controls over it, and checks it when you are done:\n"
                "- Plain CSS in the project's own style file%1. No new dependency. No JavaScript, unless the motion starts on a click: then the smallest "
                "listener that toggles one class.\n"
                "- Every value the designer may tune is a custom property, declared in %2: durations as --duration-<name>, easings as --ease-<name>, "
                "staggers as --stagger-<name>. Use them in the rules with var().\n"
                "- Any new @keyframes name starts with \"%3-\" (for example %3-rise) and is used by one rule for this selection.\n"
                "- Timing that differs per element goes through --i (the element's index, 0, 1, 2…) and, when the designer has set one, --delay-extra: "
                "animation-delay: calc(var(--i) * var(--stagger-<name>) + var(--delay-extra, 0ms)). %4\n"
                "- Change the markup only where the motion needs it (for example splitting a headline into <span class=\"word\" style=\"--i: 0\">), "
                "and nothing else in it.\n"
                "- Put everything for this motion between two comments, each on its own line:\n%5\n  … the tokens, keyframes and rules …\n%6\n")
                .arg(stack.styleFile.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(stack.styleFile),
                     stack.tailwindV4 ? QStringLiteral("the @theme block") : QStringLiteral(":root"), name,
                     brief.together ? QStringLiteral("The group is one rule for its elements' shared class; each element carries only its index, in a rule of its own "
                                                     "(#guji { --i: 1; }). Give an element with no stable selector an id or a class.")
                                    : QStringLiteral("For one element, use --i only when it is split into parts."),
                     startMarker(QStringLiteral("<name>")), endMarker());
    if (brief.reducedMotion)
        text += QStringLiteral("- Include, inside the block, a rule for reduced motion: @media (prefers-reduced-motion: reduce) { … }. For an entrance, animation: none "
                               "(the keyframes use `from` only and the animation fills `both`, so the element shows in its end state); for a loop, stop it; for hover, "
                               "keep the colour and opacity change and drop the movement.\n");
    else
        text += QStringLiteral("- The designer turned the reduced-motion rule off for this run: don't write one.\n");
    text += QStringLiteral(
                "\nWhen you're done, run:\n  %1 agent live '{\"action\": \"agentDone\", \"requestId\": \"%2\", \"summary\": \"<one line on the motion you wrote>\"}'\n"
                "If you can't do it, run the same with a summary that says why, and change nothing.\n")
                .arg(brief.command, work.requestId);
    return text;
}
}
