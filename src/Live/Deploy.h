#pragma once
#include <QDateTime>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

// Deploy-first Live (docs/OS-SUITE.md): how a project deploys, with the
// environment its .env files define. Values from those files never leave the
// deploy's own process: logs and status lines are redacted, and only key names
// are ever shown or put in a prompt.
namespace Deploy {
using Variables = std::vector<std::pair<QString, QString>>;

// dotenv: KEY=value, quotes, `export `, comments. Later keys win.
Variables parseEnv(const QByteArray &text);
// .env, .env.local, .env.production, .env.production.local in `folder`, then in `cwd` when it differs; later files win.
Variables projectEnv(const QString &folder, const QString &cwd = QString(), QStringList *files = nullptr);
// The system environment with the project's variables over it, apart from PATH and HOME.
QProcessEnvironment environment(const Variables &variables);
QStringList keys(const Variables &variables);
// Each value (four characters or longer) replaced by its key: "[API_TOKEN]".
QString redact(QString text, const Variables &variables);

struct Command {
    // A shell command; empty means the agent deploys.
    QString command;
    // Where it runs, absolute.
    QString cwd;
    // "omastrator.json", "package.json", "vercel", "netlify", "wrangler", "fly", "make", "deploy.sh", or "agent".
    QString source;
    bool viaAgent() const { return command.isEmpty(); }
};
// omastrator.json's "deploy", then package.json's deploy or deploy:prod script, then a host CLI whose config is
// there and which is installed, then a Makefile deploy target or deploy.sh; otherwise the agent.
Command resolve(const QString &folder);
// A deploy that isn't production, for Share: omastrator.json's "preview", package.json's deploy:preview or
// preview:deploy script, `vercel deploy`, a `netlify deploy` draft, or `wrangler pages deploy --branch preview` /
// `wrangler versions upload`. Nullopt when the project has none.
std::optional<Command> resolvePreview(const QString &folder);
// Writes {"deploy": {"command", "cwd"?}} into the project's omastrator.json, keeping its other keys. Returns why it failed, or empty.
QString remember(const QString &folder, const QString &command, const QString &cwd = QString());

// What Omastrator remembers per project, beside the registry: $XDG_CONFIG_HOME/omastrator/deploy.json.
struct Settings {
    // "Don't ask again for this project" on the production confirmation.
    bool confirmed = false;
    // The user said no to a GitHub repository for this project.
    bool githubDeclined = false;
};
QString settingsPath();
Settings settings(const QString &folder);
QString saveSettings(const QString &folder, const Settings &settings);

// One deploy, kept in $XDG_STATE_HOME/omastrator/deploys/index.json.
struct Record {
    QString project;
    QString commit;
    QString url;
    QString command;
    QString log;
    QDateTime time;
    bool ok = false;
    // A preview deploy (Share): never marks a commit as deployed.
    bool preview = false;
};
QString logDirectory();
// A new log file's path for `folder`, in logDirectory().
QString newLogPath(const QString &folder);
std::vector<Record> records(const QString &folder = QString());
QString addRecord(const Record &record);
// The last successful production deploy of `commit`, if any.
std::optional<Record> deployed(const QString &folder, const QString &commit);
// The newest successful deploy with a URL, production or preview.
std::optional<Record> latest(const QString &folder);

// Deploy with agent: the task for the default agent, run in the project's own checkout. It names the .env files
// and their keys, never a value.
struct AgentBrief {
    QString requestId;
    QString folder;
    QString commit;
    QString subject;
    // "origin/main", or empty when nothing was pushed.
    QString pushedTo;
    // The command to report back with: `omastrator`, quoted.
    QString binary;
};
QString agentPrompt(const AgentBrief &brief);

// The first https URL in `output`: the live URL.
QString firstUrl(const QString &output);
// The line a failure is summed up by (DeployFix::reason): the real reason, not a banner or a box, cut short.
QString lastLine(const QString &output);
// One dry line for a deploy failure, each shown once per install; empty once all are used.
QString dryLine();
// The same for any list of lines, sharing the record of lines seen.
QString dryLine(const QStringList &lines);
}
