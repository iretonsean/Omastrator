#include "Agent/AgentEdits.h"
#include "Agent/AgentParams.h"
#include "Agent/AgentTools.h"
#include "Document/EditorSession.h"
#include "Document/Swatches.h"
#include "IO/ImageImporter.h"
#include "IO/SvgImporter.h"
#include <QFileInfo>
#include <QJsonArray>

// The island's Capture mode (docs/OS-SUITE.md): the user's own actions, so
// each is a normal undo step, not a proposal. MCP does not list these.
using AgentProtocol::Error;
using namespace AgentParams;

namespace {
QColor colorParam(const QJsonObject &params, const QString &key)
{
    const QString given = requiredString(params, key);
    const QColor color = QColor::fromString(given.trimmed());
    if (!color.isValid())
        fail(QStringLiteral("“%1” is not a colour: %2.").arg(key, given));
    return color;
}
}

EditorSession &AgentTools::idleSession()
{
    EditorSession &current = session();
    if (hasProposal() && m_session == &current)
        throw Error(AgentProtocol::busy, QStringLiteral("An AI proposal is waiting. Press Enter to keep it or Esc to discard it first."));
    if (current.isInteracting())
        throw Error(AgentProtocol::busy, QStringLiteral("You're in the middle of an edit. Try again when it's done."));
    return current;
}

void AgentTools::commit(EditorSession &target, const QString &name, const VectorDocument &document, const std::vector<QUuid> &selection)
{
    target.beginInteraction(name);
    target.previewDocument(document, selection);
    target.commitInteraction();
}

std::optional<AgentTools::Capture> AgentTools::pendingCapture()
{
    EditorSession *front = m_host.session();
    if (!m_capture || !m_capture->session || m_capture->session != front || !front->hasDocument()
        || !front->document()->find(m_capture->group))
        return std::nullopt;
    return m_capture;
}

QJsonObject AgentTools::applyColor(const QJsonObject &params)
{
    const QColor color = colorParam(params, QStringLiteral("color"));
    const int target = choice(params, QStringLiteral("target"), {QStringLiteral("fill"), QStringLiteral("stroke")}).value_or(0);
    EditorSession *current = m_host.session();
    if (!current)
        throw Error(AgentProtocol::noDocument, QStringLiteral("Omastrator has no window open."));
    // With nothing selected the colour becomes the default for the next shape.
    if (!current->hasDocument() || !current->hasSelection()) {
        if (target == 0) {
            current->setDefaultFill(Paint::solid(color));
        } else {
            StrokeStyle stroke = current->defaultStroke();
            stroke.paint = Paint::solid(color);
            stroke.width = std::max(stroke.width, 1.0);
            current->setDefaultStroke(stroke);
        }
        return {{"target", target == 0 ? "fill" : "stroke"}, {"color", color.name()}, {"changed", 0}};
    }
    EditorSession &editing = idleSession();
    VectorDocument edited = *editing.document();
    int changed = 0;
    for (const QUuid &id : editing.selectedLeaves()) {
        VectorObject *object = edited.find(id);
        if (!object || !object->hasPaint() || edited.isEffectivelyLocked(id))
            continue;
        if (target == 0) {
            object->fill = Paint::solid(color);
        } else {
            object->stroke.paint = Paint::solid(color);
            object->stroke.width = std::max(object->stroke.width, 1.0);
        }
        ++changed;
    }
    if (changed > 0)
        commit(editing, target == 0 ? QStringLiteral("Fill") : QStringLiteral("Stroke"), edited, editing.selection());
    return {{"target", target == 0 ? "fill" : "stroke"}, {"color", color.name()}, {"changed", changed}};
}

QJsonObject AgentTools::swatchesGet(const QJsonObject &)
{
    Swatches *library = m_host.swatches();
    return {{"groups", library ? library->toJson() : QJsonArray()}};
}

