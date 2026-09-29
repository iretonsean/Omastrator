#include "UI/ShareController.h"
#include "Cloud/CloudStorage.h"
#include "IO/FileError.h"
#include "Live/History.h"
#include "Live/WriteBack.h"
#include "UI/AgentBridge.h"
#include "UI/ProjectWorkspace.h"
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
// The toast stays this long unless it's a failure, which waits to be read.
constexpr int noticeMs = 10'000;

// One of these may follow a plain failure, each at most once per install (docs/HUMOR.md).
const QStringList dryLines{
    QStringLiteral("On the bright side, nobody has asked for the logo bigger yet."),
    QStringLiteral("The client will have to imagine it for now. They were going to anyway."),
};

QString count(size_t number)
{
    return number == 1 ? QStringLiteral("1 object") : QStringLiteral("%1 objects").arg(number);
}
}

ShareController::ShareController(ProjectWorkspace &workspace, AgentBridge *agent, QWidget &window)
    : QObject(&window), m_workspace(workspace), m_agent(agent), m_window(window), m_job(workspace.cloud())
{
    connect(&m_job, &ShareJob::stageChanged, this, [this] {
        if (m_job.running())
            setNotice(Notice::Kind::progress, m_job.stageText(), m_jobDetail);
    });
    connect(&m_job, &ShareJob::finished, this, &ShareController::finished);
    connect(&m_workspace, &ProjectWorkspace::changed, this, &ShareController::followRenames);
    connect(&m_workspace.cloud(), &CloudStorage::remotesChanged, this, &ShareController::changed);
    connectDevice();
    followRenames();
}

QString ShareController::documentKey(const ProjectTab &tab)
{
    if (tab.cloud)
        return tab.cloud->location.toString();
    if (tab.path)
        return QFileInfo(*tab.path).absoluteFilePath();
    return QStringLiteral("untitled:") + tab.id.toString(QUuid::WithoutBraces);
}

QString ShareController::documentKey() const
{
    return documentKey(m_workspace.current());
}

void ShareController::followRenames()
{
    // A document saved for the first time keeps the shares it made while untitled.
    for (const auto &tab : m_workspace.tabs()) {
        const QString key = documentKey(*tab);
        const auto known = m_keys.find(tab->id);
        if (known != m_keys.end() && known->second != key && known->second.startsWith(QLatin1String("untitled:")))
            Share::rename(known->second, key);
        m_keys[tab->id] = key;
    }
}

bool ShareController::hasDocument() const
{
    return m_workspace.current().session.hasDocument();
}

bool ShareController::sharesSelection() const
{
    const EditorSession &session = m_workspace.current().session;
    return session.hasDocument() && session.hasSelection() && !session.document()->bounds(session.selection(), true).isEmpty();
}

QString ShareController::scopeText() const
{
    if (sharesSelection())
        return QStringLiteral("the selection (%1)").arg(count(m_workspace.current().session.selection().size()));
    const EditorSession &session = m_workspace.current().session;
    if (const std::optional<VectorDocument> &document = session.document(); document && document->artboardCount() > 1)
        return QStringLiteral("the artboard “%1”").arg(document->artboard(session.activeArtboard()).name);
    return QStringLiteral("the artboard");
}

Share::DocumentShares ShareController::shares() const
{
    return Share::load(documentKey());
}

QString ShareController::liveProject() const
{
    return m_agent ? m_agent->deployProject() : QString();
}

bool ShareController::liveRunning() const
{
    return m_agent && m_agent->liveSession().state() == LiveSession::State::running && !liveProject().isEmpty();
}

QString ShareController::liveKey() const
{
    const QString project = liveProject();
    return project.isEmpty() ? QString() : QStringLiteral("live:") + project;
}

