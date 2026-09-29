#include "Live/DeployJob.h"
#include "Live/History.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <csignal>
#include <unistd.h>

namespace {
// Deploys can build a whole site; half an hour is the most one gets.
constexpr int longestStepMs = 30 * 60 * 1000;

// A finished or never-started process has id 0, and kill(-0) would signal our own group.
void signalGroup(const QProcess *process, int signal)
{
    if (process && process->processId() > 0)
        ::kill(-pid_t(process->processId()), signal);
}
}

DeployJob::DeployJob(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        write(QStringLiteral("\nStopped after 30 minutes.\n"));
        if (m_process)
            signalGroup(m_process, SIGTERM);
    });
}

DeployJob::~DeployJob()
{
    if (m_process) {
        m_process->disconnect(this);
        signalGroup(m_process, SIGTERM);
        m_process->waitForFinished(2000);
    }
}

void DeployJob::setStage(Stage stage)
{
    m_stage = stage;
    emit stageChanged();
}

void DeployJob::write(const QString &text)
{
    if (m_file.isOpen()) {
        m_file.write(Deploy::redact(text, m_variables).toUtf8());
        m_file.flush();
    }
}

void DeployJob::fail(const QString &line)
{
    m_failure = Deploy::redact(line, m_variables);
    m_steps.clear();
    write(QStringLiteral("\nFailed: %1\n").arg(line));
    m_file.close();
    setStage(Stage::failed);
    emit finished(false);
}

void DeployJob::start(const Plan &plan)
{
    m_plan = plan;
    m_url.clear();
    m_failure.clear();
    m_pushNote.clear();
    m_steps.clear();
    QStringList envFiles;
    m_variables = plan.deploy ? Deploy::projectEnv(plan.folder, plan.command.cwd, &envFiles) : Deploy::Variables{};
    m_log = Deploy::newLogPath(plan.folder);
    m_file.close();
    m_file.setFileName(m_log);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fail(QStringLiteral("Couldn't write the deploy log %1.").arg(m_log));
        return;
    }
    QString header = QStringLiteral("Omastrator %1 of %2, %3\n").arg(plan.preview ? QStringLiteral("preview deploy") : plan.deploy ? QStringLiteral("deploy") : QStringLiteral("save"), plan.folder,
                                                                         QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!plan.commit.isEmpty())
        header += QStringLiteral("Commit: %1\n").arg(plan.commit);
    if (plan.deploy) {
        header += QStringLiteral("Deploy: %1 (from %2)\n").arg(plan.command.viaAgent() ? QStringLiteral("your agent") : plan.command.command, plan.command.source);
        QStringList names;
        for (const QString &file : std::as_const(envFiles))
            names << QDir(plan.folder).relativeFilePath(file);
        // Key names only; their values stay in the deploy's own environment.
        header += names.isEmpty() ? QStringLiteral("Environment: no .env files\n")
                                  : QStringLiteral("Environment from %1: %2\n").arg(names.join(QStringLiteral(", ")), Deploy::keys(m_variables).join(QStringLiteral(", ")));
    }
    write(header);

    if (!plan.createRepository.isEmpty()) {
        m_steps.push_back([this] {
            setStage(Stage::github);
            write(QStringLiteral("\n$ gh %1\n").arg(GitHub::createArguments(m_plan.createRepository).join(QLatin1Char(' '))));
            run(GitHub::program(), GitHub::createArguments(m_plan.createRepository), m_plan.folder, QProcessEnvironment::systemEnvironment(),
                [this](bool ok, const QString &output) {
                    if (!ok)
                        return fail(QStringLiteral("Creating the GitHub repository failed: %1").arg(Deploy::lastLine(output)));
                    next();
                });
        });
    } else if (plan.push) {
        m_steps.push_back([this] {
            QString why;
            const auto target = History::pushTarget(m_plan.folder, &why);
            if (!target) {
                m_pushNote = why;
                write(QStringLiteral("\nNot pushed: %1\n").arg(why));
                return next();
            }
            setStage(Stage::pushing);
            write(QStringLiteral("\n$ git %1\n").arg(target->arguments().join(QLatin1Char(' '))));
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            // Nobody is there to type a password; git fails instead of waiting.
            environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
            run(QStringLiteral("git"), target->arguments(), m_plan.folder, environment, [this](bool ok, const QString &output) {
                if (!ok)
                    return fail(QStringLiteral("Push failed: %1").arg(Deploy::lastLine(output)));
                next();
            });
        });
    }
    if (plan.deploy) {
        m_steps.push_back([this] {
            setStage(Stage::deploying);
            if (m_plan.command.viaAgent()) {
                write(QStringLiteral("\nYour agent is deploying.\n"));
                emit agentNeeded();
                return;
            }
            write(QStringLiteral("\n$ %1\n").arg(m_plan.command.command));
            run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), m_plan.command.command}, m_plan.command.cwd, Deploy::environment(m_variables),
                [this](bool ok, const QString &output) {
                    m_url = Deploy::firstUrl(output);
                    Deploy::addRecord({m_plan.folder, m_plan.commit, m_url, m_plan.command.command, m_log, QDateTime::currentDateTime(), ok, m_plan.preview});
                    if (!ok) {
                        const QString line = Deploy::lastLine(output);
                        return fail(line.isEmpty() ? QStringLiteral("%1 failed.").arg(m_plan.command.command) : line);
                    }
                    next();
                });
        });
    }
    next();
}

