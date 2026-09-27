#include "Agent/Setup.h"
#include "IO/DocumentExporter.h"
#include "IO/FileError.h"
#include "IO/SvgExporter.h"
#include "UI/AgentBridge.h"
#include "UI/DesignController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

// Change the real thing, widened (docs/ANYWHERE.md): a site that isn't yours
// keeps its edits as named sets on this machine, and Hand to Agent packages a
// mockup from any surface for the agent to build in the app's source.
namespace {
QString writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Couldn't write %1: %2").arg(path, file.errorString());
    return {};
}

// Where each lifted shape came from: one entry per element or widget, where it sits in the mockup.
QJsonArray liftedSelectors(const VectorDocument &art)
{
    QJsonArray list;
    for (const VectorObject &object : art.objects) {
        if (object.liftedFrom.isEmpty() || object.liftedFrom == QLatin1String("trace"))
            continue;
        const VectorObject *parent = object.parentID ? art.find(*object.parentID) : nullptr;
        if (parent && parent->liftedFrom == object.liftedFrom)
            continue;
        const QRectF bounds = art.bounds(object.id);
        list.append(QJsonObject{{"name", object.name},
                                {"selector", object.liftedFrom},
                                {"bounds", QJsonArray{std::round(bounds.x()), std::round(bounds.y()), std::round(bounds.width()), std::round(bounds.height())}}});
        if (list.size() >= 400)
            break;
    }
    return list;
}

bool hasArt(const VectorDocument &art)
{
    for (const QUuid &layer : art.layers())
        if (!art.children(layer).empty())
            return true;
    return false;
}
}

QString AgentBridge::handOff(const HandOff &given, QString *requestIdOut)
{
    if (given.folder.isEmpty() || !QFileInfo(given.folder).isDir())
        return QStringLiteral("Choose the folder with the app's source.");
    const bool art = given.art && hasArt(*given.art);
    if (!art && given.picture.isEmpty() && given.screenshot.isEmpty() && given.edits.empty())
        return QStringLiteral("There's nothing to hand over yet. Draw, lift or edit something first.");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty())
        return error;
    const QString project = QFileInfo(given.folder).canonicalFilePath();
    AgentWork work{project, {}, {}, QUuid::createUuid().toString(QUuid::WithoutBraces), true};
    if (const QString failure = work.prepare(); !failure.isEmpty())
        return failure;
    // Everything the agent reads, in one folder of its own.
    const QDir package(QDir::temp().filePath(QStringLiteral("omastrator-handoff-%1").arg(work.requestId)));
    QDir().mkpath(package.path());
    AgentWork::Package brief;
    brief.instruction = given.instruction;
    brief.source = given.source;
    brief.url = given.url;
    brief.screenshot = given.screenshot;
    brief.original = given.original;
    brief.command = Setup::shellQuote(QCoreApplication::applicationFilePath());
    auto failed = [&](const QString &message) {
        work.cleanup();
        return message;
    };
    if (art) {
        brief.png = package.filePath(QStringLiteral("mockup.png"));
        brief.svg = package.filePath(QStringLiteral("mockup.svg"));
        try {
            DocumentExporter::writePng(*given.art, brief.png, 2);
            SvgExporter::write(*given.art, brief.svg);
        } catch (const FileError &failure) {
            return failed(QStringLiteral("Couldn't write the mockup: %1").arg(failure.message()));
        }
        const QJsonArray selectors = liftedSelectors(*given.art);
        if (!selectors.isEmpty()) {
            brief.selectors = package.filePath(QStringLiteral("selectors.json"));
            if (const QString failure = writeText(brief.selectors, QString::fromUtf8(QJsonDocument(selectors).toJson(QJsonDocument::Indented)));
                !failure.isEmpty())
                return failed(failure);
        }
    } else {
        brief.png = given.picture.isEmpty() ? given.screenshot : given.picture;
    }
    if (!given.edits.empty()) {
        brief.css = package.filePath(QStringLiteral("edits.css"));
        if (const QString failure = writeText(brief.css, EditSets::css(given.origin, QStringLiteral("Edits for Hand to Agent"), given.edits, false));
            !failure.isEmpty())
            return failed(failure);
        brief.diff = EditSets::diff(given.edits);
    }
    if (brief.png.isEmpty())
        brief.png = brief.css;
    error = launchProject(work.requestId, work.worktree, QStringLiteral("handoff"), work.handoffPrompt(brief));
    if (!error.isEmpty())
        return failed(error);
    m_liveJobs[work.requestId] = work;
    m_lastProject = project;
    m_waiting = Waiting{work.requestId, Task::live, agent};
    m_liveMessage.clear();
    m_liveLog.clear();
    if (requestIdOut)
        *requestIdOut = work.requestId;
    emit waitingChanged();
    emit liveReviewChanged();
    return {};
}

