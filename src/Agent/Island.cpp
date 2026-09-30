#include "Agent/Island.h"
#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Capture.h"
#include "Agent/Dictation.h"
#include "Agent/Hyprland.h"
#include "Agent/Setup.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <unistd.h>

namespace {
QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString writeJson(const QString &path, const QJsonObject &json)
{
    const QString folder = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder))
        return QStringLiteral("Could not create %1.").arg(folder);
    QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}

// A desktop notification through notify-send. One shared id-like hint makes a newer line replace the last where the daemon
// honours it (mako and dunst do), so "Listening…" then "Heard: …" is one card, not a stack.
void notify(const QString &text, int seconds)
{
    if (text.trimmed().isEmpty())
        return;
    const QString overridden = qEnvironmentVariable("OMASTRATOR_NOTIFY");
    const QString program = overridden.isEmpty() ? QStringLiteral("notify-send") : overridden;
    const qsizetype newline = text.indexOf(QLatin1Char('\n'));
    const QString summary = (newline < 0 ? text : text.left(newline)).trimmed();
    const QString body = newline < 0 ? QString() : text.mid(newline + 1).trimmed();
    QStringList arguments{QStringLiteral("-a"), QStringLiteral("Omastrator"), QStringLiteral("-t"), QString::number(seconds * 1000),
                          QStringLiteral("-h"), QStringLiteral("string:x-canonical-private-synchronous:omastrator"), summary};
    if (!body.isEmpty())
        arguments << body;
    // Nobody waits for it, and a missing notify-send is no failure of the command.
    QProcess::startDetached(program, arguments);
}

QString stateHome()
{
    const QString given = qEnvironmentVariable("XDG_STATE_HOME");
    return given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/state")) : given;
}

// What a removed mode's name says now, or empty for a mode that still exists.
QString removedMode(const QString &mode)
{
    if (mode == QLatin1String("draw"))
        return QStringLiteral("Draw mode is gone: use Omastrator's toolbar.");
    if (mode == QLatin1String("capture"))
        return QStringLiteral("Capture mode is gone: use the Capture tab in Omastrator, or `omastrator island capture …`.");
    if (mode == QLatin1String("ai"))
        return QStringLiteral("AI mode is gone: use the Ask field in Omastrator (`omastrator island ask` focuses it), or `omastrator island ai …`.");
    if (mode == QLatin1String("live"))
        return QStringLiteral("Live mode is gone: use a Browser View in Omastrator, or `omastrator island live …`.");
    if (mode == QLatin1String("next") || mode == QLatin1String("previous"))
        return QStringLiteral("Modes no longer step: choose `omastrator island mode normal` or `mode design`.");
    return {};
}

// The verbs that only served the pill, with where their job went.
QString removedVerb(const QString &verb, const QString &argument)
{
    if (verb == QLatin1String("tool"))
        return QStringLiteral("Draw mode is gone: use Omastrator's toolbar.");
    if (verb == QLatin1String("expand") || verb == QLatin1String("rest") || verb == QLatin1String("toggle"))
        return QStringLiteral("The island pill is gone, so there is nothing to %1.").arg(verb == QLatin1String("toggle") ? QStringLiteral("expand or rest") : verb);
    if (verb == QLatin1String("seen"))
        return QStringLiteral("The island pill is gone, so it shows no first-use labels.");
    if (verb == QLatin1String("show") && (argument == QLatin1String("always") || argument == QLatin1String("with-app")))
        return QStringLiteral("The island pill is gone, so it has no place to show: notifications and Omastrator's window replace it.");
    return {};
}
}

namespace Island {
const QStringList &modes()
{
    static const QStringList all{QStringLiteral("normal"), QStringLiteral("design")};
    return all;
}

QString runtimeDirectory()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_RUNTIME_DIR");
    if (!overridden.isEmpty())
        return overridden;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir(runtime).exists())
        return QDir(runtime).filePath(QStringLiteral("omastrator"));
    return QStringLiteral("/tmp/omastrator-%1").arg(getuid());
}

QString statePath()
{
    return QDir(runtimeDirectory()).filePath(QStringLiteral("island.json"));
}

QString stateDirectory()
{
    return QDir(stateHome()).filePath(QStringLiteral("omastrator"));
}

QJsonObject State::toJson() const
{
    return {{"mode", mode}, {"activity", activity}, {"activityId", activityId}, {"activitySeconds", activitySeconds}};
}

