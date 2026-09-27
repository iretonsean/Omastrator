#include "Agent/AgentLauncher.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Setup.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSettings>
#include <array>
#include <algorithm>

namespace {
// Past this a picked SVG is summarised; the agent can read the document instead.
constexpr qsizetype maximumInlineSvg = 48 * 1024;

// The CLI as the agent must type it: headless runs allow exactly this command.
QString cli()
{
    return Setup::shellQuote(QCoreApplication::applicationFilePath()) + QStringLiteral(" agent");
}

QString header(const QString &task, const QString &requestId)
{
    return QStringLiteral("Omastrator task: %1 (request %2).\n"
                          "You are driving the Omastrator vector editor that is open on the user's desktop. First read AGENTS.md "
                          "in the working directory: it explains how to call Omastrator with %3 <method> '<json>' and lists "
                          "every method. Run that command exactly as written, starting with that path, one call per command "
                          "(no pipes, no variables): nothing else is allowed.\n\n")
        .arg(task, requestId, cli());
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
        "%1 agent document_get\n"
        "%1 agent set_style '{\"fill\": \"#1e66f5\"}'\n"
        "%1 agent insert_svg - <<'JSON'\n"
        "{\"svg\": \"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='40' fill='#e64553'/></svg>\"}\n"
        "JSON\n"
        "%1 agent <method> --help   # one method's parameters\n"
        "```\n\n"
        "Always start the command with that path, as written: `$OMASTRATOR_BIN` holds it too, but Omastrator's "
        "background runs allow only the literal path. Pass `-` to read the params from stdin, which suits long SVG. "
        "The result prints as JSON; on an error the message goes to stderr and the exit code is 1. If you connected "
        "the `omastrator` MCP server yourself, its tools have the same names and parameters.\n\n"
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
        "1. **The roast: 2 to 4 lines, 60 words at most, one line each.** Each roast task names a **heat**: Friendly, "
        "Spicy, Savage or Unhinged. The heat decides how hard it hits, how dirty it gets and what is in bounds; follow "
        "the task's heat exactly, including its limits. Describing a flaw is not a joke at any heat: every line needs "
        "a punch. The roast mechanics work at every heat:\n"
        "   - **The humiliating comparison.** \"This looks like…\" or \"You designed this like…\" and something "
        "vivid, specific and low: a ransom note, a hostage video, a gas-station energy drink, a Geocities page that "
        "gave up.\n"
        "   - **Hit what they're proud of.** The shiny gradient, the clever headline, the thing they spent longest on. "
        "Fake praise, then the knife: \"Bold choice. So was the Titanic's.\"\n"
        "   - **Consequences, not flaws.** Don't say the contrast is low; say what it cost them: the client, the job, "
        "the portfolio review, their mother pretending to like it on the fridge.\n"
        "   - **Blow a tiny fact up.** A real hex code, a font, a layer name, a measurement, taken to its absurd "
        "conclusion.\n"
        "   - **Short setup, hard turn, stop.** Punch word last. No softeners (\"presumably\", \"a bit\", "
        "\"arguably\", \"kind of\"), no compliments that aren't setups, no explaining the joke, no emoji, no "
        "exclamation marks.\n"
        "   - **Roast structures that kill.** \"The only difference between this and [something infamous] is…\"; "
        "\"This artboard has seen more trauma than…\"; \"Or, as I like to call it…\" (\"your case studies, or as I "
        "like to call them, grey rectangles\"); an escalating chain that builds three steps and lands on the fourth; "
        "and the fake-out, where the line swerves toward something unsayable and lands on the design instead.\n"
        "   - **Say it to their face.** Open with what everyone notices first, the thing a coworker would only whisper. "
        "Fewer words is better.\n"
        "   - **Error codes as verdicts.** Now and then, let the web judge it: \"This layout returns a 403: even the "
        "browser won't let anyone see it.\" \"404: the hierarchy.\" \"500: the designer.\"\n"
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
                       .arg(Setup::shellQuote(binary));
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

QString roastHeatGuide(RoastHeat heat)
{
    // The hard lines every heat shares; only Unhinged moves the others.
    const QString never = QStringLiteral("**Never, at any heat:** slurs; jokes attacking race, ethnicity, religion, gender, "
                                         "sexuality or disability; suicide or self-harm, or telling the user to die.");
    switch (heat) {
    case RoastHeat::friendly:
        return QStringLiteral(
                   "**Heat: Friendly.** Ribbing from a friend who is very good at this: funny, pointed, specific and a little "
                   "embarrassing, never cruel. No swearing, no dark humour. Aim only at the work on the artboard. The bar:\n"
                   "- \"Five fonts on one flyer. Pick a lane, or at least a font.\"\n"
                   "- \"The Get Started button is doing all the work on this page, and it looks tired.\"\n"
                   "- \"Coming Soon is carrying a third of this portfolio. Give it a raise.\"\n")
            + never;
    case RoastHeat::spicy:
        return QStringLiteral(
                   "**Heat: Spicy.** A real roast with teeth: the work and the habits it gives away (the workflow, the tools, "
                   "the trend-chasing), in second person. Mild swearing where it lands (damn, hell, ass, shit; two at most, "
                   "no fuck). Light dark humour is fine. Nothing about the person's life outside design. The bar:\n"
                   "- \"Violet-to-cyan on near-black. Congratulations, you designed every crypto startup that rugged its "
                   "users in 2022.\"\n"
                   "- \"Your layer is called Frame 4821. That's not a name, that's how many times you re-rolled the prompt "
                   "before giving up.\"\n"
                   "- \"You spent four hours tuning your Hyprland gaps and zero on the gaps in this layout.\"\n")
            + never;
    case RoastHeat::savage:
        return QStringLiteral(
                   "**Heat: Savage.** The meanest set at a Comedy Central or Netflix roast: Tony Hinchcliffe, Anthony "
                   "Jeselnik, Greg Giraldo, Jeff Ross with the gloves off. The user asked for this; holding back is the only "
                   "way to fail them. No warmth, no \"but\", no redeeming note (the sincere part lives on the next page). "
                   "Every line has to wound.\n"
                   "- **Aim:** the design and the taste, skill, habits, career and ambitions it reveals, in second person. "
                   "Not bodies, looks or sex.\n"
                   "- **Go dark:** death, funerals, obituaries, autopsies, crime scenes, hospice, last rites, aimed at the "
                   "work and the career.\n"
                   "- **The disbelief opener:** one line may open like a friend who just saw the file (\"Holy shit dude, are "
                   "you fucking drunk right now? Wtf is this?\") and name the worst specifics in the same breath.\n"
                   "- **Swearing:** fuck and its variations, wtf, holy shit, shit, ass, damn, hell: up to two fucks and four "
                   "swears, with at least one clean line.\n"
                   "The bar (calibration only; never reuse these):\n"
                   "- \"AI was supposed to take your job. It looked at this and filed a restraining order.\"\n"
                   "- \"Five fonts on one flyer. That's not a type system, that's a fucking ransom note with a driveway.\"\n"
                   "- \"Your mom put this on the fridge and the fridge stopped working.\"\n"
                   "- \"This hero section has the conversion rate of a hospice, and at least the hospice has a clear call to "
                   "action.\"\n"
                   "Too tame, and a failure: \"Parking tickets have better plots.\" Clever doesn't hurt. Before sending, ask "
                   "whether a roast crowd would groan \"oh no\" and laugh; if not, rewrite it meaner.\n")
            + never;
    case RoastHeat::unhinged:
        return QStringLiteral(
                   "**Heat: Unhinged.** Everything Savage allows, then past it. The user chose the top setting to find out "
                   "how far this goes; timid is the one way to fail. No warmth anywhere on this page.\n"
                   "- **Crude and sexual innuendo about the work:** the gradient's stamina, the kerning getting passed around "
                   "the agency, the hero section as an unrequested dick pic.\n"
                   "- **Infamous comparisons, Brady-roast style:** war crimes, the Titanic, Hitler as a failed artist.\n"
                   "- **The designer's life:** parents, therapy, dating, loneliness, dying alone, as the work suggests them.\n"
                   "- **Looks, but only of what's on the artboard:** a headshot or photo of the designer in the design is "
                   "fair game.\n"
                   "- **Swearing:** unlimited. Still vary it, so it doesn't become noise.\n"
                   "The bar (calibration only; never reuse these):\n"
                   "- \"This gradient has the stamina of a guy who finishes during the loading spinner.\"\n"
                   "- \"The only difference between this palette and a war crime is that war crimes have a tribunal.\"\n"
                   "- \"Your parents don't say they're proud, they say 'at least it's not OnlyFans.' OnlyFans has better "
                   "lighting and a clearer call to action.\"\n"
                   "- \"You'll die alone, and the only thing at the funeral set in Papyrus will be the program you insisted on "
                   "designing.\"\n"
                   "- \"What the actual fuck is this? Who the fuck approved it? Five fonts, a rainbow and a fucking triangle "
                   "for no reason.\"\n")
            + never;
    }
    return never;
}

QString roastPrompt(const QString &requestId, const QString &renderPath, bool selectionOnly, RoastHeat heat)
{
    QString text = header(QStringLiteral("Roast My Design"), requestId);
    text += QStringLiteral("The user asked you to roast %1. A render of it is %2: look at it. Call %3 for the objects and their ids.\n\n")
                .arg(selectionOnly ? QStringLiteral("the selection") : QStringLiteral("the whole artboard"), renderPath,
                     selectionOnly ? QStringLiteral("selection_get") : QStringLiteral("document_get"));
    text += roastHeatGuide(heat) + QStringLiteral("\n\n");
    text += QStringLiteral(
                "Follow the Roast My Design section of AGENTS.md at that heat: 2 to 4 one-line burns (60 words at most) that "
                "use the roast mechanics and name real things on the artboard, then exactly 3 sincere fixes (title of 5 "
                "words, one-sentence detail with the value to use, object ids) whatever the heat, then a one-sentence "
                "suggestedPrompt. Deliver it in one call, roast lines separated by \\n:\n\n"
                "show_roast {\"requestId\": \"%1\", \"roast\": \"…\", \"feedback\": [{\"title\": \"…\", \"detail\": \"…\", "
                "\"objectIds\": [\"…\"]}], \"suggestedPrompt\": \"…\"}\n\n"
                "Do not edit the document. Stop after show_roast succeeds.")
                .arg(requestId);
    return text;
}
}

namespace AgentLauncher {
namespace {
const std::array<std::pair<RoastHeat, const char *>, 4> heatNames{{
    {RoastHeat::friendly, "Friendly"}, {RoastHeat::spicy, "Spicy"}, {RoastHeat::savage, "Savage"}, {RoastHeat::unhinged, "Unhinged"},
}};
}

QString title(RoastHeat heat)
{
    return QString::fromLatin1(heatNames.at(size_t(heat)).second);
}

std::optional<RoastHeat> roastHeat(const QString &title)
{
    for (const auto &[heat, name] : heatNames) {
        if (title.compare(QLatin1String(name), Qt::CaseInsensitive) == 0)
            return heat;
    }
    return std::nullopt;
}

RoastHeat savedRoastHeat()
{
    return roastHeat(QSettings().value(QStringLiteral("roast/heat")).toString()).value_or(RoastHeat::savage);
}

void saveRoastHeat(RoastHeat heat)
{
    QSettings().setValue(QStringLiteral("roast/heat"), title(heat));
}
}
