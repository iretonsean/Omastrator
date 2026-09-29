#include "Agent/DesignCli.h"
#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/BrowserPoolState.h"
#include "Agent/Island.h"
#include "Agent/WorkspaceClaims.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace {
int failed(QTextStream &err, const QString &message)
{
    err << message << '\n';
    return 1;
}

// Calls the background app, starting it first. Errors also reach the island, since the overlay's clicks have no terminal.
int call(const QString &method, const QJsonObject &params, QTextStream &out, QTextStream &err, bool print = false)
{
    if (const QString failure = Island::ensureAppRunning(); !failure.isEmpty()) {
        Island::setActivity(failure, 6);
        return failed(err, failure);
    }
    try {
        AgentClient::Connection connection;
        const QJsonObject result = connection.call(method, params, 60'000);
        if (print)
            out << QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
        return 0;
    } catch (const AgentProtocol::Error &failure) {
        Island::setActivity(failure.message(), 6);
        return failed(err, failure.message());
    }
}

// "--name VALUE" taken out of `words`; false when the value is missing.
bool option(QStringList &words, const QString &name, QString *value)
{
    const qsizetype at = words.indexOf(name);
    if (at < 0)
        return true;
    if (at + 1 >= words.size())
        return false;
    *value = words[at + 1];
    words.remove(at, 2);
    return true;
}
}

