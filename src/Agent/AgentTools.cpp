#include "Agent/AgentTools.h"
#include <QRegularExpression>
#include "Agent/AgentEdits.h"
#include "Agent/AgentParams.h"
#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "IO/DocumentExporter.h"
#include "IO/ImageImporter.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include "Rendering/VectorRenderer.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QTemporaryFile>
#include <cmath>

using AgentProtocol::Error;
using namespace AgentParams;

namespace {
// Large enough to read fine detail, small enough to hand an agent.
constexpr double maximumRenderSide = 8192;

// A temporary PNG the agent can open after this call returns.
QString temporaryPng(const QString &stem)
{
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("omastrator-%1-XXXXXX.png").arg(stem)));
    file.setAutoRemove(false);
    if (!file.open())
        throw Error(AgentProtocol::fileError, QStringLiteral("Could not create a temporary file: %1").arg(file.errorString()));
    return file.fileName();
}

void writePng(const QImage &image, const QString &path)
{
    if (image.isNull())
        throw Error(AgentProtocol::internalError, QStringLiteral("There was not enough memory to render."));
    if (!image.save(path, "PNG"))
        throw Error(AgentProtocol::fileError, QStringLiteral("Could not write “%1”.").arg(path));
}
}

AgentTools::AgentTools(AgentHost &host, QObject *parent) : QObject(parent), m_host(host) {}

QJsonObject AgentTools::call(const QString &method, const QJsonObject &params)
{
    using Handler = QJsonObject (AgentTools::*)(const QJsonObject &);
    static const std::vector<std::pair<QString, Handler>> handlers{
        {QStringLiteral("document_get"), &AgentTools::documentGet},
        {QStringLiteral("render"), &AgentTools::render},
        {QStringLiteral("insert_svg"), &AgentTools::insertSvg},
        {QStringLiteral("set_style"), &AgentTools::setStyle},
        {QStringLiteral("transform"), &AgentTools::transform},
        {QStringLiteral("arrange"), &AgentTools::arrange},
        {QStringLiteral("align"), &AgentTools::align},
        {QStringLiteral("distribute"), &AgentTools::distribute},
        {QStringLiteral("group"), &AgentTools::group},
        {QStringLiteral("ungroup"), &AgentTools::ungroup},
        {QStringLiteral("pathfinder"), &AgentTools::pathfinder},
        {QStringLiteral("delete"), &AgentTools::remove},
        {QStringLiteral("select"), &AgentTools::select},
        {QStringLiteral("update_object"), &AgentTools::updateObject},
        {QStringLiteral("replace_objects"), &AgentTools::replaceObjects},
        {QStringLiteral("proposal_finish"), &AgentTools::proposalFinish},
        {QStringLiteral("trace_image"), &AgentTools::traceImage},
        {QStringLiteral("open"), &AgentTools::open},
        {QStringLiteral("save"), &AgentTools::save},
        {QStringLiteral("export"), &AgentTools::exportFile},
        {QStringLiteral("place"), &AgentTools::place},
        {QStringLiteral("show_variations"), &AgentTools::showVariations},
        {QStringLiteral("show_roast"), &AgentTools::showRoast},
        {QStringLiteral("select_tool"), &AgentTools::selectTool},
        {QStringLiteral("apply_color"), &AgentTools::applyColor},
        {QStringLiteral("swatches_get"), &AgentTools::swatchesGet},
        {QStringLiteral("swatches_add"), &AgentTools::swatchesAdd},
        {QStringLiteral("open_capture"), &AgentTools::openCapture},
        {QStringLiteral("paste_svg"), &AgentTools::pasteSvg},
        {QStringLiteral("new_document"), &AgentTools::newDocument},
        {QStringLiteral("show_panel"), &AgentTools::showPanel},
        {QStringLiteral("ai_start"), &AgentTools::aiStart},
        {QStringLiteral("live"), &AgentTools::live},
        {QStringLiteral("live_deployed"), &AgentTools::liveDeployed},
        {QStringLiteral("command"), &AgentTools::command},
        {QStringLiteral("design"), &AgentTools::design},
        {QStringLiteral("show_window"), &AgentTools::showWindow},
        {QStringLiteral("quit_app"), &AgentTools::quitApp},
    };
    try {
        if (method == QLatin1String("selection_get"))
            return selectionGet();
        if (method == QLatin1String("status_get"))
            return status();
        // The island's canvas work shows the window when the app runs in the background with none on show.
        static const QStringList onCanvas{"select_tool", "apply_color", "open_capture", "paste_svg", "command"};
        for (const auto &[name, handler] : handlers) {
            if (name != method)
                continue;
            QJsonObject result = (this->*handler)(params);
            if (onCanvas.contains(method))
                m_host.showWindow({}, false);
            return result;
        }
    } catch (const FileError &failure) {
        throw Error(AgentProtocol::fileError, failure.message());
    } catch (const CodecError &failure) {
        throw Error(AgentProtocol::invalidParams, QStringLiteral("The object JSON is not valid: %1.").arg(QString::fromUtf8(failure.what())));
    }
    throw Error(AgentProtocol::methodNotFound, QStringLiteral("There is no method “%1”. Run `omastrator agent --help` for the list.").arg(method));
}