std::vector<ShareController::Destination> ShareController::destinations() const
{
    std::vector<Destination> found;
    // A running Live session shares the site first.
    const bool live = liveRunning();
    if (live)
        found.push_back({Share::live, QStringLiteral("Live preview deploy")});
    CloudStorage &cloud = m_workspace.cloud();
    for (const CloudRemote &remote : cloud.remotes()) {
        if (CloudStorage::makesLinks(remote.type))
            found.push_back({Share::cloudDestination(remote.name), cloud.serviceName(remote.name)});
    }
    if (!QStandardPaths::findExecutable(GitHub::program()).isEmpty())
        found.push_back({Share::github, QStringLiteral("GitHub")});
    if (!live && !liveProject().isEmpty())
        found.push_back({Share::live, QStringLiteral("Live preview deploy")});
    return found;
}

QString ShareController::chosenDestination() const
{
    const std::vector<Destination> available = destinations();
    const QString remembered = shares().destination;
    for (const Destination &each : available) {
        if (each.id == remembered)
            return each.id;
    }
    return available.empty() ? QString() : available.front().id;
}

QString ShareController::destinationLabel(const QString &id) const
{
    for (const Destination &each : destinations()) {
        if (each.id == id)
            return each.label;
    }
    return id;
}

Share::Format ShareController::chosenFormat() const
{
    return shares().format.value_or(Share::Format::png);
}

void ShareController::setNotice(Notice::Kind kind, const QString &text, const QString &detail, const QString &link)
{
    m_notice = {kind, text, detail, link, kind == Notice::Kind::failed ? m_job.log() : QString()};
    const int number = ++m_noticeNumber;
    if (kind == Notice::Kind::shared) {
        QTimer::singleShot(noticeMs, this, [this, number] {
            if (number == m_noticeNumber)
                dismissNotice();
        });
    }
    emit changed();
}

void ShareController::setFailure(const QString &text)
{
    setNotice(Notice::Kind::failed, text);
}

void ShareController::dismissNotice()
{
    ++m_noticeNumber;
    m_notice = {};
    emit changed();
}

void ShareController::remember(const Options &options)
{
    // A choice made in the popover sticks to the document.
    if (!options.format && options.destination.isEmpty())
        return;
    Share::DocumentShares remembered = shares();
    if (options.format)
        remembered.format = options.format;
    if (!options.destination.isEmpty())
        remembered.destination = options.destination;
    Share::save(documentKey(), remembered);
    emit changed();
}

QString ShareController::share(const Options &options)
{
    if (running())
        return QStringLiteral("A share is already under way.");
    remember(options);
    // rclone's remotes may not have been listed yet; list them once, then decide.
    CloudStorage &cloud = m_workspace.cloud();
    if (!m_listedRemotes && cloud.remotes().isEmpty() && CloudStorage::isInstalled()) {
        m_listedRemotes = true;
        setNotice(Notice::Kind::progress, QStringLiteral("Looking for somewhere to share…"));
        cloud.refreshRemotes([this, options] {
            if (const QString failure = start(options); !failure.isEmpty())
                setNotice(Notice::Kind::failed, failure);
        });
        return {};
    }
    m_listedRemotes = true;
    return start(options);
}

QString ShareController::start(const Options &options)
{
    const QString destination = options.destination.isEmpty() ? chosenDestination() : options.destination;
    if (destination == Share::live)
        return shareLive();
    if (!hasDocument())
        return QStringLiteral("Open a document to share.");
    if (destination.isEmpty()) {
        setNotice(Notice::Kind::connect, QStringLiteral("There's nowhere to share to yet. Connect a cloud service or GitHub, then press Share again."));
        return {};
    }
    const QString remote = Share::remoteOf(destination);
    if (!remote.isEmpty() && !m_workspace.cloud().remote(remote))
        return QStringLiteral("%1 isn't connected any more.").arg(remote);
    if (destination == Share::github && !Share::githubConfirmed()) {
        m_pending = options;
        m_pending->destination = destination;
        emit githubQuestion();
        emit changed();
        return {};
    }

    const ProjectTab &tab = m_workspace.current();
    const EditorSession &session = tab.session;
    const Share::Format format = options.format.value_or(chosenFormat());
    const bool selection = sharesSelection();
    QString file;
    if (const QString failure = render(format, QStringLiteral("%1-%2").arg(Share::safeName(tab.title()), Share::stamp(QDateTime::currentDateTime())), &file);
        !failure.isEmpty())
        return failure;
    ShareJob::Request request;
    request.file = file;
    request.format = format;
    request.documentName = tab.title();
    request.destination = destination;
    request.where = remote.isEmpty() ? QStringLiteral("GitHub") : m_workspace.cloud().serviceName(remote);
    request.scope = selection ? QStringLiteral("selection") : QStringLiteral("artboard");
    for (const QUuid &id : session.selection()) {
        if (selection)
            request.objects << id.toString(QUuid::WithoutBraces);
    }
    m_jobKey = documentKey();
    m_unsharing.clear();
    QString scope = scopeText();
    scope[0] = scope[0].toUpper();
    m_jobDetail = QStringLiteral("%1 as %2.").arg(scope, Share::label(format));
    m_job.start(request);
    return {};
}

