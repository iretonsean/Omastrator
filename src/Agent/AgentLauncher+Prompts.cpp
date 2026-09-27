#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include <QJsonArray>
#include <algorithm>

namespace {
// Past this a picked SVG is summarised; the agent can read the document instead.
constexpr qsizetype maximumInlineSvg = 48 * 1024;

QString header(const QString &task, const QString &requestId)
{
    return QStringLiteral("Omastrator task: %1 (request %2).\n"
                          "You are driving the Omastrator vector editor that is open on the user's desktop. First read AGENTS.md "
                          "in the working directory: it explains how to call Omastrator with \"$OMASTRATOR_BIN\" agent <method> "
                          "'<json>' (or the omastrator MCP tools) and lists every method.\n\n")
        .arg(task, requestId);
}

QString size(const QRectF &box)
{
    return QStringLiteral("%1 × %2 pt").arg(box.width()).arg(box.height());
}
}

namespace AgentLauncher {
QString instructions(const QString &binary)
{
    QString text = QStringLiteral(
        "# Driving Omastrator\n\n"
        "Omastrator is a vector illustration app, like Illustrator, and it is running on this desktop. You change the "
        "user's open document through its agent bridge. Everything you make stays editable: real paths, groups and "
        "styles, never a flattened picture.\n\n"
        "## How to call it\n\n"
        "From a shell, with one JSON object as the params:\n\n"
        "```sh\n"
        "\"$OMASTRATOR_BIN\" agent document_get\n"
        "\"$OMASTRATOR_BIN\" agent set_style '{\"fill\": \"#1e66f5\"}'\n"
        "\"$OMASTRATOR_BIN\" agent insert_svg - <<'JSON'\n"
        "{\"svg\": \"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='40' fill='#e64553'/></svg>\"}\n"
        "JSON\n"
        "\"$OMASTRATOR_BIN\" agent <method> --help   # one method's parameters\n"
        "```\n\n"
        "`$OMASTRATOR_BIN` is `%1`. Pass `-` to read the params from stdin, which suits long SVG. The result prints as "
        "JSON; on an error the message goes to stderr and the exit code is 1. If the `omastrator` MCP server is "
        "connected (see `.mcp.json`), its tools have the same names and parameters.\n\n"
        "## Rules\n\n"
        "- **Preview, then accept.** Every edit goes into one proposal the user sees live on the canvas. Your further "
        "edits add to it. When you are done, call `proposal_finish` with a short `title` and a one or two sentence "
        "`summary`. The user presses Enter to keep it (one undo step, \"AI: <title>\") or Esc to discard it. You cannot "
        "accept it yourself, so don't wait for it.\n"
        "- **Look before and after.** Call `render` and open the PNG it returns to see the artboard; use "
        "`selectionOnly` to see the selection up close.\n"
        "- **Coordinates** are points, with the origin at the artboard's top left and y going down. `document_get` "
        "gives the artboard's `width` and `height`. Objects are listed bottom to top; children follow their parent.\n"
        "- **Scope.** When something is selected, work on the selection; methods that take `ids` default to it.\n"
        "- **Files.** Never call `save` or `export` unless the user asked for it. Nothing else writes files.\n"
        "- **SVG.** Write complete SVG documents with a `viewBox`. Paths, basic shapes, groups, solid fills, strokes, "
        "opacity and linear or radial gradients import as editable paths. Filters, masks, images and CSS are dropped, "
        "and `<text>` does not import: draw lettering as paths.\n"
        "- **Results for the panels.** `show_variations` and `show_roast` fill panels in the app; they are not edits. "
        "Always pass the `requestId` your task gave you.\n\n"
        "## Roast My Design\n\n"
        "When asked to roast, it goes in this order:\n\n"
        "1. **The roast.** Full comedy-roast energy, deadpan and specific, aimed only at the design, never at the "
        "person: layout, type, colour, alignment, the fourth drop shadow. Design-world references (clients, kerning, "
        "Comic Sans, \"make the logo bigger\", feedback rounds) land best. No exclamation marks, no emoji.\n"
        "2. **Then sincere feedback,** clearly separate: specific, actionable fixes in order of impact, each pointing "
        "at the actual objects by id.\n"
        "3. **Then `suggestedPrompt`:** one brief for a new round of variations that applies that feedback.\n\n"
        "## Methods\n")
                       .arg(binary);
    QString group;
    for (const AgentProtocol::Method &method : AgentProtocol::methods()) {
        if (method.group != group) {
            group = method.group;
            text += QStringLiteral("\n### %1\n\n").arg(group.left(1).toUpper() + group.mid(1));
        }
        QStringList parameters;
        const QJsonObject properties = method.inputSchema["properties"].toObject();
        const QJsonArray required = method.inputSchema["required"].toArray();
        for (auto it = properties.begin(); it != properties.end(); ++it)
            parameters << (required.contains(it.key()) ? it.key() : it.key() + QLatin1Char('?'));
        text += QStringLiteral("- `%1 {%2}`: %3\n").arg(method.name, parameters.join(QStringLiteral(", ")), method.description);
    }
    return text;
}

QString generatePrompt(const QString &requestId, const QString &brief, int count, std::optional<QRectF> fitTo,
                       const std::vector<Round> &history)
{
    count = std::clamp(count, 1, 6);
    QString text = header(QStringLiteral("Generate"), requestId);
    if (!history.empty()) {
        text += QStringLiteral("This refines earlier rounds. What the user asked for, in order:\n");
        for (size_t index = 0; index < history.size(); ++index)
            text += QStringLiteral("%1. %2\n").arg(index + 1).arg(history[index].instruction);
        const QString &chosen = history.back().chosenSvg;
        if (!chosen.isEmpty() && chosen.size() <= maximumInlineSvg)
            text += QStringLiteral("\nThe variation they picked last round:\n\n```svg\n%1\n```\n\n").arg(chosen);
        text += QStringLiteral("Now apply this to the picked variation, keeping what worked:\n%1\n\n").arg(brief);
    } else {
        text += QStringLiteral("The brief:\n%1\n\n").arg(brief);
    }
    text += QStringLiteral("Make %1 distinct %2 as standalone SVG documents. ").arg(count).arg(count == 1 ? QStringLiteral("design") : QStringLiteral("variations"));
    if (fitTo)
        text += QStringLiteral("Each will be fitted into a box of %1, so give each a viewBox with that aspect ratio. ").arg(size(*fitTo));
    else
        text += QStringLiteral("Call document_get first for the artboard's size and the style already there. ");
    text += QStringLiteral(
                "Make the variations genuinely different directions, not one idea recoloured. Deliver them all in one call:\n\n"
                "show_variations {\"requestId\": \"%1\", \"variations\": [{\"name\": \"…\", \"svg\": \"<svg …>…</svg>\", "
                "\"note\": \"one line on the idea\"}]}\n\n"
                "Do not insert them into the document: the user picks one in the Variations panel, and the app inserts it. "
                "Stop after show_variations succeeds.")
                .arg(requestId);
    return text;
}

QString editPrompt(const QString &requestId, const QString &instruction, bool hasSelection)
{
    QString text = header(QStringLiteral("Edit with Instruction"), requestId);
    text += QStringLiteral("The instruction:\n%1\n\n").arg(instruction);
    text += hasSelection ? QStringLiteral("It applies to the current selection only: call selection_get for its objects, and render "
                                          "{\"selectionOnly\": true} to see it.\n\n")
                         : QStringLiteral("Nothing is selected, so it applies to the whole document: call document_get, and render to see it.\n\n");
    text += QStringLiteral(
        "Carry it out with the edit methods (set_style, transform, align, distribute, arrange, group, ungroup, pathfinder, "
        "update_object, replace_objects, insert_svg, delete). Render again to check the result. Then call proposal_finish "
        "{\"title\": \"…\", \"summary\": \"…\"} with a short title such as \"Recolor\" and what you changed. Do not save.");
    return text;
}

QString smartTracePrompt(const QString &requestId, const QString &traceGroupId, const QString &imagePath, TraceMode mode)
{
    const bool logo = mode == TraceMode::logo;
    QString text = header(logo ? QStringLiteral("Vectorize with AI, Logo & icon") : QStringLiteral("Vectorize with AI, Sketch & line art"), requestId);
    text += QStringLiteral(
                "The app has already traced an image with its classic tracer. The rough result is the group %1, in an open "
                "proposal. The original image is %2: look at it closely. Render {\"selectionOnly\": true} to see the rough "
                "trace.\n\n")
                .arg(traceGroupId, imagePath);
    text += logo ? QStringLiteral(
                       "Redraw it as a clean logo or icon: a few flat colours, true circles and ellipses where the shapes are "
                       "nearly round, straight lines where the edges are nearly straight, and symmetric halves mirrored exactly. "
                       "Keep the number of paths small.\n\n")
                 : QStringLiteral(
                       "Redraw it as line art: centreline strokes (fill none, a stroke width that matches the drawing, round "
                       "caps and joins) that follow the middle of each line, not filled outlines of the ink. Keep lines "
                       "continuous and smooth, with as few anchor points as hold the shape.\n\n");
    text += QStringLiteral(
                "Give the SVG a viewBox with the image's width and height in pixels, so it lines up. Then call:\n\n"
                "replace_objects {\"ids\": [\"%1\"], \"svg\": \"<svg …>…</svg>\", \"name\": \"%2\"}\n\n"
                "Render again and compare with the original. When it is right, call proposal_finish {\"title\": \"Vectorize\", "
                "\"summary\": \"…\"}.")
                .arg(traceGroupId, logo ? QStringLiteral("Logo") : QStringLiteral("Line Art"));
    return text;
}

QString roastPrompt(const QString &requestId, const QString &renderPath, bool selectionOnly)
{
    QString text = header(QStringLiteral("Roast My Design"), requestId);
    text += QStringLiteral("The user asked you to roast %1. A render of it is %2: look at it. Call %3 for the objects and their ids.\n\n")
                .arg(selectionOnly ? QStringLiteral("the selection") : QStringLiteral("the whole artboard"), renderPath,
                     selectionOnly ? QStringLiteral("selection_get") : QStringLiteral("document_get"));
    text += QStringLiteral(
                "Follow the Roast My Design section of AGENTS.md: first the roast (savage, deadpan, about the design and never "
                "the person), then sincere, specific feedback in order of impact, pointing at object ids, then one "
                "suggestedPrompt for a round of variations that applies the feedback. Deliver it in one call:\n\n"
                "show_roast {\"requestId\": \"%1\", \"roast\": \"…\", \"feedback\": [{\"title\": \"…\", \"detail\": \"…\", "
                "\"objectIds\": [\"…\"]}], \"suggestedPrompt\": \"…\"}\n\n"
                "Do not edit the document. Stop after show_roast succeeds.")
                .arg(requestId);
    return text;
}
}