State State::fromJson(const QJsonObject &json)
{
    State state;
    if (modes().contains(json["mode"].toString()))
        state.mode = json["mode"].toString();
    state.activity = json["activity"].toString();
    state.activityId = qint64(json["activityId"].toDouble());
    state.activitySeconds = json["activitySeconds"].toInt(3);
    return state;
}

State read()
{
    return State::fromJson(readJson(statePath()));
}

QString write(const State &state)
{
    return writeJson(statePath(), state.toJson());
}

QString setActivity(const QString &text, int seconds)
{
    State state = read();
    state.activity = text;
    state.activityId = QDateTime::currentMSecsSinceEpoch();
    state.activitySeconds = std::clamp(seconds, 1, 60);
    const QString failure = write(state);
    notify(text, state.activitySeconds);
    return failure;
}

bool appIsRunning()
{
    QLocalSocket probe;
    probe.connectToServer(AgentProtocol::socketPath());
    const bool up = probe.waitForConnected(300);
    probe.abort();
    return up;
}

void resetKeys()
{
    // Harmless when no submap is held.
    Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.submap(\"reset\"))"), QStringLiteral("submap reset"));
}

void holdKeysFor(const QString &mode)
{
    const QString submap = QStringLiteral("omastrator-") + mode;
    if (mode == QLatin1String("normal") || !Setup::submapDefined(Setup::Environment::current(), submap)) {
        resetKeys();
        return;
    }
    Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.submap(\"%1\"))").arg(submap), QStringLiteral("submap ") + submap);
}

void holdDesignKeys()
{
    if (!Setup::designKeysLoaded(Setup::Environment::current()))
        return;
    Hyprland::dispatch(QStringLiteral("hl.dispatch(hl.dsp.submap(\"omastrator-design\"))"), QStringLiteral("submap omastrator-design"));
}

QString ensureAppRunning(int timeoutMs)
{
    if (appIsRunning())
        return {};
    const QString overridden = qEnvironmentVariable("OMASTRATOR_APP");
    const QString program = overridden.isEmpty() ? QCoreApplication::applicationFilePath() : overridden;
    // In the background: the window opens only when something asks for it.
    if (!QProcess::startDetached(program, {QStringLiteral("--daemon")}))
        return QStringLiteral("Could not start Omastrator (%1).").arg(program);
    for (int waited = 0; waited < timeoutMs; waited += 100) {
        QThread::msleep(100);
        if (appIsRunning())
            return {};
    }
    return QStringLiteral("Omastrator did not start in time. Open it, then try again.");
}

QString helpText()
{
    return QStringLiteral(
        "Usage: omastrator island <verb> [args]\n\n"
        "Runs Omastrator's desktop commands. The pill under the bar is gone: Draw,\n"
        "Capture, AI and Live moved into Omastrator's window (its toolbar, the\n"
        "Capture tab, the Ask field and Browser View); these commands stay.\n\n"
        "  mode <normal|design>\n"
        "                     Switch design mode (docs/ANYWHERE.md) on or off. Design\n"
        "                     starts Omastrator in the background if it isn't running.\n"
        "  activity <text> [--seconds N]\n"
        "                     Show a line as a desktop notification.\n"
        "  new                Bring Omastrator forward on a new document.\n"
        "  show <swatches|variations|roast|connect-agent>\n"
        "                     Bring Omastrator forward on a panel.\n"
        "  ask                Bring Omastrator forward with the Ask field focused.\n"
        "  page <workspace>   Go to the page that holds a workspace (`design:Doc · Page`):\n"
        "                     the bar's page dots. Fails when Omastrator isn't running.\n"
        "  ai <generate|edit|roast|vectorize|cancel> [--prompt TEXT] [--count N]\n"
        "     [--fit] [--mode logo|sketch]\n"
        "                     Start an AI flow. Without a prompt, Generate and Edit\n"
        "                     open their sheet in Omastrator.\n"
        "  live start [--url URL [--app]] [--command CMD] [--folder PATH] | stop\n"
        "       | select on|off | status | handoff [FOLDER] [NOTES]\n"
        "                     Live mode: edit a page in Omastrator's Chromium. start\n"
        "                     with neither option opens the Live sheet.\n"
        "  live start --tab [ID] [--folder PATH]\n"
        "                     Live in your own Chromium's tab (its active tab without\n"
        "                     ID), through Omastrator's extension.\n"
        "  live deploy [--confirm [--remember]] [--github NAME | --no-github]\n"
        "       [--folder PATH]\n"
        "                     Write the live edits into the code, commit, push, and\n"
        "                     deploy to production with the project's own setup.\n"
        "                     The first deploy of a project asks in Omastrator.\n"
        "  live save [--github NAME | --no-github]\n"
        "                     The same, without deploying.\n"
        "  live changes | discard [ID] | history [--list] | restore SHA\n"
        "       | details | remember | github [connect] | cancel\n"
        "                     The record of write-backs and their diffs, the\n"
        "                     project's commits, the last deploy's log, the\n"
        "                     command the agent deployed with, and GitHub.\n"
        "  live writeback | ask TEXT\n"
        "                     Write the edits back (or ask the agent) without saving.\n"
        "  dictate start | stop | cancel\n"
        "                     Push-to-talk: start listens, stop transcribes with voxtype,\n"
        "                     shows what was heard, and runs it unless cancelled.\n"
        "  dictate file WAV | transcribe WAV | parse TEXT\n"
        "                     The same from a recording, or just the parse.\n"
        "  capture color [fill|stroke|swatch]\n"
        "                     Pick a colour anywhere on screen (hyprpicker).\n"
        "  capture screenshot Choose a region (slurp, grim), open it and trace it.\n"
        "  capture window     The focused window, opened and traced: Capture to Omastrator.\n"
        "  capture paste-svg  Paste the clipboard's SVG as editable paths.\n"
        "  capture theme-swatches\n"
        "                     Load the Omarchy theme's colours as a swatch group.\n"
        "  state              Print the mode and the last activity line as JSON.\n");
}

