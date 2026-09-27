#include "UI/ShareJob.h"
#include "Cloud/CloudStorage.h"
#include "Live/History.h"
#include "Live/WriteBack.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {
// Uploads over a slow connection take a while; gh gets this long per command.
constexpr int ghTimeoutMs = 5 * 60 * 1000;

QString oneLine(const QString &output)
{
    return CloudStorage::scrub(Deploy::lastLine(output));
}
}

const QString ShareJob::repositoryName = QStringLiteral("omastrator-shares");

ShareJob::ShareJob(CloudStorage &cloud, QObject *parent) : QObject(parent), m_cloud(cloud) {}

ShareJob::~ShareJob()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (auto *job = qobject_cast<CloudJob *>(m_cloudJob.data()))
        job->cancel();
}

void ShareJob::setStage(const QString &stage)
{
    m_stage = stage;
    emit stageChanged();
}

void ShareJob::succeed()
{
    m_running = false;
    m_stage.clear();
    emit finished(true);
}

void ShareJob::fail(const QString &line, bool needsConnection)
{
    m_running = false;
    m_stage.clear();
    m_failure = line;
    m_needsConnection = needsConnection;
    emit finished(false);
}

QString ShareJob::releaseLink(const QString &repository, const QString &tag, const QString &fileName)
{
    return QStringLiteral("https://github.com/%1/releases/download/%2/%3")
        .arg(repository, tag, QString::fromUtf8(QUrl::toPercentEncoding(fileName)));
}

QString ShareJob::gistId(const QString &url)
{
    return QUrl(url).path().section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
}

void ShareJob::gh(const QStringList &arguments, std::function<void(bool, const QString &)> done)
{
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Nobody is there to answer: gh fails instead of asking.
    environment.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process] { process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done(false, QStringLiteral("gh isn't installed."));
    });
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        done(status == QProcess::NormalExit && code == 0, QString::fromUtf8(process->readAll()));
    });
    process->start(GitHub::program(), arguments);
    timeout->start(ghTimeoutMs);
}

void ShareJob::start(const Request &request)
{
    m_request = request;
    m_record = {};
    m_record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_record.time = QDateTime::currentDateTime();
    m_record.format = Share::suffix(request.format);
    m_record.scope = request.scope;
    m_record.objects = request.objects;
    m_failure.clear();
    m_needsConnection = false;
    m_running = true;
    if (request.destination == Share::github)
        shareToGitHub();
    else
        shareToCloud();
}

void ShareJob::shareToCloud()
{
    const QString remote = Share::remoteOf(m_request.destination);
    // Omastrator Shares/<document>/<time>.<ext>, readable in the service's own file list.
    QString folder = m_request.documentName.trimmed();
    static const QRegularExpression separators(QStringLiteral("[/\\\\:\\x00-\\x1f]+"));
    folder.replace(separators, QStringLiteral("-"));
    if (folder.isEmpty() || folder.startsWith(QLatin1Char('.')))
        folder.prepend(QStringLiteral("Untitled"));
    const CloudLocation target{remote, QStringLiteral("Omastrator Shares/%1/%2-%3.%4").arg(folder, Share::stamp(m_record.time), m_record.id.left(4), m_record.format)};
    m_record.kind = m_request.destination;
    m_record.where = m_request.where;
    m_record.remotePath = target.toString();
    setStage(QStringLiteral("Uploading to %1…").arg(m_request.where));
    QPointer<ShareJob> self = this;
    m_cloudJob = m_cloud.upload(m_request.file, target, [self, target](const QString &error) {
        if (!self || !self->m_running)
            return;
        if (!error.isEmpty())
            return self->fail(QStringLiteral("Couldn't upload to %1: %2").arg(self->m_request.where, error));
        self->setStage(QStringLiteral("Making the link…"));
        self->m_cloudJob = self->m_cloud.link(target, [self](const QString &url, const QString &error) {
            if (!self || !self->m_running)
                return;
            if (url.isEmpty())
                return self->fail(QStringLiteral("%1 didn't make a link: %2").arg(self->m_request.where, error));
            self->m_record.link = url;
            self->succeed();
        });
    });
}

void ShareJob::shareToGitHub()
{
    setStage(QStringLiteral("Checking GitHub…"));
    gh({QStringLiteral("auth"), QStringLiteral("status"), QStringLiteral("--hostname"), QStringLiteral("github.com")}, [this](bool ok, const QString &output) {
        if (!ok)
            return fail(output.startsWith(QLatin1String("gh isn't installed")) ? QStringLiteral("GitHub isn't connected: gh isn't installed.")
                                                                                : QStringLiteral("GitHub isn't connected."),
                        true);
        if (m_request.format == Share::Format::svg) {
            // SVG is text: a secret gist, unlisted but open to anyone with the link.
            setStage(QStringLiteral("Uploading to GitHub…"));
            gh({QStringLiteral("gist"), QStringLiteral("create"), m_request.file, QStringLiteral("--desc"),
                QStringLiteral("%1, shared from Omastrator").arg(m_request.documentName)},
               [this](bool ok, const QString &output) {
                   const QString url = Deploy::firstUrl(output);
                   if (!ok || url.isEmpty())
                       return fail(QStringLiteral("Couldn't make the gist: %1").arg(oneLine(output)));
                   m_record.kind = QStringLiteral("gist");
                   m_record.where = QStringLiteral("GitHub gist");
                   m_record.link = url;
                   m_record.gist = gistId(url);
                   succeed();
               });
            return;
        }
        // PNG and PDF: an asset on a release in the user's own public repository.
        setStage(QStringLiteral("Uploading to GitHub…"));
        const QStringList view{QStringLiteral("repo"), QStringLiteral("view"), repositoryName, QStringLiteral("--json"), QStringLiteral("nameWithOwner"),
                               QStringLiteral("--jq"), QStringLiteral(".nameWithOwner")};
        gh(view, [this, view](bool ok, const QString &output) {
            if (ok && !output.trimmed().isEmpty())
                return uploadRelease(output.trimmed());
            gh({QStringLiteral("repo"), QStringLiteral("create"), repositoryName, QStringLiteral("--public"), QStringLiteral("--add-readme"),
                QStringLiteral("--description"), QStringLiteral("Files shared from Omastrator. Anyone with a link can download them.")},
               [this, view](bool ok, const QString &output) {
                   if (!ok)
                       return fail(QStringLiteral("Couldn't create the %1 repository: %2").arg(repositoryName, oneLine(output)));
                   gh(view, [this](bool ok, const QString &output) {
                       if (!ok || output.trimmed().isEmpty())
                           return fail(QStringLiteral("Couldn't find the %1 repository: %2").arg(repositoryName, oneLine(output)));
                       uploadRelease(output.trimmed());
                   });
               });
        });
    });
}