QString ShareController::render(Share::Format format, const QString &baseName, QString *file)
{
    const ProjectTab &tab = m_workspace.current();
    const EditorSession &session = tab.session;
    if (!sharesSelection() && !session.document()->artboard(session.activeArtboard()).exported)
        return QStringLiteral("“%1” is set not to export. Turn it on in Properties ▸ Document, or pick another artboard.")
            .arg(session.document()->artboard(session.activeArtboard()).name);
    const VectorDocument document = sharesSelection() ? Share::selectionDocument(*session.document(), session.selection())
        : session.document()->artboards.empty() ? *session.document() : session.document()->artboardDocument(session.activeArtboard());
    m_folder = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("omastrator-share-XXXXXX")));
    *file = m_folder->filePath(QStringLiteral("%1.%2").arg(baseName, Share::suffix(format)));
    try {
        Share::write(document, format, *file);
    } catch (const FileError &error) {
        m_folder.reset();
        return error.message();
    }
    return {};
}

void ShareController::answerGitHub(bool yes)
{
    if (!m_pending)
        return;
    const Options options = *m_pending;
    m_pending.reset();
    if (!yes) {
        emit changed();
        return;
    }
    Share::setGithubConfirmed(true);
    if (const QString failure = start(options); !failure.isEmpty())
        setNotice(Notice::Kind::failed, failure);
}

QString ShareController::shareLive(bool fresh)
{
    if (running())
        return QStringLiteral("A share is already under way.");
    const QString folder = liveProject();
    if (folder.isEmpty())
        return QStringLiteral("Open a project in Live to share its site.");
    m_jobKey = liveKey();
    m_unsharing.clear();
    m_jobDetail = QStringLiteral("A preview of %1.").arg(QFileInfo(folder).fileName());
    const std::optional<Deploy::Record> latest = Deploy::latest(folder);
    const QString head = WriteBack::git(folder, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).trimmed();
    const bool unsaved = m_agent && m_agent->unsavedFiles() > 0;
    if (!fresh && latest && !head.isEmpty() && latest->commit == head && !unsaved) {
        Share::Record record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.link = latest->url;
        record.time = QDateTime::currentDateTime();
        record.kind = Share::live;
        record.where = latest->preview ? QStringLiteral("Preview deploy") : QStringLiteral("Live site");
        record.format = QStringLiteral("site");
        record.scope = QStringLiteral("site");
        m_jobDetail = QStringLiteral("The latest deploy of %1.").arg(QFileInfo(folder).fileName());
        addRecord(m_jobKey, record);
        noticeShared(record);
        return {};
    }
    m_job.startPreview(folder);
    return {};
}

void ShareController::addRecord(const QString &key, const Share::Record &record)
{
    Share::DocumentShares shares = Share::load(key);
    shares.shared.push_back(record);
    Share::save(key, shares);
}

void ShareController::noticeShared(const Share::Record &record)
{
    copyLink(record.link);
    setNotice(Notice::Kind::shared, QStringLiteral("Link copied — %1").arg(record.where), m_jobDetail, record.link);
}