EditorSession &AgentTools::session()
{
    EditorSession *session = m_host.session();
    if (!session || !session->hasDocument())
        throw Error(AgentProtocol::noDocument, QStringLiteral("No document is open in Omastrator. Open one first, or call open."));
    return *session;
}

const VectorDocument &AgentTools::document()
{
    return *session().document();
}

bool AgentTools::ownsProposal(const EditorSession &session) const
{
    // The name rules out a user's drag begun after the proposal was kept, before it moved anything.
    return m_session == &session && session.isInteracting() && session.interactionName() == QStringLiteral("AI: ") + m_title && m_preview
        && session.document() && *session.document() == *m_preview;
}

bool AgentTools::hasProposal() const
{
    return m_session && ownsProposal(*m_session);
}

EditorSession *AgentTools::proposalSession() const
{
    return hasProposal() ? m_session.data() : nullptr;
}

QString AgentTools::proposalTitle() const
{
    return hasProposal() ? QStringLiteral("AI: ") + m_title : QString();
}

QJsonObject AgentTools::status()
{
    EditorSession *current = m_host.session();
    QJsonObject result{{"running", true},
                       {"document", current && current->hasDocument()},
                       {"tool", current ? rawValue(current->tool()) : QStringLiteral("select")},
                       {"proposal", proposalTitle()}};
    if (pendingCapture())
        result["offer"] = QStringLiteral("vectorize");
    const QJsonObject extras = m_host.statusExtras();
    for (auto it = extras.begin(); it != extras.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QJsonObject AgentTools::selectTool(const QJsonObject &params)
{
    static const QHash<QString, QString> aliases{{"move", "select"}, {"selection", "select"}, {"direct", "directSelect"},
                                                 {"directselection", "directSelect"}, {"type", "text"}, {"eyedrop", "eyedropper"},
                                                 {"roundedrect", "roundedRectangle"}, {"rect", "rectangle"}, {"shape builder", "shapeBuilder"}};
    const QString given = requiredString(params, QStringLiteral("tool")).trimmed();
    QString name = aliases.value(given.toLower(), given);
    // Case aside, "directselect" and "directSelect" are one tool.
    for (const Tool each : allTools) {
        if (rawValue(each).compare(name, Qt::CaseInsensitive) == 0)
            name = rawValue(each);
    }
    const std::optional<Tool> tool = toolNamed(name);
    if (!tool)
        fail(QStringLiteral("There is no tool “%1”. Use one of: select, directSelect, pen, pencil, text, line, rectangle, "
                            "roundedRectangle, ellipse, polygon, star, shapeBuilder, scissors, rotate, scale, gradient, eyedropper, hand, zoom.").arg(given));
    EditorSession *current = m_host.session();
    if (!current)
        throw Error(AgentProtocol::noDocument, QStringLiteral("Omastrator has no window open."));
    current->selectTool(*tool);
    return {{"tool", rawValue(current->tool())}};
}

VectorDocument AgentTools::draft()
{
    EditorSession &current = session();
    if (current.isInteracting() && !ownsProposal(current))
        throw Error(AgentProtocol::busy, QStringLiteral("The user is in the middle of an edit. Try again in a moment."));
    return *current.document();
}

void AgentTools::propose(const QString &title, const VectorDocument &document, const std::vector<QUuid> &selection)
{
    EditorSession &current = session();
    if (!ownsProposal(current)) {
        if (current.isInteracting())
            throw Error(AgentProtocol::busy, QStringLiteral("The user is in the middle of an edit. Try again in a moment."));
        current.beginInteraction(QStringLiteral("AI: ") + title);
        m_session = &current;
        m_title = title;
    }
    current.previewDocument(document, selection);
    m_preview = *current.document();
    emit proposalChanged();
}

std::vector<QUuid> AgentTools::targets(const QJsonObject &params, bool required)
{
    const VectorDocument &current = document();
    std::optional<std::vector<QUuid>> given = ids(params);
    if (!given && required)
        fail(QStringLiteral("“ids” is required."));
    std::vector<QUuid> result = given.value_or(session().selection());
    if (result.empty())
        fail(given ? QStringLiteral("“ids” is empty.") : QStringLiteral("Nothing is selected. Pass “ids”, or call select first."));
    for (const QUuid &id : result) {
        const VectorObject *object = current.find(id);
        if (!object)
            fail(QStringLiteral("No object has the id %1. Call document_get for the current ids.").arg(idString(id)));
        if (object->kind == ObjectKind::layer)
            fail(QStringLiteral("%1 is a layer; name the objects in it instead.").arg(idString(id)));
    }
    return result;
}

std::vector<QUuid> AgentTools::unlocked(const VectorDocument &document, const std::vector<QUuid> &ids) const
{
    std::vector<QUuid> result;
    for (const QUuid &id : ids) {
        if (!document.isEffectivelyLocked(id))
            result.push_back(id);
    }
    if (result.empty())
        throw Error(AgentProtocol::invalidParams, QStringLiteral("Those objects are locked. The user can unlock them in the Layers panel."));
    return result;
}

QJsonObject AgentTools::documentGet(const QJsonObject &params)
{
    const bool images = boolean(params, QStringLiteral("includeImages"), false);
    EditorSession &current = session();
    QJsonObject json = DocumentCodec::encode(*current.document());
    if (!images) {
        QJsonArray objects = json["objects"].toArray();
        for (qsizetype index = 0; index < objects.size(); ++index) {
            QJsonObject object = objects[index].toObject();
            if (object["kind"].toString() != rawValue(ObjectKind::image))
                continue;
            const VectorObject *image = current.document()->find(QUuid::fromString(object["id"].toString()));
            object["image"] = QStringLiteral("(omitted: pass includeImages to get the PNG)");
            if (image)
                object["imageSize"] = QJsonArray{image->image.width(), image->image.height()};
            objects[index] = object;
        }
        json["objects"] = objects;
    }
    json["selection"] = idArray(current.selection());
    if (const auto layer = current.activeLayer())
        json["activeLayer"] = idString(*layer);
    json["proposal"] = QJsonObject{{"open", hasProposal()}, {"title", proposalTitle()}};
    return json;
}

QJsonObject AgentTools::selectionGet()
{
    EditorSession &current = session();
    std::vector<VectorObject> objects;
    for (const QUuid &id : AgentEdits::inOrder(*current.document(), current.selection())) {
        objects.push_back(*current.document()->find(id));
        for (const QUuid &nested : current.document()->descendants(id))
            objects.push_back(*current.document()->find(nested));
    }
    return {{"selection", idArray(current.selection())}, {"objects", DocumentCodec::encode(objects)},
            {"bounds", current.hasSelection() ? QJsonValue(rect(current.selectionBounds(true))) : QJsonValue(QJsonValue::Null)}};
}

QJsonObject AgentTools::render(const QJsonObject &params)
{
    const double scale = number(params, QStringLiteral("scale")).value_or(1);
    if (!(scale > 0))
        fail(QStringLiteral("“scale” must be above zero."));
    const bool selectionOnly = boolean(params, QStringLiteral("selectionOnly"), false);
    const QString path = string(params, QStringLiteral("path")).value_or(QString());
    EditorSession &current = session();
    VectorDocument copy = *current.document();
    QRectF area(QPointF(0, 0), copy.size);
    if (selectionOnly) {
        if (!current.hasSelection())
            fail(QStringLiteral("Nothing is selected to render. Leave out “selectionOnly” for the artboard."));
        // A little margin so strokes and antialiasing aren't clipped.
        area = current.selectionBounds(true).adjusted(-2, -2, 2, 2);
        for (const QUuid &layer : copy.layers())
            copy.transform(layer, QTransform::fromTranslate(-area.x(), -area.y()));
        copy.size = QSizeF(std::max(1.0, area.width()), std::max(1.0, area.height()));
        copy.artboards.clear();
        copy.exportAssets.clear();
    }
    if (copy.size.width() * scale > maximumRenderSide || copy.size.height() * scale > maximumRenderSide)
        fail(QStringLiteral("%1 × %2 pixels is too large; use a smaller “scale”.")
                 .arg(std::ceil(copy.size.width() * scale)).arg(std::ceil(copy.size.height() * scale)));
    const QImage image = VectorRenderer::render(copy, scale, false);
    const QString target = path.isEmpty() ? temporaryPng(QStringLiteral("render")) : path;
    writePng(image, target);
    return {{"path", QFileInfo(target).absoluteFilePath()}, {"width", image.width()}, {"height", image.height()},
            {"area", rect(area)}, {"scale", scale}};
}

QJsonObject AgentTools::proposalFinish(const QJsonObject &params)
{
    const QString title = string(params, QStringLiteral("title")).value_or(QString()).trimmed();
    const QString summary = string(params, QStringLiteral("summary")).value_or(QString()).trimmed();
    const bool open = hasProposal();
    if (open && !title.isEmpty()) {
        m_title = title.startsWith(QLatin1String("AI: ")) ? title.mid(4) : title;
        // Renames the open interaction: undo it, reopen it named, show it again.
        const VectorDocument preview = *m_preview;
        const std::vector<QUuid> selection = m_session->selection();
        m_session->cancelInteraction();
        m_session->beginInteraction(QStringLiteral("AI: ") + m_title);
        m_session->previewDocument(preview, selection);
        m_preview = *m_session->document();
        emit proposalChanged();
    }
    const QString shown = open ? m_title : title;
    m_host.proposalFinished(shown, summary);
    return {{"proposal", open}, {"title", open ? proposalTitle() : QString()},
            {"note", open ? QStringLiteral("The user now presses Enter to keep it or Esc to discard it.")
                          : QStringLiteral("There was no proposal open, so the user has nothing to accept.")}};
}

QJsonObject AgentTools::open(const QJsonObject &params)
{
    const QString path = requiredString(params, QStringLiteral("path"));
    const QString failure = m_host.openFile(path);
    if (!failure.isEmpty())
        throw Error(AgentProtocol::fileError, failure);
    return {{"opened", QFileInfo(path).absoluteFilePath()}};
}

QJsonObject AgentTools::save(const QJsonObject &params)
{
    const QString path = string(params, QStringLiteral("path")).value_or(QString());
    session();
    if (hasProposal())
        throw Error(AgentProtocol::busy, QStringLiteral("A proposal is waiting for the user. Saving has to wait until they keep or discard it."));
    const QString failure = m_host.saveFile(path);
    if (!failure.isEmpty())
        throw Error(AgentProtocol::fileError, failure);
    return {{"saved", path.isEmpty() ? QJsonValue(true) : QJsonValue(QFileInfo(path).absoluteFilePath())}};
}

QJsonObject AgentTools::exportFile(const QJsonObject &params)
{
    const QString path = requiredString(params, QStringLiteral("path"));
    const auto chosen = choice(params, QStringLiteral("format"), {QStringLiteral("pdf"), QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("svg")});
    const DocumentExporter::Format format = chosen ? DocumentExporter::Format(*chosen) : DocumentExporter::format(path);
    const double scale = number(params, QStringLiteral("scale")).value_or(1);
    const int quality = int(number(params, QStringLiteral("quality")).value_or(90));
    const bool transparent = boolean(params, QStringLiteral("transparent"), false);
    if (!(scale > 0))
        fail(QStringLiteral("“scale” must be above zero."));
    if (quality < 0 || quality > 100)
        fail(QStringLiteral("“quality” must be 0 to 100."));
    const VectorDocument &current = document();
    switch (format) {
    case DocumentExporter::Format::pdf:
        DocumentExporter::writePdf(current, path);
        break;
    case DocumentExporter::Format::png:
        DocumentExporter::writePng(current, path, scale, transparent);
        break;
    case DocumentExporter::Format::jpeg:
        DocumentExporter::writeJpeg(current, path, scale, quality);
        break;
    case DocumentExporter::Format::svg:
        SvgExporter::write(current, path);
        break;
    }
    static const char *names[] = {"pdf", "png", "jpeg", "svg"};
    return {{"path", QFileInfo(path).absoluteFilePath()}, {"format", names[int(format)]}, {"includesProposal", hasProposal()}};
}

QJsonObject AgentTools::place(const QJsonObject &params)
{
    const QString path = requiredString(params, QStringLiteral("path"));
    VectorDocument edited = draft();
    const auto layer = AgentEdits::openLayer(edited, session().activeLayer());
    if (!layer)
        throw Error(AgentProtocol::busy, QStringLiteral("Every layer is locked or hidden, so there is nowhere to place it."));
    const QString name = QFileInfo(path).fileName();
    QUuid id;
    if (ImageImporter::isVector(path)) {
        id = AgentEdits::insertArt(edited, SvgImporter::read(path), name, *layer);
        const QRectF bounds = edited.bounds(id);
        const QPointF shift = QPointF(edited.size.width() / 2, edited.size.height() / 2) - bounds.center();
        edited.transform(id, QTransform::fromTranslate(shift.x(), shift.y()));
    } else {
        const QImage image = ImageImporter::read(path);
        VectorObject object;
        object.kind = ObjectKind::image;
        object.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        object.name = name;
        // Larger than the artboard: fit it, as Place does.
        const QSizeF size = image.size();
        const double scale = std::min({1.0, edited.size.width() / size.width(), edited.size.height() / size.height()});
        object.transform = QTransform::fromScale(scale, scale)
            * QTransform::fromTranslate((edited.size.width() - size.width() * scale) / 2, (edited.size.height() - size.height() * scale) / 2);
        id = object.id;
        edited.insert(std::move(object), *layer);
    }
    propose(QStringLiteral("Place"), edited, {id});
    return {{"id", idString(id)}, {"bounds", rect(edited.bounds(id))}};
}

QJsonObject AgentTools::showVariations(const QJsonObject &params)
{
    const QString requestId = requiredString(params, QStringLiteral("requestId"));
    const QJsonValue value = params["variations"];
    if (!value.isArray() || value.toArray().isEmpty())
        fail(QStringLiteral("“variations” must be a non-empty array of {name, svg, note?}."));
    std::vector<AgentVariation> variations;
    for (const QJsonValue &each : value.toArray()) {
        const QJsonObject object = each.toObject();
        const int index = int(variations.size()) + 1;
        if (!each.isObject())
            fail(QStringLiteral("Variation %1 must be an object {name, svg, note?}.").arg(index));
        AgentVariation variation;
        variation.name = string(object, QStringLiteral("name")).value_or(QStringLiteral("Variation %1").arg(index));
        variation.svg = requiredString(object, QStringLiteral("svg"));
        variation.note = string(object, QStringLiteral("note")).value_or(QString());
        // A broken SVG is the agent's to fix, before the user sees an empty thumbnail.
        try {
            const VectorDocument parsed = SvgImporter::parse(variation.svg.toUtf8());
            if (parsed.objects.size() <= parsed.layers().size())
                fail(QStringLiteral("Variation %1 (“%2”) has no shapes.").arg(index).arg(variation.name));
        } catch (const FileError &failure) {
            fail(QStringLiteral("Variation %1 (“%2”): %3").arg(index).arg(variation.name, failure.message()));
        }
        variations.push_back(variation);
    }
    m_host.showVariations(requestId, variations);
    return {{"shown", int(variations.size())}, {"requestId", requestId}};
}

QJsonObject AgentTools::showRoast(const QJsonObject &params)
{
    AgentRoast roast;
    roast.requestId = requiredString(params, QStringLiteral("requestId"));
    roast.roast = requiredString(params, QStringLiteral("roast")).trimmed();
    // The panel pages through short parts; a speech doesn't fit one.
    const qsizetype words = roast.roast.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    if (words > 70 || roast.roast.count(QLatin1Char('\n')) > 4)
        fail(QStringLiteral("The roast is %1 words. Keep it to 2–4 one-line burns, 60 words at most, and call again.").arg(words));
    roast.suggestedPrompt = requiredString(params, QStringLiteral("suggestedPrompt"));
    const QJsonValue value = params["feedback"];
    if (!value.isArray() || value.toArray().isEmpty())
        fail(QStringLiteral("“feedback” must be a non-empty array of {title, detail, objectIds?}."));
    if (value.toArray().size() > 4)
        fail(QStringLiteral("%1 fixes is too many. Send the 3 with the most impact.").arg(value.toArray().size()));
    for (const QJsonValue &each : value.toArray()) {
        if (!each.isObject())
            fail(QStringLiteral("Each feedback item must be an object {title, detail, objectIds?}."));
        const QJsonObject object = each.toObject();
        AgentFeedback feedback;
        feedback.title = requiredString(object, QStringLiteral("title"));
        feedback.detail = requiredString(object, QStringLiteral("detail"));
        feedback.objectIds = ids(object, QStringLiteral("objectIds")).value_or(std::vector<QUuid>{});
        roast.feedback.push_back(feedback);
    }
    m_host.showRoast(roast);
    return {{"shown", true}, {"requestId", roast.requestId}};
}