namespace DesignCli {
QString designHelp()
{
    return QStringLiteral(
        "Usage: omastrator design <verb> [args]\n\n"
        "Design mode everywhere (docs/ANYWHERE.md): inspect, measure and draw on top\n"
        "of any window, page or the desktop. Clicks still reach the apps underneath.\n\n"
        "  on [--monitor NAME] | off | toggle\n"
        "                     Start or leave design mode on the focused monitor.\n"
        "  tool <point|inspect|pen|rectangle|ellipse|line|arrow|text|note>\n"
        "                     What the overlay does with the pointer. Inspect leaves\n"
        "                     it click-through; the others draw on the surface.\n"
        "  alt on|off         Hold to measure from what's hovered to the next thing.\n"
        "  select ID | deselect\n"
        "                     Pin the floating bar to a hovered thing (its id).\n"
        "  home [--target N]  Move the floating bar to that window (or the one hovered), where it\n"
        "                     then sticks.\n"
        "  follow on|off      Whether the bar follows the pointer from window to window\n"
        "                     instead of sticking to its app (off unless turned on).\n"
        "  measure ID [off]   Measure from that thing until turned off.\n"
        "  draw TOOL X,Y [X,Y…] [--text WORDS]\n"
        "                     Draw on the surface under the first point.\n"
        "  action ID [--target N]\n"
        "                     One of the floating bar's actions.\n"
        "  ask WORDS [--target N]\n"
        "                     The agent works on what's pointed at; the result is a\n"
        "                     preview on the overlay to keep or discard.\n"
        "  keep | discard     Keep or discard that preview.\n"
        "  lift [--target N] [--region X,Y,W,H] [--to overlay|desk|document] | lift cancel\n"
        "                     Turn what's pointed at (or a region of the screen) into\n"
        "                     editable shapes and text, in place: pages from the DOM,\n"
        "                     apps from the accessibility tree, else traced.\n"
        "  send <overlay|desk|document|source|agent> [--surface KEY] [--target N]\n"
        "       [--prompt WORDS]\n"
        "                     Where a surface's work goes; remembered per surface.\n"
        "  reset              End design mode, discard previews and lifts, clear the drawings\n"
        "                     and give the keyboard back. Also `omastrator reset` and Super+Alt+Escape.\n"
        "  undo | redo | clear KEY|all\n"
        "                     The overlay's history, and taking a surface's art away (all: every surface's).\n"
        "  onboarding [open|close|done|skip] | onboarding QUESTION VALUE…\n"
        "                     The few questions that tune the suggestions.\n"
        "  look [get|history|discard|save|panel [SECTION]|revert ID|KEY=VALUE…]\n"
        "                     Omarchy's own look: gapsIn, gapsOut, borderSize,\n"
        "                     rounding, activeBorder, inactiveBorder, barPosition,\n"
        "                     barTransparent, barHeight, barBackground, barText,\n"
        "                     font, textSize, wallpaper and colors.KEY. KEY=VALUE\n"
        "                     previews on the desktop; save asks first, with every\n"
        "                     file and command, and backs them up for revert.\n"
        "  restyle [--target N] [get|save|discard|KEY=VALUE…]\n"
        "                     Restyle the app pointed at through its toolkit:\n"
        "                     accent, background, foreground, font, fontSize,\n"
        "                     radius. KEY=VALUE opens a second copy to preview.\n"
        "  status             Print design mode's state as JSON.\n\n"
        "Also: omastrator desk [show|window|toggle|hide]\n"
        "      omastrator daemon [start|stop|status]\n");
}

int runDesign(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString verb = args.value(0);
    if (verb.isEmpty() || verb == QLatin1String("--help") || verb == QLatin1String("-h") || verb == QLatin1String("help")) {
        (verb.isEmpty() ? err : out) << designHelp();
        return verb.isEmpty() ? 1 : 0;
    }
    QStringList rest = args.mid(1);
    QJsonObject params{{"action", verb}};
    auto targetOption = [&]() {
        QString target;
        if (!option(rest, QStringLiteral("--target"), &target))
            return false;
        if (!target.isEmpty())
            params["target"] = target.toInt();
        return true;
    };
    if (verb == QLatin1String("on") || verb == QLatin1String("off") || verb == QLatin1String("toggle")) {
        QString monitor;
        if (!option(rest, QStringLiteral("--monitor"), &monitor))
            return failed(err, QStringLiteral("--monitor needs a name."));
        const bool on = verb == QLatin1String("toggle") ? Island::read().mode != QLatin1String("design") : verb == QLatin1String("on");
        // The island's mode is the switch; the background app follows it.
        if (const int code = Island::runCli({QStringLiteral("mode"), on ? QStringLiteral("design") : QStringLiteral("normal")}, out, err); code != 0)
            return code;
        if (!on)
            return 0;
        params["action"] = QStringLiteral("on");
        if (!monitor.isEmpty())
            params["monitor"] = monitor;
        return call(QStringLiteral("design"), params, out, err);
    }
    if (verb == QLatin1String("status"))
        return call(QStringLiteral("design"), params, out, err, true);
    if (verb == QLatin1String("tool")) {
        if (rest.size() != 1)
            return failed(err, QStringLiteral("Name one tool: point, inspect, pen, rectangle, ellipse, line, arrow, text or note."));
        params["tool"] = rest[0];
    } else if (verb == QLatin1String("follow")) {
        if (rest.size() != 1 || (rest[0] != QLatin1String("on") && rest[0] != QLatin1String("off")))
            return failed(err, QStringLiteral("Say on or off: omastrator design follow off keeps the bar with its app."));
        params["action"] = QStringLiteral("barFollowsFocus");
        params["on"] = rest[0] == QLatin1String("on");
    } else if (verb == QLatin1String("home")) {
        if (!targetOption())
            return failed(err, QStringLiteral("--target needs an id."));
    } else if (verb == QLatin1String("alt")) {
        params["on"] = rest.value(0) != QLatin1String("off");
    } else if (verb == QLatin1String("select")) {
        params["target"] = rest.value(0).toInt();
    } else if (verb == QLatin1String("measure")) {
        params["target"] = rest.value(0).toInt();
        params["on"] = rest.value(1) != QLatin1String("off");
    } else if (verb == QLatin1String("draw")) {
        QString text;
        if (!option(rest, QStringLiteral("--text"), &text))
            return failed(err, QStringLiteral("--text needs words."));
        if (rest.size() < 2)
            return failed(err, QStringLiteral("Say the tool and at least one point: omastrator design draw rectangle 10,10 200,120"));
        params["tool"] = rest.takeFirst();
        QJsonArray points;
        for (const QString &pair : rest) {
            const QStringList xy = pair.split(QLatin1Char(','));
            bool okX = false, okY = false;
            const double x = xy.value(0).toDouble(&okX), y = xy.value(1).toDouble(&okY);
            if (xy.size() != 2 || !okX || !okY)
                return failed(err, QStringLiteral("“%1” isn't a point. Write points as X,Y.").arg(pair));
            points.append(QJsonArray{x, y});
        }
        params["points"] = points;
        if (!text.isEmpty())
            params["text"] = text;
    } else if (verb == QLatin1String("action")) {
        if (!targetOption())
            return failed(err, QStringLiteral("--target needs an id."));
        if (rest.size() != 1)
            return failed(err, QStringLiteral("Name one action, such as: omastrator design action capture"));
        params["id"] = rest[0];
    } else if (verb == QLatin1String("lift")) {
        QString region, to;
        if (!targetOption() || !option(rest, QStringLiteral("--region"), &region) || !option(rest, QStringLiteral("--to"), &to))
            return failed(err, QStringLiteral("An option is missing its value."));
        if (rest.value(0) == QLatin1String("cancel"))
            params["cancel"] = true;
        if (!region.isEmpty()) {
            const QStringList parts = region.split(QLatin1Char(','));
            QJsonArray box;
            for (const QString &part : parts) {
                bool ok = false;
                box.append(part.trimmed().toInt(&ok));
                if (!ok)
                    return failed(err, QStringLiteral("Write the region as X,Y,W,H on screen."));
            }
            if (box.size() != 4)
                return failed(err, QStringLiteral("Write the region as X,Y,W,H on screen."));
            params["region"] = box;
        }
        if (!to.isEmpty())
            params["to"] = to;
    } else if (verb == QLatin1String("ask")) {
        if (!targetOption())
            return failed(err, QStringLiteral("--target needs an id."));
        params["prompt"] = rest.join(QLatin1Char(' '));
    } else if (verb == QLatin1String("send")) {
        QString surface, prompt;
        if (!targetOption() || !option(rest, QStringLiteral("--surface"), &surface) || !option(rest, QStringLiteral("--prompt"), &prompt))
            return failed(err, QStringLiteral("An option is missing its value."));
        if (rest.size() != 1)
            return failed(err, QStringLiteral("Send to one of: overlay, desk, document, source or agent."));
        params["destination"] = rest[0];
        if (!surface.isEmpty())
            params["surface"] = surface;
        if (!prompt.isEmpty())
            params["prompt"] = prompt;
    } else if (verb == QLatin1String("clear") || verb == QLatin1String("selectArt")) {
        if (rest.isEmpty())
            return failed(err, QStringLiteral("Name the surface's key, as `omastrator design status` shows it."));
        params["surface"] = rest[0];
    } else if (verb == QLatin1String("onboarding")) {
        const QString first = rest.value(0);
        if (first.isEmpty() || first == QLatin1String("open"))
            params["open"] = true;
        else if (first == QLatin1String("close"))
            params["open"] = false;
        else if (first == QLatin1String("done") || first == QLatin1String("skip"))
            params["finish"] = first == QLatin1String("done");
        else {
            params["question"] = first;
            params["values"] = QJsonArray::fromStringList(rest.mid(1));
        }
    } else if (verb == QLatin1String("look") || verb == QLatin1String("restyle")) {
        if (verb == QLatin1String("restyle") && !targetOption())
            return failed(err, QStringLiteral("--target needs an id."));
        // KEY=VALUE pairs are an edit to preview; a word is the operation.
        QJsonObject edits;
        for (const QString &word : rest) {
            const qsizetype equals = word.indexOf(QLatin1Char('='));
            if (equals <= 0) {
                if (params.contains(QLatin1String("op")))
                    params[verb == QLatin1String("look") && params["op"] == QLatin1String("revert") ? QStringLiteral("id") : QStringLiteral("section")] = word;
                else
                    params["op"] = word;
                continue;
            }
            const QString key = word.left(equals);
            const QString text = word.mid(equals + 1);
            bool number = false;
            const double value = text.toDouble(&number);
            const QJsonValue parsed = number ? QJsonValue(value)
                : text == QLatin1String("true") || text == QLatin1String("false") ? QJsonValue(text == QLatin1String("true"))
                : text.startsWith(QLatin1Char('{')) ? QJsonValue(QJsonDocument::fromJson(text.toUtf8()).object())
                                                    : QJsonValue(text);
            if (key.startsWith(QLatin1String("colors."))) {
                QJsonObject colours = edits["colors"].toObject();
                colours[key.mid(7)] = text;
                edits["colors"] = colours;
            } else {
                edits[key] = parsed;
            }
        }
        if (!edits.isEmpty()) {
            params["op"] = QStringLiteral("preview");
            params[verb == QLatin1String("look") ? QStringLiteral("edits") : QStringLiteral("style")] = edits;
        }
        return call(QStringLiteral("design"), params, out, err, params["op"].toString(QStringLiteral("get")) == QLatin1String("get"));
    } else if (!QStringList{"keep", "discard", "undo", "redo", "deselect"}.contains(verb)) {
        return failed(err, QStringLiteral("There is no design verb “%1”. Run `omastrator design --help`.").arg(verb));
    }
    return call(QStringLiteral("design"), params, out, err);
}

int runDesk(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString how = args.value(0, QStringLiteral("show"));
    if (how == QLatin1String("--help") || how == QLatin1String("-h")) {
        out << "Usage: omastrator desk [show|window|toggle|hide]\n\n"
               "The Desk: one canvas for everything sent from any surface. show opens it on\n"
               "its own Hyprland workspace, window as a normal window where you are.\n";
        return 0;
    }
    if (!QStringList{"show", "window", "toggle", "hide"}.contains(how))
        return failed(err, QStringLiteral("Choose show, window, toggle or hide."));
    return call(QStringLiteral("design"), {{"action", "desk"}, {"how", how}}, out, err);
}

int runReset(QTextStream &out, QTextStream &err)
{
    // The island and the keyboard first: they need nothing from the app.
    Island::State state = Island::read();
    state.mode = QStringLiteral("normal");
    state.expanded = false;
    Island::write(state);
    Island::resetKeys();
    // Pages' workspaces next: the file names them, so this needs no app either.
    const WorkspaceClaims::GiveBack workspaces = WorkspaceClaims::giveBackFromFile();
    if (workspaces.moved > 0)
        out << "Moved " << workspaces.moved << (workspaces.moved == 1 ? " window" : " windows") << " back to workspace " << workspaces.to << ".\n";
    if (Island::appIsRunning()) {
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("design"), {{"action", "reset"}}, 10'000);
        } catch (const AgentProtocol::Error &failure) {
            BrowserPoolState::endLeftover();
            return failed(err, QStringLiteral("The island is back to normal, but the app didn't answer: ") + failure.message());
        }
    }
    // The Browser View browser stops with the app's reset; one left by a crash, or by an app that didn't answer, is ended here.
    if (BrowserPoolState::endLeftover())
        out << "Stopped Omastrator's browser.\n";
    out << "Reset.\n";
    return 0;
}

int runDaemon(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString verb = args.value(0, QStringLiteral("start"));
    if (verb == QLatin1String("status")) {
        out << (Island::appIsRunning() ? "Omastrator is running.\n" : "Omastrator isn't running.\n");
        return 0;
    }
    if (verb == QLatin1String("start")) {
        const QString failure = Island::ensureAppRunning();
        return failure.isEmpty() ? 0 : failed(err, failure);
    }
    if (verb == QLatin1String("stop")) {
        if (!Island::appIsRunning())
            return 0;
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("quit_app"), {});
            return 0;
        } catch (const AgentProtocol::Error &failure) {
            return failed(err, failure.message());
        }
    }
    return failed(err, QStringLiteral("Usage: omastrator daemon [start|stop|status]"));
}
}