QString AgentBridge::siteAction(const QString &action, const QJsonObject &params, QJsonObject &result)
{
    LiveSession &live = m_live;
    if (live.state() != LiveSession::State::running)
        return QStringLiteral("Open the site in Omastrator's browser first.");
    if (!live.isMockup())
        return QStringLiteral("This is your site: its edits go to the code with Deploy.");
    const QString name = params["name"].toString().trimmed();
    if (action == QLatin1String("list")) {
        QJsonArray sets;
        for (const EditSets::Set &set : live.editSets())
            sets.append(set.summary());
        result = {{"origin", live.origin()}, {"sets", sets}, {"pending", int(live.edits().size())},
                  {"notice", QStringLiteral("Not your site: changes stay on this machine.")}};
        return {};
    }
    if (action == QLatin1String("keep")) {
        QString kept;
        const QString failure = live.keepEdits(name, &kept);
        result["name"] = kept;
        return failure;
    }
    if (action == QLatin1String("toggle"))
        return live.setEditSetEnabled(name, params["on"].toBool(true));
    if (action == QLatin1String("remove"))
        return live.removeEditSet(name);
    if (action == QLatin1String("original"))
        return live.showOriginal(params["on"].toBool(true));
    if (action == QLatin1String("export")) {
        const std::vector<EditSets::Edit> edits = live.editsShown(params["set"].toString());
        if (edits.empty())
            return QStringLiteral("There are no edits on this page to export.");
        const QString host = live.url().host().isEmpty() ? QStringLiteral("page") : live.url().host();
        QString path = params["path"].toString();
        if (path.isEmpty()) {
            // Asked from the page: the window comes forward with a save dialog.
            bringForward();
            const QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
            path = QFileDialog::getSaveFileName(&m_window, QStringLiteral("Export CSS"),
                                                QDir(folder.isEmpty() ? QDir::homePath() : folder).filePath(host + QStringLiteral(".user.css")),
                                                QStringLiteral("Userstyle (*.user.css);;CSS (*.css)"));
            if (path.isEmpty())
                return {};
        }
        const bool userstyle = params["format"].toString() == QLatin1String("userstyle")
                               || (params["format"].toString().isEmpty() && path.endsWith(QLatin1String(".user.css")));
        const QString title = params["set"].toString().isEmpty() ? QStringLiteral("Edits on %1").arg(host) : params["set"].toString();
        if (const QString failure = writeText(path, EditSets::css(live.origin(), title, edits, userstyle)); !failure.isEmpty())
            return failure;
        result["path"] = path;
        result["edits"] = int(edits.size());
        live.notice(QStringLiteral("Exported %1 edits to %2.").arg(edits.size()).arg(QFileInfo(path).fileName()));
        return {};
    }
    if (action == QLatin1String("beforeAfter"))
        return m_design->beforeAfter(result);
    if (action == QLatin1String("handoff"))
        return m_design->handOffPage(params, result);
    return QStringLiteral("There is no action “%1” for a site that isn't yours.").arg(action);
}
