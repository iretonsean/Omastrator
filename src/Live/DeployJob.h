#pragma once
#include "Live/Deploy.h"
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <deque>
#include <functional>

// The slow half of Save and Deploy, run in the background: create the GitHub
// repository if asked, push, then run the deploy command with the project's
// environment (or wait while the agent deploys). Everything it runs is written,
// redacted, to one log file under Deploy::logDirectory().
class DeployJob : public QObject {
    Q_OBJECT
public:
    enum class Stage { idle, github, pushing, deploying, done, failed };
    struct Plan {
        QString folder;
        // The commit being deployed, for the record; empty outside git.
        QString commit;
        bool push = true;
        // A name: `gh repo create` it (private, pushing) instead of a plain push.
        QString createRepository;
        bool deploy = true;
        Deploy::Command command;
    };

    explicit DeployJob(QObject *parent = nullptr);
    ~DeployJob() override;

    void start(const Plan &plan);
    // Stops what is running; the job fails with "Cancelled."
    void cancel();
    // Deploy with agent: what `live_deployed` reported.
    void agentFinished(const QString &url, const QString &error);

    Stage stage() const { return m_stage; }
    bool running() const { return m_stage != Stage::idle && m_stage != Stage::done && m_stage != Stage::failed; }
    const Plan &plan() const { return m_plan; }
    QString log() const { return m_log; }
    QString url() const { return m_url; }
    // One line on why it failed, with no .env value in it.
    QString failure() const { return m_failure; }
    // Why nothing was pushed, when there was nowhere to push to.
    QString pushNote() const { return m_pushNote; }
    const Deploy::Variables &variables() const { return m_variables; }

signals:
    void stageChanged();
    // The command is the agent: launch it, then call agentFinished().
    void agentNeeded();
    void finished(bool ok);

private:
    void next();
    void setStage(Stage stage);
    void fail(const QString &line);
    void write(const QString &text);
    // Runs one program; `done` gets whether it succeeded and what it printed (redacted).
    void run(const QString &program, const QStringList &arguments, const QString &cwd, const QProcessEnvironment &environment,
             std::function<void(bool, const QString &)> done);
    void drain(bool all);

    Plan m_plan;
    Stage m_stage = Stage::idle;
    std::deque<std::function<void()>> m_steps;
    Deploy::Variables m_variables;
    QString m_log;
    QFile m_file;
    QString m_url;
    QString m_failure;
    QString m_pushNote;
    QPointer<QProcess> m_process;
    QByteArray m_pending;
    QString m_output;
    QTimer m_timeout;
};
