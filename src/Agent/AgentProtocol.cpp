#include "Agent/AgentProtocol.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtGlobal>
#include <unistd.h>

namespace {
// JSON Schemas once, as text: MCP's tools/list and the CLI's help both read them.
constexpr const char *methodTable = R"json([
{"name": "document_get", "group": "read",
 "description": "The whole document as DocumentCodec JSON (objects bottom to top, children after their parent), plus selection, activeLayer and whether a proposal is open. Placed images are summarised unless includeImages is true.",
 "inputSchema": {"type": "object", "properties": {
   "includeImages": {"type": "boolean", "description": "Include placed images as base64 PNG. Default false."}}}},
{"name": "selection_get", "group": "read",
 "description": "The selected objects (and their descendants) as JSON, and the selection's bounds [x, y, width, height].",
 "inputSchema": {"type": "object", "properties": {}}},
{"name": "render", "group": "read",
 "description": "Renders the artboard, or just the selection's bounds, to a PNG so you can look at it. Returns its path, width and height.",
 "inputSchema": {"type": "object", "properties": {
   "scale": {"type": "number", "exclusiveMinimum": 0, "description": "Pixels per point. Default 1."},
   "selectionOnly": {"type": "boolean", "description": "Crop to the selection's bounds. Default false."},
   "path": {"type": "string", "description": "Where to write the PNG. Default: a new temporary file."}}}},
{"name": "insert_svg", "group": "edit",
 "description": "Imports SVG as editable paths, grouped, on top of the active layer, into the proposal. SVG user units are points. Returns the group's id.",
 "inputSchema": {"type": "object", "required": ["svg"], "properties": {
   "svg": {"type": "string", "description": "A complete <svg> document with a viewBox, or width and height."},
   "name": {"type": "string", "description": "The group's name in the Layers panel."},
   "at": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2, "description": "Top-left corner [x, y] of the placed art, or of the fit box. Default: the SVG's own coordinates, or the artboard's centre with fit."},
   "fit": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2, "description": "Scale to fit [width, height], keeping proportions, centred in the box."},
   "center": {"type": "boolean", "description": "Centre the art on the artboard at its own size (ignored with at or fit)."}}}},
{"name": "set_style", "group": "edit",
 "description": "Sets fill, stroke, opacity or blend mode. Fill and stroke reach every path and text inside a group.",
 "inputSchema": {"type": "object", "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "description": "Object ids. Default: the selection."},
   "fill": {"description": "\"none\", a colour such as \"#ff6600\", or DocumentCodec paint JSON {kind: none|solid|linearGradient|radialGradient, color, stops: [{offset, color}], start, end}.", "type": ["string", "object"]},
   "stroke": {"type": "object", "description": "Stroke fields to change: paint (as fill), color (shorthand for a solid paint), width, cap (butt|square|round), join (miter|bevel|round), miterLimit, dashes, align (center|inside|outside), startArrow and endArrow (none|arrow|triangle|circle|square|bar), arrowScale (percent)."},
   "opacity": {"type": "number", "minimum": 0, "maximum": 1},
   "blendMode": {"type": "string", "description": "normal, multiply, screen, overlay, softLight, darken, lighten, difference, colorDodge, colorBurn, hue, saturation, color or luminosity (document_get's names, such as \"Soft Light\", work too)."}}}},
{"name": "transform", "group": "edit",
 "description": "Moves, rotates or scales objects. Either a matrix, or scale then rotate about origin, then translate.",
 "inputSchema": {"type": "object", "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "description": "Object ids. Default: the selection."},
   "matrix": {"type": "array", "items": {"type": "number"}, "minItems": 6, "maxItems": 6, "description": "[a, b, c, d, e, f]: x' = a*x + c*y + e, y' = b*x + d*y + f."},
   "translate": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
   "rotate": {"type": "number", "description": "Degrees, clockwise on screen."},
   "scale": {"description": "A factor, or [sx, sy].", "type": ["number", "array"], "items": {"type": "number"}},
   "origin": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2, "description": "Centre of rotate and scale. Default: the objects' bounds centre."}}}},
{"name": "arrange", "group": "edit",
 "description": "Changes stacking order within each object's parent.",
 "inputSchema": {"type": "object", "required": ["order"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "description": "Object ids. Default: the selection."},
   "order": {"type": "string", "enum": ["bringToFront", "bringForward", "sendBackward", "sendToBack"]}}}},
{"name": "align", "group": "edit",
 "description": "Aligns objects to their combined bounds, or to the artboard (always the artboard for one object).",
 "inputSchema": {"type": "object", "required": ["edge"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "description": "Object ids. Default: the selection."},
   "edge": {"type": "string", "enum": ["left", "horizontalCenter", "right", "top", "verticalCenter", "bottom"]},
   "target": {"type": "string", "enum": ["selection", "artboard"]}}}},
{"name": "distribute", "group": "edit",
 "description": "Spaces three or more objects' centres evenly between the outermost two.",
 "inputSchema": {"type": "object", "required": ["axis"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "description": "Object ids. Default: the selection."},
   "axis": {"type": "string", "enum": ["horizontal", "vertical"]}}}},
{"name": "group", "group": "edit",
 "description": "Groups objects above the topmost of them. Returns the group's id.",
 "inputSchema": {"type": "object", "required": ["ids"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "minItems": 1},
   "name": {"type": "string"}}}},
{"name": "ungroup", "group": "edit",
 "description": "Releases groups' children into their parent, in place.",
 "inputSchema": {"type": "object", "required": ["ids"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "minItems": 1}}}},
{"name": "pathfinder", "group": "edit",
 "description": "Combines the paths under ids into one. minusFront cuts the upper paths from the bottom one. Returns the new path's id.",
 "inputSchema": {"type": "object", "required": ["ids", "operation"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "minItems": 1},
   "operation": {"type": "string", "enum": ["unite", "intersect", "minusFront", "exclude"]}}}},
{"name": "delete", "group": "edit",
 "description": "Deletes objects and everything inside them.",
 "inputSchema": {"type": "object", "required": ["ids"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "minItems": 1}}}},
{"name": "select", "group": "edit",
 "description": "Selects objects; an empty list deselects. Later calls default to the selection.",
 "inputSchema": {"type": "object", "required": ["ids"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}}}}},
{"name": "update_object", "group": "edit",
 "description": "Replaces one object with DocumentCodec object JSON (as document_get returns it). The id picks the object; its parent stays.",
 "inputSchema": {"type": "object", "required": ["object"], "properties": {
   "object": {"type": "object"}}}},
{"name": "replace_objects", "group": "edit",
 "description": "Swaps objects for new SVG art in their place in the z-order, fitted to their bounds. How smart tracing returns cleaned-up art. Returns the new group's id.",
 "inputSchema": {"type": "object", "required": ["ids", "svg"], "properties": {
   "ids": {"type": "array", "items": {"type": "string"}, "minItems": 1},
   "svg": {"type": "string"},
   "name": {"type": "string"},
   "fit": {"type": "boolean", "description": "Fit the art to the replaced objects' bounds. Default true; false keeps the SVG's coordinates."}}}},
{"name": "proposal_finish", "group": "edit",
 "description": "Ends your turn. The user sees title and summary over the canvas and presses Enter to keep the proposal or Esc to discard it. You cannot accept it yourself.",
 "inputSchema": {"type": "object", "properties": {
   "title": {"type": "string", "description": "A short name; the undo step becomes \"AI: <title>\"."},
   "summary": {"type": "string", "description": "One or two sentences on what changed."}}}},
{"name": "open", "group": "files",
 "description": "Opens a document, SVG or image in a new tab.",
 "inputSchema": {"type": "object", "required": ["path"], "properties": {
   "path": {"type": "string"}}}},
{"name": "save", "group": "files",
 "description": "Saves the document as .omai, to path or to its own file. Refused while a proposal waits for the user.",
 "inputSchema": {"type": "object", "properties": {
   "path": {"type": "string"}}}},
{"name": "export", "group": "files",
 "description": "Exports the artboard as it shows now, proposal included.",
 "inputSchema": {"type": "object", "required": ["path"], "properties": {
   "path": {"type": "string"},
   "format": {"type": "string", "enum": ["pdf", "svg", "png", "jpeg"], "description": "Default: from the file name."},
   "scale": {"type": "number", "exclusiveMinimum": 0, "description": "PNG and JPEG pixels per point. Default 1."},
   "quality": {"type": "integer", "minimum": 0, "maximum": 100, "description": "JPEG quality. Default 90."},
   "transparent": {"type": "boolean", "description": "PNG without the artboard's paper."}}}},
{"name": "place", "group": "files",
 "description": "Places an image or SVG file on the artboard, into the proposal. Returns the new object's id.",
 "inputSchema": {"type": "object", "required": ["path"], "properties": {
   "path": {"type": "string"}}}},
{"name": "show_variations", "group": "panels",
 "description": "Fills the Variations panel with options for the user to pick from. Not a document edit.",
 "inputSchema": {"type": "object", "required": ["requestId", "variations"], "properties": {
   "requestId": {"type": "string", "description": "The id your prompt gave you."},
   "variations": {"type": "array", "minItems": 1, "items": {"type": "object", "required": ["name", "svg"], "properties": {
     "name": {"type": "string"}, "svg": {"type": "string"}, "note": {"type": "string"}}}}}}},
{"name": "show_roast", "group": "panels",
 "description": "Fills the Roast panel: the roast, then sincere feedback, then the prompt for \"Make variations from this feedback\". Not a document edit.",
 "inputSchema": {"type": "object", "required": ["requestId", "roast", "feedback", "suggestedPrompt"], "properties": {
   "requestId": {"type": "string"},
   "roast": {"type": "string", "description": "2 to 4 one-line burns separated by newlines, 60 words at most."},
   "feedback": {"type": "array", "minItems": 1, "maxItems": 4, "description": "3 fixes, highest impact first.", "items": {"type": "object", "required": ["title", "detail"], "properties": {
     "title": {"type": "string"}, "detail": {"type": "string"},
     "objectIds": {"type": "array", "items": {"type": "string"}}}}},
   "suggestedPrompt": {"type": "string"}}}},
{"name": "trace_image", "group": "panels",
 "description": "Runs the classic Image Trace on a placed image, into the proposal: the image becomes a group of filled paths. Returns the group's id, the paths' ids and the original pixels as a PNG path to compare against.",
 "inputSchema": {"type": "object", "required": ["mode"], "properties": {
   "id": {"type": "string", "description": "The image object. Default: the one selected image."},
   "mode": {"type": "string", "enum": ["color", "blackAndWhite"]},
   "colors": {"type": "integer", "minimum": 2, "maximum": 16, "description": "Colour mode's palette size. Default 6."}}}},
{"name": "select_tool", "group": "session",
 "description": "Chooses the canvas tool, as clicking it in the toolbar does. Works with no document open. Returns the tool now chosen.",
 "inputSchema": {"type": "object", "required": ["tool"], "properties": {
   "tool": {"type": "string", "description": "select, directSelect, pen, pencil, text, line, rectangle, roundedRectangle, ellipse, polygon, star, shapeBuilder, scissors, rotate, scale, gradient, eyedropper, hand or zoom (move, direct, type and eyedrop work too)."}}}},
{"name": "status_get", "group": "session",
 "description": "What the app is doing: the tool, whether a document is open, the proposal waiting for the user, the agent task it waits on and the variations ready. Not a document read.",
 "inputSchema": {"type": "object", "properties": {}}},
{"name": "apply_color", "group": "desktop", "mcp": false,
 "description": "The user's own colour pick: sets the selection's fill or stroke colour as one undo step, or the default for new objects when nothing is selected. Not for agents: agent edits go through set_style.",
 "inputSchema": {"type": "object", "required": ["color"], "properties": {
   "color": {"type": "string", "description": "A colour such as \"#ff6600\"."},
   "target": {"type": "string", "enum": ["fill", "stroke"], "description": "Default fill."}}}},
{"name": "swatches_get", "group": "desktop",
 "description": "The Swatches panel's groups of named colours.",
 "inputSchema": {"type": "object", "properties": {}}},
{"name": "swatches_add", "group": "desktop", "mcp": false,
 "description": "Adds colours to a swatch group, creating it. A colour already in the group is skipped. Returns how many were added.",
 "inputSchema": {"type": "object", "required": ["swatches"], "properties": {
   "group": {"type": "string", "description": "Default \"Swatches\"."},
   "swatches": {"type": "array", "minItems": 1, "items": {"type": "object", "required": ["color"], "properties": {
     "name": {"type": "string"}, "color": {"type": "string"}}}},
   "replace": {"type": "boolean", "description": "Empty the group first. Default false."}}}},
{"name": "open_capture", "group": "desktop", "mcp": false,
 "description": "Opens a screenshot as a new document and runs Image Trace on it, as two undo steps. The traced group is offered to Vectorize with AI next.",
 "inputSchema": {"type": "object", "required": ["path"], "properties": {
   "path": {"type": "string"},
   "trace": {"type": "boolean", "description": "Default true."},
   "colors": {"type": "integer", "minimum": 2, "maximum": 16, "description": "Default 6."}}}},
{"name": "new_document", "group": "desktop", "mcp": false,
 "description": "Brings Omastrator forward on a new tab's New Document sheet, as File ▸ New does.",
 "inputSchema": {"type": "object", "properties": {}}},
{"name": "show_panel", "group": "desktop", "mcp": false,
 "description": "Brings Omastrator forward showing one panel or sheet.",
 "inputSchema": {"type": "object", "required": ["panel"], "properties": {
   "panel": {"type": "string", "enum": ["swatches", "variations", "roast", "connectAgent"]}}}},
{"name": "ai_start", "group": "desktop", "mcp": false,
 "description": "Starts one of the app's AI flows for the user, as its menu entry does. Generate and Edit open their sheet unless a prompt is given. Vectorize takes the selected image, else the last traced screenshot. Cancel stops waiting.",
 "inputSchema": {"type": "object", "required": ["flow"], "properties": {
   "flow": {"type": "string", "enum": ["generate", "edit", "roast", "vectorize", "cancel", "handoff"]},
   "prompt": {"type": "string"},
   "count": {"type": "integer", "minimum": 1, "maximum": 6, "description": "Generate's variations. Default 3."},
   "fitToSelection": {"type": "boolean"},
   "mode": {"type": "string", "enum": ["logo", "sketch"], "description": "Vectorize's mode. Default logo."}}}},
{"name": "live", "group": "desktop", "mcp": false,
 "description": "Live mode: open a page or project in Omastrator's own Chromium with the editing overlay, and act on it. start with neither url nor folder opens the Live sheet. deploy writes the live edits back, commits, pushes and deploys; save stops after the push.",
 "inputSchema": {"type": "object", "required": ["action"], "properties": {
   "action": {"type": "string", "enum": ["start", "stop", "select", "edit", "status", "screenshot", "writeBack", "ask", "agentDone",
                                         "review", "discard", "save", "deploy", "cancel", "history", "restore", "details", "remember",
                                         "github", "handoff"]},
   "confirm": {"type": "boolean", "description": "deploy: the user confirmed deploying to production."},
   "remember": {"type": "boolean", "description": "deploy with confirm: don't ask again for this project."},
   "github": {"type": "string", "description": "deploy, save: a name to create a private GitHub repository with first; empty for not now."},
   "connect": {"type": "boolean", "description": "github: open a terminal running gh auth login when not logged in."},
   "list": {"type": "boolean", "description": "history: return the commits without opening the History panel."},
   "prompt": {"type": "string", "description": "ask: what the agent should change about the selection."},
   "requestId": {"type": "string", "description": "agentDone: the id your task gave you."},
   "summary": {"type": "string", "description": "agentDone: one line on what changed, or why nothing did."},
   "id": {"type": "string", "description": "discard: one write-back (default: every one not committed yet). restore: a commit."},
   "path": {"type": "string", "description": "screenshot: where to write the PNG."},
   "url": {"type": "string"},
   "folder": {"type": "string", "description": "The page's code; with url, the user has confirmed it. handoff: the app's source. deploy: the project, default Live's."},
   "command": {"type": "string", "description": "start: an Electron app's command line, relaunched with debugging in a dedicated profile."},
   "app": {"type": "boolean", "description": "start with url: open it as an app window, as an Omarchy web app."},
   "on": {"type": "boolean", "description": "select without a selector: whether clicks in the page select elements."},
   "add": {"type": "boolean", "description": "select with a selector: add to the selection, as Shift-click does."},
   "selector": {"type": "string"},
   "property": {"type": "string"},
   "value": {"type": "string"}}}},
{"name": "live_deployed", "group": "desktop", "mcp": false,
 "description": "Ends a Live deploy task: where the site is live, and the one command that deploys it, which the user may keep for next time. Never put a secret in it. On failure, error instead of url.",
 "inputSchema": {"type": "object", "properties": {
   "requestId": {"type": "string", "description": "The id your deploy task gave you."},
   "url": {"type": "string", "description": "The live URL."},
   "command": {"type": "string", "description": "A shell command that deploys from the project folder with no questions."},
   "error": {"type": "string", "description": "Why it couldn't deploy, in one line."}}}},
{"name": "command", "group": "desktop", "mcp": false,
 "description": "One of the user's own commands, as a voice command runs it: each is a normal undo step.",
 "inputSchema": {"type": "object", "required": ["name"], "properties": {
   "name": {"type": "string", "enum": ["undo", "redo", "zoomIn", "zoomOut", "zoomToFit", "actualSize", "selectAll", "deselect", "group", "ungroup",
                                       "delete", "duplicate", "arrange", "align", "distribute", "fill", "stroke", "strokeWidth", "opacity"]},
   "order": {"type": "string", "enum": ["bringToFront", "bringForward", "sendBackward", "sendToBack"]},
   "edge": {"type": "string", "enum": ["left", "horizontalCenter", "right", "top", "verticalCenter", "bottom"]},
   "target": {"type": "string", "enum": ["selection", "artboard"]},
   "axis": {"type": "string", "enum": ["horizontal", "vertical"]},
   "color": {"type": "string"},
   "width": {"type": "number", "minimum": 0},
   "value": {"type": "number", "minimum": 0, "maximum": 1}}}},
{"name": "paste_svg", "group": "desktop", "mcp": false,
 "description": "The user's paste of SVG from the clipboard: editable paths, grouped and centred, as one undo step. A new document is made when none is open.",
 "inputSchema": {"type": "object", "required": ["svg"], "properties": {
   "svg": {"type": "string"},
   "name": {"type": "string"}}}}
])json";
}