void ShareController::finished(bool ok)
{
    m_folder.reset();
    if (!m_unsharing.isEmpty()) {
        const QString id = m_unsharing;
        m_unsharing.clear();
        if (!ok) {
            setNotice(Notice::Kind::failed, m_job.failure());
            return;
        }
        Share::DocumentShares shares = Share::load(m_jobKey);
        std::erase_if(shares.shared, [&id](const Share::Record &record) { return record.id == id; });
        Share::save(m_jobKey, shares);
        setNotice(Notice::Kind::shared, m_job.record().canUnshare() ? QStringLiteral("Unshared. The link no longer works.")
                                                                    : QStringLiteral("Removed from the list. The deploy itself stays up."));
        return;
    }
    if (!ok) {
        if (m_job.needsConnection()) {
            setNotice(Notice::Kind::connect, m_job.failure() + QStringLiteral(" Connect GitHub or a cloud service, then press Share again."));
            return;
        }
        QString text = m_job.failure();
        if (text != QLatin1String("Cancelled."))
            if (const QString dry = Deploy::dryLine(dryLines); !dry.isEmpty())
                text += QLatin1Char(' ') + dry;
        setNotice(Notice::Kind::failed, text);
        return;
    }
    addRecord(m_jobKey, m_job.record());
    noticeShared(m_job.record());
}

std::vector<Share::Record> ShareController::sharedList() const
{
    std::vector<Share::Record> list = shares().shared;
    if (const QString key = liveKey(); !key.isEmpty()) {
        const std::vector<Share::Record> site = Share::load(key).shared;
        list.insert(list.end(), site.begin(), site.end());
    }
    // Newest first; the list is kept oldest first, so ties keep the later share on top.
    std::reverse(list.begin(), list.end());
    std::stable_sort(list.begin(), list.end(), [](const Share::Record &a, const Share::Record &b) { return a.time > b.time; });
    return list;
}

QString ShareController::unshare(const QString &recordId)
{
    if (running())
        return QStringLiteral("Wait for the share under way to finish.");
    for (const QString &key : {documentKey(), liveKey()}) {
        if (key.isEmpty())
            continue;
        for (const Share::Record &record : Share::load(key).shared) {
            if (record.id != recordId)
                continue;
            m_jobKey = key;
            m_unsharing = recordId;
            m_jobDetail.clear();
            m_job.unshare(record);
            return {};
        }
    }
    return QStringLiteral("That share isn't in the list any more.");
}

QString ShareController::pasteFeedback(const QString &recordId, const QString &feedback)
{
    if (!m_agent)
        return QStringLiteral("AI isn't available in this window.");
    if (feedback.trimmed().isEmpty())
        return QStringLiteral("Paste what the client said first.");
    std::optional<Share::Record> shared;
    for (const Share::Record &record : shares().shared) {
        if (record.id == recordId)
            shared = record;
    }
    if (!shared)
        return QStringLiteral("That share isn't in this document's list.");
    EditorSession &session = m_workspace.current().session;
    if (!session.hasDocument())
        return QStringLiteral("Open the document that was shared.");
    // The edit applies to what the client saw: the shared selection if it's still there, else the whole artboard.
    std::vector<QUuid> objects;
    for (const QString &id : shared->objects) {
        const QUuid uuid = QUuid::fromString(id);
        if (session.document()->find(uuid))
            objects.push_back(uuid);
    }
    if (objects.empty())
        session.deselectAll();
    else
        session.select(objects);
    return m_agent->editWithInstruction(
        QStringLiteral("The client replied to the design shared with them. Make the changes their feedback asks for, and nothing else.\n\n"
                       "Client feedback:\n%1")
            .arg(feedback.trimmed()));
}

QString ShareController::copyLink(const QString &link)
{
    if (link.isEmpty())
        return QStringLiteral("There's no link to copy.");
    QGuiApplication::clipboard()->setText(link);
    return {};
}

QString ShareController::openLink(const QString &link)
{
    if (link.isEmpty())
        return QStringLiteral("There's no link to open.");
    const QString opener = qEnvironmentVariable("OMASTRATOR_XDG_OPEN", QStringLiteral("xdg-open"));
    if (!QProcess::startDetached(opener, {link}))
        return QStringLiteral("Couldn't open the link. It's on the clipboard.");
    return {};
}

QString ShareController::connectCloud()
{
    m_workspace.connectCloud();
    return {};
}

QString ShareController::connectGitHub()
{
    return m_agent ? m_agent->connectGitHub() : GitHub::connect();
}