void DeployJob::agentFinished(const QString &url, const QString &error)
{
    if (m_stage != Stage::deploying || !m_plan.command.viaAgent())
        return;
    m_url = Deploy::redact(url.trimmed(), m_variables);
    Deploy::addRecord({m_plan.folder, m_plan.commit, m_url, QStringLiteral("agent"), m_log, QDateTime::currentDateTime(), error.isEmpty(), m_plan.preview});
    if (!error.isEmpty())
        return fail(error);
    write(QStringLiteral("The agent reports it live at %1\n").arg(m_url));
    next();
}

void DeployJob::next()
{
    if (m_steps.empty()) {
        write(QStringLiteral("\nDone.%1\n").arg(m_url.isEmpty() ? QString() : QStringLiteral(" Live at %1").arg(m_url)));
        m_file.close();
        setStage(Stage::done);
        emit finished(true);
        return;
    }
    const auto step = std::move(m_steps.front());
    m_steps.pop_front();
    step();
}

void DeployJob::cancel()
{
    if (!running())
        return;
    if (m_process) {
        m_process->disconnect(this);
        signalGroup(m_process, SIGTERM);
        if (!m_process->waitForFinished(3000))
            m_process->kill();
        m_process->deleteLater();
    }
    m_timeout.stop();
    fail(QStringLiteral("Cancelled."));
}

void DeployJob::drain(bool all)
{
    if (!m_process)
        return;
    m_pending += m_process->readAll();
    // Whole lines only, so a value split across two reads is still redacted.
    const qsizetype end = all ? m_pending.size() : m_pending.lastIndexOf('\n') + 1;
    if (end <= 0)
        return;
    const QString text = Deploy::redact(QString::fromUtf8(m_pending.left(end)), m_variables);
    m_pending.remove(0, end);
    m_output += text;
    if (m_file.isOpen()) {
        m_file.write(text.toUtf8());
        m_file.flush();
    }
}

void DeployJob::run(const QString &program, const QStringList &arguments, const QString &cwd, const QProcessEnvironment &environment,
                    std::function<void(bool, const QString &)> done)
{
    auto *process = new QProcess(this);
    m_process = process;
    m_pending.clear();
    m_output.clear();
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(cwd);
    process->setProcessEnvironment(environment);
    process->setStandardInputFile(QProcess::nullDevice());
    // Its own process group, so Cancel stops everything the command started.
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
    connect(process, &QProcess::readyRead, this, [this] { drain(false); });
    connect(process, &QProcess::errorOccurred, this, [this, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_timeout.stop();
        process->deleteLater();
        done(false, QStringLiteral("Couldn't run %1: %2").arg(process->program(), process->errorString()));
    });
    connect(process, &QProcess::finished, this, [this, process, done](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        drain(true);
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok)
            write(QStringLiteral("(exit code %1)\n").arg(code));
        process->deleteLater();
        done(ok, m_output);
    });
    m_timeout.start(longestStepMs);
    process->start(program, arguments);
}