void ShareJob::uploadRelease(const QString &repository)
{
    const QString name = QFileInfo(m_request.file).fileName();
    const QString tag = QStringLiteral("share-%1-%2").arg(Share::stamp(m_record.time), m_record.id.left(4));
    gh({QStringLiteral("release"), QStringLiteral("create"), tag, m_request.file, QStringLiteral("--repo"), repository, QStringLiteral("--title"),
        QStringLiteral("%1 (%2)").arg(m_request.documentName, m_record.time.toString(QStringLiteral("yyyy-MM-dd HH:mm"))), QStringLiteral("--notes"),
        QStringLiteral("Shared from Omastrator.")},
       [this, repository, tag, name](bool ok, const QString &output) {
           if (!ok)
               return fail(QStringLiteral("Couldn't upload the release: %1").arg(oneLine(output)));
           m_record.kind = QStringLiteral("release");
           m_record.where = QStringLiteral("GitHub release");
           m_record.repository = repository;
           m_record.tag = tag;
           m_record.link = releaseLink(repository, tag, name);
           succeed();
       });
}

void ShareJob::startPreview(const QString &folder)
{
    m_record = {};
    m_record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_record.time = QDateTime::currentDateTime();
    m_record.kind = Share::live;
    m_record.where = QStringLiteral("Preview deploy");
    m_record.format = QStringLiteral("site");
    m_record.scope = QStringLiteral("site");
    m_failure.clear();
    m_needsConnection = false;
    m_log.clear();
    const std::optional<Deploy::Command> command = Deploy::resolvePreview(folder);
    if (!command) {
        m_running = true;
        QTimer::singleShot(0, this, [this] {
            fail(QStringLiteral("This project has no preview deploy: no Vercel, Netlify or Cloudflare setup, and no \"preview\" command in omastrator.json."));
        });
        return;
    }
    m_running = true;
    delete m_deploy;
    m_deploy = new DeployJob(this);
    connect(m_deploy, &DeployJob::finished, this, [this](bool ok) {
        m_log = m_deploy->log();
        if (!ok)
            return fail(QStringLiteral("Preview deploy failed: %1").arg(m_deploy->failure()));
        if (m_deploy->url().isEmpty())
            return fail(QStringLiteral("The preview deploy finished without printing a link."));
        m_record.link = m_deploy->url();
        succeed();
    });
    setStage(QStringLiteral("Preview deploying…"));
    DeployJob::Plan plan;
    plan.folder = folder;
    plan.commit = WriteBack::git(folder, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).trimmed();
    plan.push = false;
    plan.deploy = true;
    plan.command = *command;
    plan.preview = true;
    m_deploy->start(plan);
}

void ShareJob::unshare(const Share::Record &record)
{
    m_record = record;
    m_failure.clear();
    m_needsConnection = false;
    m_running = true;
    setStage(QStringLiteral("Unsharing…"));
    const auto ended = [this](bool ok, const QString &output) {
        if (!ok)
            return fail(QStringLiteral("Couldn't unshare: %1").arg(oneLine(output)));
        succeed();
    };
    if (record.kind == QLatin1String("gist")) {
        gh({QStringLiteral("gist"), QStringLiteral("delete"), record.gist, QStringLiteral("--yes")}, ended);
        return;
    }
    if (record.kind == QLatin1String("release")) {
        gh({QStringLiteral("release"), QStringLiteral("delete"), record.tag, QStringLiteral("--repo"), record.repository, QStringLiteral("--yes"),
            QStringLiteral("--cleanup-tag")},
           ended);
        return;
    }
    const std::optional<CloudLocation> file = CloudLocation::parse(record.remotePath);
    if (!file) {
        // A deploy stays up; Unshare only takes it off the list.
        QTimer::singleShot(0, this, [this] { succeed(); });
        return;
    }
    QPointer<ShareJob> self = this;
    m_cloudJob = m_cloud.deleteFile(*file, [self](const QString &error) {
        if (!self || !self->m_running)
            return;
        if (!error.isEmpty())
            return self->fail(QStringLiteral("Couldn't unshare: %1").arg(error));
        self->succeed();
    });
}

void ShareJob::cancel()
{
    if (!m_running)
        return;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
    }
    if (auto *job = qobject_cast<CloudJob *>(m_cloudJob.data()))
        job->cancel();
    if (m_deploy) {
        m_deploy->disconnect(this);
        m_deploy->cancel();
    }
    fail(QStringLiteral("Cancelled."));
}