namespace AgentProtocol {
const std::vector<Method> &methods()
{
    static const std::vector<Method> table = [] {
        std::vector<Method> result;
        QJsonParseError error{};
        const QJsonArray array = QJsonDocument::fromJson(QByteArray(methodTable), &error).array();
        Q_ASSERT_X(error.error == QJsonParseError::NoError, "AgentProtocol", "method table is not valid JSON");
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            result.push_back({object["name"].toString(), object["group"].toString(), object["description"].toString(),
                              object["inputSchema"].toObject(), object["mcp"].toBool(true)});
        }
        return result;
    }();
    return table;
}

const Method *method(const QString &name)
{
    for (const Method &candidate : methods()) {
        if (candidate.name == name)
            return &candidate;
    }
    return nullptr;
}

QString socketPath()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_SOCKET");
    if (!overridden.isEmpty())
        return overridden;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir(runtime).exists())
        return QDir(runtime).filePath(QStringLiteral("omastrator.sock"));
    return QStringLiteral("/tmp/omastrator-%1.sock").arg(getuid());
}

QByteArray frame(const QJsonObject &message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject request(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

QJsonObject result(const QJsonValue &id, const QJsonValue &result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject error(const QJsonValue &id, int code, const QString &message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject respond(const QByteArray &line, const Call &call)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError)
        return error(QJsonValue::Null, parseError, QStringLiteral("The request is not JSON: %1.").arg(parse.errorString()));
    const QJsonObject message = document.object();
    const QJsonValue id = message.contains("id") ? message["id"] : QJsonValue(QJsonValue::Undefined);
    const QJsonValue replyID = id.isUndefined() ? QJsonValue(QJsonValue::Null) : id;
    if (!document.isObject() || message["jsonrpc"].toString() != QLatin1String("2.0") || !message["method"].isString())
        return error(replyID, invalidRequest, QStringLiteral("Send a JSON-RPC 2.0 request with a method."));
    const QJsonValue params = message["params"];
    if (!params.isUndefined() && !params.isNull() && !params.isObject())
        return error(replyID, invalidParams, QStringLiteral("Params must be a JSON object."));
    QJsonObject reply;
    try {
        reply = result(replyID, call(message["method"].toString(), params.toObject()));
    } catch (const Error &failure) {
        reply = error(replyID, failure.code, failure.message());
    } catch (const std::exception &failure) {
        reply = error(replyID, internalError, QString::fromUtf8(failure.what()));
    }
    // A notification gets no answer, though it still ran.
    return id.isUndefined() ? QJsonObject() : reply;
}

QString helpText()
{
    QString text = QStringLiteral(
        "Usage: omastrator agent <method> [json-params | -]\n"
        "       omastrator agent <method> --help\n"
        "       omastrator --mcp\n\n"
        "Sends one request to the running Omastrator and prints the result as JSON.\n"
        "Pass - to read the params from stdin. Edits go into a proposal the user\n"
        "keeps with Enter or discards with Esc.\n");
    QString group;
    for (const Method &method : methods()) {
        if (method.group != group) {
            group = method.group;
            text += QStringLiteral("\n%1:\n").arg(group.left(1).toUpper() + group.mid(1));
        }
        text += QStringLiteral("  %1%2\n").arg(method.name.leftJustified(17), method.description.section(QLatin1String(". "), 0, 0));
    }
    return text;
}

QString methodHelp(const Method &method)
{
    QString text = QStringLiteral("%1: %2\n\nParameters:\n").arg(method.name, method.description);
    const QJsonObject properties = method.inputSchema["properties"].toObject();
    const QJsonArray required = method.inputSchema["required"].toArray();
    if (properties.isEmpty())
        text += QStringLiteral("  (none)\n");
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QJsonObject property = it.value().toObject();
        QString type = property["type"].toString();
        if (property["type"].isArray()) {
            QStringList types;
            for (const QJsonValue &each : property["type"].toArray())
                types << each.toString();
            type = types.join(QLatin1Char('|'));
        }
        if (property.contains("enum")) {
            QStringList values;
            for (const QJsonValue &each : property["enum"].toArray())
                values << each.toString();
            type = values.join(QLatin1Char('|'));
        }
        const bool isRequired = required.contains(it.key());
        text += QStringLiteral("  %1 (%2%3)%4\n")
                    .arg(it.key(), type, isRequired ? QStringLiteral(", required") : QString(),
                         property.contains("description") ? QStringLiteral(": ") + property["description"].toString() : QString());
    }
    return text;
}
}