int runCli(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString verb = args.value(0);
    if (verb.isEmpty() || verb == QLatin1String("--help") || verb == QLatin1String("-h") || verb == QLatin1String("help")) {
        (verb.isEmpty() ? err : out) << helpText();
        return verb.isEmpty() ? 1 : 0;
    }
    auto failed = [&](const QString &message) {
        err << message << '\n';
        return 1;
    };
    auto save = [&](const State &state) {
        const QString failure = write(state);
        return failure.isEmpty() ? 0 : failed(failure);
    };
    State state = read();
    if (verb == QLatin1String("state")) {
        out << QString::fromUtf8(QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact)) << '\n';
        return 0;
    }
    if (verb == QLatin1String("mode")) {
        const QString mode = args.value(1);
        if (const QString gone = removedMode(mode); !gone.isEmpty())
            return failed(gone);
        if (!modes().contains(mode))
            return failed(QStringLiteral("Choose a mode: %1.").arg(modes().join(QStringLiteral(", "))));
        state.mode = mode;
        if (const int code = save(state); code != 0)
            return code;
        // The keys follow the mode however it changed: a mode left by a click keeps none of its letters.
        holdKeysFor(mode);
        // Design mode is kept by the background app, which starts it if it isn't running.
        if (mode == QLatin1String("design")) {
            if (const QString failure = ensureAppRunning(); !failure.isEmpty())
                return failed(failure);
        }
        return 0;
    }
    if (const QString gone = removedVerb(verb, args.value(1)); !gone.isEmpty())
        return failed(gone);
    if (verb == QLatin1String("activity")) {
        QStringList words = args.mid(1);
        int seconds = 3;
        if (const qsizetype at = words.indexOf(QStringLiteral("--seconds")); at >= 0) {
            bool ok = false;
            seconds = words.value(at + 1).toInt(&ok);
            if (!ok)
                return failed(QStringLiteral("--seconds needs a whole number."));
            words.remove(at, std::min<qsizetype>(2, words.size() - at));
        }
        const QString failure = setActivity(words.join(QLatin1Char(' ')).trimmed(), seconds);
        return failure.isEmpty() ? 0 : failed(failure);
    }
    if (verb == QLatin1String("ask")) {
        if (const QString failure = ensureAppRunning(); !failure.isEmpty())
            return failed(failure);
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("show_window"), {{"raise", true}, {"focus", "ask"}});
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            return failed(failure.message());
        }
    }
    // The bar's page dots: the running app moves its editor to the page's workspace and the user with it, in one step.
    // It never starts the app: the dot falls back to Hyprland's own focus when this fails.
    if (verb == QLatin1String("page")) {
        if (args.size() < 2 || args.mid(1).join(QLatin1Char(' ')).trimmed().isEmpty())
            return failed(QStringLiteral("Name a page's workspace, as `design:Poster · Front`."));
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("go_to_page"), {{"workspace", args.mid(1).join(QLatin1Char(' '))}}, 5000);
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            return failed(failure.message());
        }
    }
    if (verb == QLatin1String("new") || verb == QLatin1String("show")) {
        if (verb == QLatin1String("show") && args.size() != 2)
            return failed(QStringLiteral("Name a panel: swatches, variations, roast or connect-agent."));
        if (const QString failure = ensureAppRunning(); !failure.isEmpty())
            return failed(failure);
        try {
            AgentClient::Connection connection;
            if (verb == QLatin1String("new"))
                connection.call(QStringLiteral("new_document"), {});
            else
                connection.call(QStringLiteral("show_panel"),
                                {{"panel", args[1] == QLatin1String("connect-agent") ? QStringLiteral("connectAgent") : args[1]}});
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            return failed(failure.message());
        }
    }
    if (verb == QLatin1String("ai")) {
        const QString flow = args.value(1);
        if (!QStringList{"generate", "edit", "roast", "vectorize", "cancel", "handoff"}.contains(flow))
            return failed(QStringLiteral("Choose an AI flow: generate, edit, roast, vectorize, handoff or cancel."));
        QJsonObject params{{"flow", flow}};
        const QStringList options = args.mid(2);
        for (qsizetype at = 0; at < options.size(); ++at) {
            const QString option = options[at];
            if (option == QLatin1String("--fit")) {
                params["fitToSelection"] = true;
            } else if (option == QLatin1String("--prompt") || option == QLatin1String("--count") || option == QLatin1String("--mode")) {
                if (at + 1 >= options.size())
                    return failed(QStringLiteral("%1 needs a value.").arg(option));
                const QString value = options[++at];
                if (option == QLatin1String("--count"))
                    params["count"] = value.toInt();
                else
                    params[option.mid(2)] = value;
            } else {
                return failed(QStringLiteral("Unknown option %1.").arg(option));
            }
        }
        if (const QString failure = ensureAppRunning(); !failure.isEmpty()) {
            setActivity(failure, 6);
            return failed(failure);
        }
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("ai_start"), params);
            if ((flow == QLatin1String("generate") || flow == QLatin1String("edit")) && !params.contains("prompt"))
                setActivity(flow == QLatin1String("generate") ? QStringLiteral("Generate is open in Omastrator")
                                                              : QStringLiteral("Edit with Instruction is open in Omastrator"),
                            3);
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            // The island says why, since nobody sees this command's output.
            setActivity(failure.message(), 6);
            return failed(failure.message());
        }
    }
    if (verb == QLatin1String("live")) {
        const QString action = args.value(1);
        QJsonObject params{{"action", action}};
        QStringList rest = args.mid(2);
        // "--flag VALUE" out of `rest`; false when the value is missing.
        auto option = [&](const QString &flag, const QString &key) {
            const qsizetype at = rest.indexOf(flag);
            if (at < 0)
                return true;
            if (at + 1 >= rest.size())
                return false;
            params[key] = key == QLatin1String("folder") ? QFileInfo(rest[at + 1]).absoluteFilePath() : rest[at + 1];
            rest.remove(at, 2);
            return true;
        };
        if (action == QLatin1String("start")) {
            const QStringList options = args.mid(2);
            for (qsizetype at = 0; at < options.size(); ++at) {
                if (options[at] == QLatin1String("--app")) {
                    // A bare flag: the page opens as an app window.
                    params["app"] = true;
                    continue;
                }
                if (options[at] == QLatin1String("--tab")) {
                    // The user's own Chromium: a tab id, or its active tab.
                    bool number = false;
                    const int tab = options.value(at + 1).toInt(&number);
                    params["tab"] = number ? tab : 0;
                    at += number ? 1 : 0;
                    continue;
                }
                if (options[at] != QLatin1String("--url") && options[at] != QLatin1String("--folder") && options[at] != QLatin1String("--command"))
                    return failed(QStringLiteral("Unknown option %1.").arg(options[at]));
                if (at + 1 >= options.size())
                    return failed(QStringLiteral("%1 needs a value.").arg(options[at]));
                params[options[at].mid(2)] = options[at] == QLatin1String("--folder") ? QFileInfo(options[at + 1]).absoluteFilePath() : options[at + 1];
                ++at;
            }
        } else if (action == QLatin1String("select")) {
            params["on"] = args.value(2) != QLatin1String("off");
        } else if (action == QLatin1String("handoff")) {
            rest.removeAll(QStringLiteral("--confirm"));
            if (!rest.isEmpty())
                params["folder"] = QFileInfo(rest.takeFirst()).absoluteFilePath();
            params["prompt"] = rest.join(QLatin1Char(' '));
        } else if (action == QLatin1String("deploy") || action == QLatin1String("save")) {
            params["confirm"] = rest.removeAll(QStringLiteral("--confirm")) > 0;
            params["remember"] = rest.removeAll(QStringLiteral("--remember")) > 0;
            if (rest.removeAll(QStringLiteral("--no-github")) > 0)
                params["github"] = QString();
            if (!option(QStringLiteral("--github"), QStringLiteral("github")) || !option(QStringLiteral("--folder"), QStringLiteral("folder")))
                return failed(QStringLiteral("%1 needs a value.").arg(rest.last()));
            if (!rest.isEmpty())
                return failed(QStringLiteral("Unknown option %1.").arg(rest.front()));
        } else if (action == QLatin1String("writeback")) {
            params["action"] = QStringLiteral("writeBack");
        } else if (action == QLatin1String("ask")) {
            rest.removeAll(QStringLiteral("--confirm"));
            params["prompt"] = rest.join(QLatin1Char(' '));
        } else if (action == QLatin1String("discard")) {
            params["id"] = rest.value(0);
        } else if (action == QLatin1String("restore")) {
            if (rest.isEmpty())
                return failed(QStringLiteral("Name the commit to restore: omastrator island live restore <sha>"));
            params["id"] = rest.value(0);
        } else if (action == QLatin1String("history")) {
            params["list"] = rest.removeAll(QStringLiteral("--list")) > 0;
        } else if (action == QLatin1String("github")) {
            params["connect"] = rest.value(0) == QLatin1String("connect");
        } else if (action == QLatin1String("changes")) {
            params["action"] = QStringLiteral("review");
        } else if (!QStringList{"stop", "status", "review", "cancel", "details", "remember"}.contains(action)) {
            return failed(QStringLiteral("Choose a Live action: start, stop, select on|off, deploy, save, changes, discard, history, restore, "
                                         "details, remember, github, cancel, writeback, ask or status."));
        }
        if (const QString failure = ensureAppRunning(); !failure.isEmpty()) {
            setActivity(failure, 6);
            return failed(failure);
        }
        try {
            AgentClient::Connection connection;
            const QJsonObject result = connection.call(QStringLiteral("live"), params);
            if (action == QLatin1String("status")) {
                out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
            } else if (result["sheet"].toBool()) {
                setActivity(action == QLatin1String("deploy")  ? QStringLiteral("Confirm the deploy in Omastrator")
                            : action == QLatin1String("save") ? QStringLiteral("Confirm the save in Omastrator")
                                                              : QStringLiteral("Live is open in Omastrator: choose a page"),
                            4);
            } else if (action == QLatin1String("history")) {
                for (const QJsonValue &value : result["commits"].toArray()) {
                    const QJsonObject commit = value.toObject();
                    out << commit["sha"].toString().left(7) << "  " << commit["time"].toString() << "  " << commit["subject"].toString();
                    if (commit["deployed"].toBool())
                        out << "  [deployed" << (commit["url"].toString().isEmpty() ? QString() : QStringLiteral(" ") + commit["url"].toString()) << "]";
                    out << '\n';
                }
            } else if (action == QLatin1String("github")) {
                out << (!result["installed"].toBool() ? QStringLiteral("gh isn't installed.")
                        : result["loggedIn"].toBool() ? QStringLiteral("Logged in to GitHub as %1.").arg(result["account"].toString())
                                                      : QStringLiteral("Not logged in to GitHub. Run: omastrator island live github connect"))
                    << '\n';
            } else if (!result["output"].toString().isEmpty()) {
                out << result["output"].toString();
            }
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            setActivity(failure.message(), 6);
            return failed(failure.message());
        }
    }
    if (verb == QLatin1String("capture"))
        return Capture::runCli(args.mid(1), out, err);
    if (verb == QLatin1String("dictate"))
        return Dictation::runCli(args.mid(1), out, err);
    return failed(QStringLiteral("There is no island verb “%1”. Run `omastrator island --help`.").arg(verb));
}
}