QJsonObject AgentTools::swatchesAdd(const QJsonObject &params)
{
    Swatches *library = m_host.swatches();
    if (!library)
        throw Error(AgentProtocol::internalError, QStringLiteral("This Omastrator has no Swatches panel."));
    const QString group = string(params, QStringLiteral("group")).value_or(Swatches::defaultGroup).trimmed();
    if (group.isEmpty())
        fail(QStringLiteral("“group” must not be empty."));
    if (!params["swatches"].isArray() || params["swatches"].toArray().isEmpty())
        fail(QStringLiteral("“swatches” must be a list such as [{\"name\": \"Accent\", \"color\": \"#0a84ff\"}]."));
    std::vector<Swatch> swatches;
    for (const QJsonValue &each : params["swatches"].toArray()) {
        if (!each.isObject())
            fail(QStringLiteral("Each swatch must be an object with a “color”."));
        swatches.push_back({each["name"].toString(), colorParam(each.toObject(), QStringLiteral("color"))});
    }
    const int added = library->add(group, swatches, boolean(params, QStringLiteral("replace"), false));
    return {{"group", group}, {"added", added}};
}

QJsonObject AgentTools::openCapture(const QJsonObject &params)
{
    const QString path = requiredString(params, QStringLiteral("path"));
    const auto colors = number(params, QStringLiteral("colors"));
    if (colors && (*colors < 2 || *colors > 16 || *colors != std::floor(*colors)))
        fail(QStringLiteral("“colors” must be a whole number from 2 to 16."));
    const QImage image = ImageImporter::read(path);
    if (EditorSession *current = m_host.session(); current && current->isInteracting())
        throw Error(AgentProtocol::busy, QStringLiteral("Finish the edit or proposal in front first."));
    if (const QString failure = m_host.newDocument(QSizeF(image.size())); !failure.isEmpty())
        throw Error(AgentProtocol::fileError, failure);
    EditorSession &fresh = idleSession();
    const QUuid placed = fresh.placeImage(image, QStringLiteral("Screenshot"), QPointF(image.width() / 2.0, image.height() / 2.0));
    QJsonObject result{{"imageId", idString(placed)}, {"width", image.width()}, {"height", image.height()}};
    if (!boolean(params, QStringLiteral("trace"), true))
        return result;
    fresh.select({placed});
    fresh.traceSelectedImage(int(colors.value_or(6)));
    if (fresh.document()->find(placed) || fresh.selection().size() != 1) {
        result["traced"] = false;
        return result;
    }
    const QUuid group = fresh.selection().front();
    m_capture = Capture{&fresh, group, QFileInfo(path).absoluteFilePath()};
    result["traced"] = true;
    result["id"] = idString(group);
    result["paths"] = int(fresh.document()->children(group).size());
    return result;
}

QJsonObject AgentTools::pasteSvg(const QJsonObject &params)
{
    const QString svg = requiredString(params, QStringLiteral("svg"));
    VectorDocument art;
    try {
        art = SvgImporter::parse(svg.toUtf8());
    } catch (const FileError &failure) {
        fail(QStringLiteral("The clipboard's SVG could not be read: %1").arg(failure.message()));
    }
    if (art.objects.empty())
        fail(QStringLiteral("The clipboard's SVG has nothing to draw."));
    EditorSession *current = m_host.session();
    if (!current || !current->hasDocument()) {
        if (const QString failure = m_host.newDocument(art.size.isEmpty() ? QSizeF(800, 600) : art.size); !failure.isEmpty())
            throw Error(AgentProtocol::fileError, failure);
    }
    EditorSession &editing = idleSession();
    VectorDocument edited = *editing.document();
    const auto layer = AgentEdits::openLayer(edited, editing.activeLayer());
    if (!layer)
        throw Error(AgentProtocol::busy, QStringLiteral("Every layer is locked or hidden, so there is nowhere to paste."));
    const QUuid id = AgentEdits::insertArt(edited, art, string(params, QStringLiteral("name")).value_or(QStringLiteral("Pasted SVG")), *layer);
    const QPointF shift = QPointF(edited.size.width() / 2, edited.size.height() / 2) - edited.bounds(id).center();
    edited.transform(id, QTransform::fromTranslate(shift.x(), shift.y()));
    commit(editing, QStringLiteral("Paste SVG"), edited, {id});
    return {{"id", idString(id)}, {"paths", int(AgentEdits::leaves(edited, {id}).size())}};
}

