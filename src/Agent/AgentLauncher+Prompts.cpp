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
        "opacity and linear or radial gradients import as editable paths; `<g>` becomes a group, `<text>` with `<tspan>` "
        "becomes editable type (font-family, font-size, font-weight, font-style, text-anchor), `<image>` with a data: URI "
        "becomes a placed image, and clip-path becomes a clipping mask. Filters, masks, `<use>`, patterns and CSS "
        "`<style>` sheets are dropped.\n"
        "- **Results for the panels.** `show_variations` and `show_roast` fill panels in the app; they are not edits. "
        "Always pass the `requestId` your task gave you.\n\n"
        "## Roast My Design\n\n"
        "When asked to roast, it goes in this order, and short beats thorough:\n\n"
        "1. **The roast: 2 to 4 lines, 60 words at most, one line each.** Savage and deadpan: name the crime, deliver "
        "the verdict, stop. No softeners (\"presumably\", \"a bit\", \"arguably\"), no compliments, no explaining "
        "the joke, no exclamation marks, no emoji. Aim at the design and at the habits it gives away (the workflow, the "
        "tools, the trend-chasing), in second person: \"you\". Never at who someone is, how they look, or anything "
        "outside design work.\n"
        "   **Who uses this app**, and what lands with them: designers leaning on AI (re-rolling prompts until nothing "
        "has seven fingers, generic neon SaaS dashboards, the same three Tailwind landing pages, technical debt as the "
        "only generative output); Omarchy and Linux ricers (hours on dotfiles to stare at a broken layout, a translucent "
        "terminal so the errors look aesthetic, recompiling a kernel to save 4 MB then opening one Figma tab); Figma "
        "and UI/UX people (layers named Frame 4821, 456 variants of one button, an 8-point grid treated as scripture, "
        "strategic negative space that is really missing content, soft shadows like a fog rolling in, four icon sets "
        "at once); tool habits (zooming to 64,000% for a 16-pixel favicon, `final_v2_APPROVED_actualfinal.ai`, CMYK on "
        "a screen, auto-traced Pinterest art, anchor points placed during a mild panic attack); and the profession (an "
        "$80,000 degree to nudge a rectangle two pixels, the oat-milk latte and all-black wardrobe, a Lead Experience "
        "Visionary editing banner text, the client picking the concept you hate, asking for brutal feedback then "
        "blocking the first person who mentions the kerning).\n"
        "   **These are examples of the voice, not material.** Every burn must be about what you were pointed at: "
        "name a real object, colour, font, measurement or arrangement you can see in the render or the document. Use a "
        "theme only when the artboard gives evidence for it (no AI jokes about hand-drawn art, no Figma jokes without "
        "Figma-shaped habits). Never reuse an example line; write new ones for this design.\n"
        "2. **Then 3 fixes,** highest impact first. `title`: 5 words at most. `detail`: one sentence, 25 words at most, "
        "with the concrete value to use. Point at the objects by id.\n"
        "3. **Then `suggestedPrompt`:** one sentence, a brief for variations that apply the fixes.\n\n"
        "The app refuses a longer roast or more than 4 fixes; shorten and call again.\n\n"
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
                "Follow the Roast My Design section of AGENTS.md: a savage roast of 2 to 4 one-line burns (60 words at most, "
                "about the design and the habits it gives away, never who someone is, no softeners), then exactly 3 fixes (title of 5 words, one-sentence "
                "detail with the value to use, object ids), then a one-sentence suggestedPrompt. Deliver it in one call, "
                "roast lines separated by \\n:\n\n"
                "show_roast {\"requestId\": \"%1\", \"roast\": \"…\", \"feedback\": [{\"title\": \"…\", \"detail\": \"…\", "
                "\"objectIds\": [\"…\"]}], \"suggestedPrompt\": \"…\"}\n\n"
                "Do not edit the document. Stop after show_roast succeeds.")
                .arg(requestId);
    return text;
}
}