QJsonObject AgentTools::newDocument(const QJsonObject &)
{
    if (const QString failure = m_host.showNewDocument(); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"shown", true}};
}

QJsonObject AgentTools::showPanel(const QJsonObject &params)
{
    static const QStringList panels{QStringLiteral("swatches"), QStringLiteral("variations"), QStringLiteral("roast"), QStringLiteral("connectAgent")};
    const QString panel = panels.value(*choice(params, QStringLiteral("panel"), panels, true));
    if (const QString failure = m_host.showPanel(panel); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"shown", panel}};
}

QJsonObject AgentTools::aiStart(const QJsonObject &params)
{
    static const QStringList flows{QStringLiteral("generate"), QStringLiteral("edit"), QStringLiteral("roast"), QStringLiteral("vectorize"),
                                   QStringLiteral("cancel"), QStringLiteral("handoff")};
    AgentHost::AiRequest request;
    request.flow = flows.value(*choice(params, QStringLiteral("flow"), flows, true));
    request.prompt = string(params, QStringLiteral("prompt")).value_or(QString()).trimmed();
    const auto count = number(params, QStringLiteral("count"));
    if (count && (*count < 1 || *count > 6 || *count != std::floor(*count)))
        fail(QStringLiteral("“count” must be a whole number from 1 to 6."));
    request.count = int(count.value_or(3));
    request.fitToSelection = boolean(params, QStringLiteral("fitToSelection"), false);
    request.sketch = choice(params, QStringLiteral("mode"), {QStringLiteral("logo"), QStringLiteral("sketch")}).value_or(0) == 1;
    if (const QString failure = m_host.startAi(request); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"started", request.flow}};
}

const QStringList &AgentTools::designActions()
{
    static const QStringList actions{"on",     "off",    "toggle", "status", "tool", "alt",  "select", "selectArt", "deselect", "measure", "draw",
                                     "action", "ask",    "keep",   "discard", "send", "undo", "redo",   "clear",     "onboarding", "desk",
                                     "lift",   "look",   "restyle", "reset"};
    return actions;
}

QJsonObject AgentTools::design(const QJsonObject &params)
{
    const QString action = designActions().value(*choice(params, QStringLiteral("action"), designActions(), true));
    QJsonObject result;
    if (const QString failure = m_host.design(action, params, result); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return result;
}

QJsonObject AgentTools::showWindow(const QJsonObject &params)
{
    QStringList files;
    for (const QJsonValue &file : params["files"].toArray())
        files << file.toString();
    if (const QString failure = m_host.showWindow(files, params["raise"].toBool(true)); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"shown", true}};
}

QJsonObject AgentTools::quitApp(const QJsonObject &)
{
    if (const QString failure = m_host.quitApp(); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"quitting", true}};
}

QJsonObject AgentTools::live(const QJsonObject &params)
{
    static const QStringList actions{"start", "stop", "select", "edit", "status", "screenshot", "writeBack", "ask", "agentDone", "review", "discard",
                                     "save", "deploy", "cancel", "history", "restore", "details", "remember", "github", "handoff", "folders"};
    const QString action = actions.value(*choice(params, QStringLiteral("action"), actions, true));
    if (action == QLatin1String("edit")) {
        requiredString(params, QStringLiteral("selector"));
        requiredString(params, QStringLiteral("property"));
        requiredString(params, QStringLiteral("value"));
    }
    QJsonObject result;
    if (const QString failure = m_host.live(action, params, result); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return result;
}

QJsonObject AgentTools::liveDeployed(const QJsonObject &params)
{
    const QString url = string(params, QStringLiteral("url")).value_or(QString()).trimmed();
    if (!url.isEmpty() && !url.startsWith(QLatin1String("http://")) && !url.startsWith(QLatin1String("https://")))
        fail(QStringLiteral("“url” must be the live http or https address."));
    QJsonObject result;
    const QJsonObject forwarded{{"requestId", params["requestId"]}, {"url", url}, {"command", params["command"]}, {"error", params["error"]}};
    if (const QString failure = m_host.live(QStringLiteral("deployed"), forwarded, result); !failure.isEmpty())
        throw Error(AgentProtocol::busy, failure);
    return {{"recorded", true}};
}

QJsonObject AgentTools::command(const QJsonObject &params)
{
    static const QStringList names{"undo", "redo", "zoomIn", "zoomOut", "zoomToFit", "actualSize", "selectAll", "deselect", "group", "ungroup",
                                   "delete", "duplicate", "arrange", "align", "distribute", "fill", "stroke", "strokeWidth", "opacity"};
    const QString name = names.value(*choice(params, QStringLiteral("name"), names, true));
    // Colours reuse the Capture path: the selection's, else the next shape's.
    if (name == QLatin1String("fill") || name == QLatin1String("stroke"))
        return applyColor({{"color", params["color"]}, {"target", name}});
    EditorSession &current = session();
    // The view can change mid-proposal; the document can't.
    if (name == QLatin1String("zoomIn"))
        current.zoomIn();
    else if (name == QLatin1String("zoomOut"))
        current.zoomOut();
    else if (name == QLatin1String("zoomToFit"))
        current.zoomToFit();
    else if (name == QLatin1String("actualSize"))
        current.actualSize();
    else {
        EditorSession &editing = idleSession();
        const bool needsSelection = !QStringList{"undo", "redo", "selectAll", "deselect"}.contains(name);
        if (needsSelection && !editing.hasSelection())
            throw Error(AgentProtocol::invalidParams, QStringLiteral("Nothing is selected. Select something first."));
        if (name == QLatin1String("undo")) {
            if (!editing.canUndo())
                throw Error(AgentProtocol::invalidParams, QStringLiteral("There's nothing to undo."));
            editing.undo();
        } else if (name == QLatin1String("redo")) {
            if (!editing.canRedo())
                throw Error(AgentProtocol::invalidParams, QStringLiteral("There's nothing to redo."));
            editing.redo();
        } else if (name == QLatin1String("selectAll")) {
            editing.selectAll();
        } else if (name == QLatin1String("deselect")) {
            editing.deselectAll();
        } else if (name == QLatin1String("group")) {
            editing.groupSelection();
        } else if (name == QLatin1String("ungroup")) {
            editing.ungroupSelection();
        } else if (name == QLatin1String("delete")) {
            editing.deleteSelection();
        } else if (name == QLatin1String("duplicate")) {
            editing.duplicateSelection();
        } else if (name == QLatin1String("arrange")) {
            static const QStringList orders{"bringToFront", "bringForward", "sendBackward", "sendToBack"};
            editing.arrange(ArrangeOrder(*choice(params, QStringLiteral("order"), orders, true)));
        } else if (name == QLatin1String("align")) {
            static const QStringList edges{"left", "horizontalCenter", "right", "top", "verticalCenter", "bottom"};
            const int edge = *choice(params, QStringLiteral("edge"), edges, true);
            const bool artboard = choice(params, QStringLiteral("target"), {QStringLiteral("selection"), QStringLiteral("artboard")}).value_or(0) == 1;
            editing.align(AlignEdge(edge), artboard ? AlignTarget::artboard : AlignTarget::selection);
        } else if (name == QLatin1String("distribute")) {
            editing.distribute(*choice(params, QStringLiteral("axis"), {QStringLiteral("horizontal"), QStringLiteral("vertical")}, true) == 0
                                   ? DistributeAxis::horizontal
                                   : DistributeAxis::vertical);
        } else if (name == QLatin1String("strokeWidth")) {
            const auto width = number(params, QStringLiteral("width"));
            if (!width || *width < 0)
                fail(QStringLiteral("“width” must be a number, zero or more."));
            VectorDocument edited = *editing.document();
            for (const QUuid &id : editing.selectedLeaves()) {
                VectorObject *object = edited.find(id);
                if (object && object->hasPaint() && !edited.isEffectivelyLocked(id))
                    object->stroke.width = *width;
            }
            commit(editing, QStringLiteral("Stroke"), edited, editing.selection());
        } else if (name == QLatin1String("opacity")) {
            const auto value = number(params, QStringLiteral("value"));
            if (!value || *value < 0 || *value > 1)
                fail(QStringLiteral("“value” must be 0 to 1."));
            editing.setOpacityOfSelection(*value);
        }
    }
    return {{"done", name}, {"undo", current.undoName()}};
}
