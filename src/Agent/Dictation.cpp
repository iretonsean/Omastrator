#include "Agent/Dictation.h"
#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Island.h"
#include "Agent/Setup.h"
#include "Agent/Vocabulary.h"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <csignal>
#include <optional>

namespace {
// How long "Heard: …" waits for Esc before it runs, the first time a command is used.
constexpr int confirmMs = 2500;

QString tool(const char *variable, const char *fallback)
{
    const QString overridden = qEnvironmentVariable(variable);
    return overridden.isEmpty() ? QString::fromLatin1(fallback) : overridden;
}

QString statePath()
{
    return QDir(Island::runtimeDirectory()).filePath(QStringLiteral("dictation.json"));
}

QString seenPath()
{
    return QFileInfo(Island::seenPath()).dir().filePath(QStringLiteral("dictation-seen.json"));
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

void writeJson(const QString &path, const QJsonObject &json)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

bool alive(qint64 pid)
{
    return pid > 0 && ::kill(pid_t(pid), 0) == 0;
}

// ------------------------------------------------------------ words

const QHash<QString, int> units{{"zero", 0},     {"oh", 0},        {"one", 1},       {"two", 2},        {"three", 3},     {"four", 4},
                                {"five", 5},     {"six", 6},       {"seven", 7},     {"eight", 8},      {"nine", 9},      {"ten", 10},
                                {"eleven", 11},  {"twelve", 12},   {"thirteen", 13}, {"fourteen", 14},  {"fifteen", 15},  {"sixteen", 16},
                                {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}};
const QHash<QString, int> tens{{"twenty", 20}, {"thirty", 30}, {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"seventy", 70}, {"eighty", 80}, {"ninety", 90}};

// "ef", "bee", "6600", "six" → hex characters, or nullopt when the word isn't hex.
std::optional<QString> hexOf(const QString &word)
{
    static const QHash<QString, QString> letters{{"a", "a"},   {"ay", "a"},  {"b", "b"},   {"be", "b"},  {"bee", "b"}, {"c", "c"},
                                                 {"see", "c"}, {"sea", "c"}, {"cee", "c"}, {"d", "d"},   {"dee", "d"}, {"e", "e"},
                                                 {"f", "f"},   {"ef", "f"},  {"eff", "f"}};
    if (letters.contains(word))
        return letters.value(word);
    if (units.contains(word) && units.value(word) < 10)
        return QString::number(units.value(word));
    static const QRegularExpression hexadecimal(QStringLiteral("^[0-9a-f]+$"));
    if (hexadecimal.match(word).hasMatch())
        return word;
    return std::nullopt;
}

// Spoken hex after "hash" becomes "#rrggbb" when it spells 3, 6 or 8 digits.
QStringList joinHex(const QStringList &words)
{
    static const QStringList triggers{"hash", "hashtag", "pound", "hex", "hat", "hatch", "#"};
    QStringList out;
    for (qsizetype at = 0; at < words.size(); ++at) {
        if (!triggers.contains(words[at])) {
            out << words[at];
            continue;
        }
        QString digits;
        qsizetype next = at + 1;
        for (; next < words.size() && digits.size() < 8; ++next) {
            if (words[next] == QLatin1String("double") && next + 1 < words.size()) {
                if (const auto pair = hexOf(words[next + 1]); pair && pair->size() == 1) {
                    digits += *pair + *pair;
                    ++next;
                    continue;
                }
            }
            const auto chars = hexOf(words[next]);
            if (!chars)
                break;
            digits += *chars;
        }
        if (digits.size() == 3 || digits.size() == 6 || digits.size() == 8) {
            out << QLatin1Char('#') + digits;
            at = next - 1;
        } else {
            out << words[at];
        }
    }
    return out;
}

// "twenty four" → "24", "one hundred" → "100", "zero point five" → "0.5"; "six six" stays two numbers.
QStringList joinNumbers(const QStringList &words)
{
    auto isUnit = [](const QString &word) { return units.contains(word) && word != QLatin1String("oh"); };
    QStringList out;
    for (qsizetype at = 0; at < words.size();) {
        if (!isUnit(words[at]) && !tens.contains(words[at])) {
            out << words[at++];
            continue;
        }
        int value = 0;
        bool started = false;
        bool afterUnit = false;
        while (at < words.size()) {
            const QString &word = words[at];
            if (isUnit(word)) {
                const int unit = units.value(word);
                // A unit only finishes a round number: "twenty four", "one hundred five"; "six six" is two.
                if (started && (afterUnit || value % 10 != 0 || unit >= 10))
                    break;
                value += unit;
                afterUnit = true;
            } else if (tens.contains(word)) {
                if (started && value % 100 != 0)
                    break;
                value += tens.value(word);
                afterUnit = false;
            } else if (word == QLatin1String("hundred") && started && afterUnit && value > 0 && value < 10) {
                value *= 100;
                afterUnit = false;
            } else {
                break;
            }
            started = true;
            ++at;
        }
        QString number = QString::number(value);
        if (at + 1 < words.size() && words[at] == QLatin1String("point") && isUnit(words[at + 1]) && units.value(words[at + 1]) < 10) {
            number += QLatin1Char('.') + QString::number(units.value(words[at + 1]));
            at += 2;
        }
        out << number;
    }
    return out;
}

// ------------------------------------------------------------ grammar

struct ToolName {
    const char *spoken;
    const char *tool;
    const char *title;
};
constexpr ToolName toolNames[] = {
    {"direct selection", "directSelect", "Direct Selection"}, {"direct select", "directSelect", "Direct Selection"},
    {"white arrow", "directSelect", "Direct Selection"},     {"rounded rectangle", "roundedRectangle", "Rounded Rectangle"},
    {"line segment", "line", "Line Segment"},                {"selection", "select", "Selection"},
    {"select", "select", "Selection"},                       {"move", "select", "Selection"},
    {"arrow", "select", "Selection"},                        {"pen", "pen", "Pen"},
    {"pencil", "pencil", "Pencil"},                          {"type", "text", "Type"},
    {"text", "text", "Type"},                                {"line", "line", "Line Segment"},
    {"rectangle", "rectangle", "Rectangle"},                 {"rect", "rectangle", "Rectangle"},
    {"square", "rectangle", "Rectangle"},                    {"ellipse", "ellipse", "Ellipse"},
    {"circle", "ellipse", "Ellipse"},                        {"oval", "ellipse", "Ellipse"},
    {"polygon", "polygon", "Polygon"},                       {"star", "star", "Star"},
    {"rotate", "rotate", "Rotate"},                          {"scale", "scale", "Scale"},
    {"eyedropper", "eyedropper", "Eyedropper"},              {"hand", "hand", "Hand"},
    {"zoom", "zoom", "Zoom"}};

Dictation::Command command(const QString &kind, const QString &description, const QJsonObject &params)
{
    Dictation::Command made;
    made.tier = 1;
    made.kind = kind;
    made.description = description;
    made.method = QStringLiteral("command");
    made.params = params;
    made.params["name"] = kind;
    return made;
}

std::optional<QColor> colorOf(const QString &words)
{
    QString name = words.trimmed();
    name.remove(QLatin1Char(' '));
    if (name.startsWith(QLatin1Char('#')) || QColor::isValidColorName(name)) {
        const QColor color = QColor::fromString(name);
        if (color.isValid())
            return color;
    }
    return std::nullopt;
}
}

namespace Dictation {
QString normalize(const QString &heard)
{
    QString text = heard.toLower();
    text.replace(QStringLiteral("colour"), QStringLiteral("color"))
        .replace(QStringLiteral("centre"), QStringLiteral("center"))
        .replace(QStringLiteral("grey"), QStringLiteral("gray"));
    // Punctuation goes; a decimal point and a hash stay.
    static const QRegularExpression punctuation(QStringLiteral("[^a-z0-9#%.\\s-]|(?<![0-9])\\.|\\.(?![0-9])|-"));
    text.replace(punctuation, QStringLiteral(" "));
    static const QRegularExpression hashGlued(QStringLiteral("#(?=[a-z0-9])"));
    text.replace(hashGlued, QStringLiteral("# "));
    // Design terms Whisper splits or runs together.
    static const std::pair<const char *, const char *> corrections[] = {
        {"\\bpath finder\\b", "pathfinder"}, {"\\bminus fronts?\\b", "minus front"}, {"\\beye droppers?\\b", "eyedropper"},
        {"\\bhash tag\\b", "hashtag"},       {"\\bnumber sign\\b", "hash"},           {"\\bpercent sign\\b", "percent"},
        {"%", " percent"},                    {"\\bstrokes\\b", "stroke"},             {"\\bfills\\b", "fill"},
        {"\\bline weight\\b", "stroke weight"}, {"\\b(pt|pts)\\b", "points"},
        {"\\bpixels?\\b", "px"}};
    for (const auto &[pattern, replacement] : corrections)
        text.replace(QRegularExpression(QLatin1String(pattern)), QLatin1String(replacement));
    QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    // "hash" joins with what follows first, so its digits don't become a number.
    for (QString &word : words) {
        if (word == QLatin1String("#"))
            word = QStringLiteral("hash");
    }
    words = joinNumbers(joinHex(words));
    return words.join(QLatin1Char(' '));
}

bool isCancel(const QString &normalized)
{
    static const QRegularExpression cancel(QStringLiteral("^(cancel|cancel that|never ?mind|stop|forget it|no)$"));
    return cancel.match(normalized).hasMatch();
}

Command parse(const QString &normalized)
{
    QString text = normalized.trimmed();
    static const QRegularExpression polite(QStringLiteral("^((please|can you|could you|would you|hey omastrator|omastrator|now|ok|okay|and)\\s+)+"));
    static const QRegularExpression trailing(QStringLiteral("\\s+please$"));
    text.remove(polite);
    text.remove(trailing);
    Command none;
    if (text.isEmpty())
        return none;
    if (isCancel(text)) {
        none.kind = QStringLiteral("cancel");
        none.description = QStringLiteral("Cancel");
        return none;
    }
    auto matches = [&](const char *pattern) {
        return QRegularExpression(QStringLiteral("^(?:%1)$").arg(QLatin1String(pattern))).match(text);
    };
    if (matches("undo( that| it| the last thing)?").hasMatch())
        return command(QStringLiteral("undo"), QStringLiteral("Undo"), {});
    if (matches("redo( that| it)?").hasMatch())
        return command(QStringLiteral("redo"), QStringLiteral("Redo"), {});
    if (matches("zoom in( more)?").hasMatch())
        return command(QStringLiteral("zoomIn"), QStringLiteral("Zoom In"), {});
    if (matches("zoom out( more)?").hasMatch())
        return command(QStringLiteral("zoomOut"), QStringLiteral("Zoom Out"), {});
    if (matches("zoom to fit|fit( to)?( the)? (window|artboard|screen)|fit artboard( in window)?").hasMatch())
        return command(QStringLiteral("zoomToFit"), QStringLiteral("Fit Artboard in Window"), {});
    if (matches("actual size|zoom to 100( percent)?|100 percent( zoom)?").hasMatch())
        return command(QStringLiteral("actualSize"), QStringLiteral("Actual Size"), {});
    if (matches("select all( objects)?").hasMatch())
        return command(QStringLiteral("selectAll"), QStringLiteral("Select All"), {});
    if (matches("deselect( all)?|select none|clear( the)? selection").hasMatch())
        return command(QStringLiteral("deselect"), QStringLiteral("Deselect"), {});
    if (matches("group( them| these| those| it| the selection)?").hasMatch())
        return command(QStringLiteral("group"), QStringLiteral("Group"), {});
    if (matches("ungroup( them| these| it| that| the selection)?").hasMatch())
        return command(QStringLiteral("ungroup"), QStringLiteral("Ungroup"), {});
    if (matches("(delete|remove|erase)( it| that| this| them| these| the selection)?").hasMatch())
        return command(QStringLiteral("delete"), QStringLiteral("Delete"), {});
    if (matches("duplicate( it| that| this| them| these| the selection)?").hasMatch())
        return command(QStringLiteral("duplicate"), QStringLiteral("Duplicate"), {});
    if (matches("bring( it| this| them| the selection)? to( the)? front").hasMatch())
        return command(QStringLiteral("arrange"), QStringLiteral("Bring to Front"), {{"order", "bringToFront"}});
    if (matches("bring( it| this| them| the selection)? forward").hasMatch())
        return command(QStringLiteral("arrange"), QStringLiteral("Bring Forward"), {{"order", "bringForward"}});
    if (matches("send( it| this| them| the selection)? backwards?").hasMatch())
        return command(QStringLiteral("arrange"), QStringLiteral("Send Backward"), {{"order", "sendBackward"}});
    if (matches("send( it| this| them| the selection)? to( the)? back").hasMatch())
        return command(QStringLiteral("arrange"), QStringLiteral("Send to Back"), {{"order", "sendToBack"}});
    if (const auto align = matches("(?:align|line up)(?: it| them| these| the selection)?(?: to)?(?: the)? (.+?)(?: edges?)?( (?:to|on|with) the artboard)?");
        align.hasMatch()) {
        const QString where = align.captured(1);
        static const std::pair<const char *, const char *> edges[] = {
            {"^left$", "left"}, {"^right$", "right"}, {"^top$", "top"}, {"^bottom$", "bottom"},
            {"^(vertical center|vertical centers|middle|center vertically|centers vertically)$", "verticalCenter"},
            {"^(horizontal center|horizontal centers|center|centers|center horizontally|centers horizontally)$", "horizontalCenter"}};
        for (const auto &[pattern, edge] : edges) {
            if (QRegularExpression(QLatin1String(pattern)).match(where).hasMatch()) {
                QJsonObject params{{"edge", edge}};
                if (!align.captured(2).isEmpty())
                    params["target"] = QStringLiteral("artboard");
                static const QHash<QString, QString> titles{{"left", "Left"}, {"right", "Right"}, {"top", "Top"}, {"bottom", "Bottom"},
                                                            {"verticalCenter", "Vertical Centers"}, {"horizontalCenter", "Horizontal Centers"}};
                return command(QStringLiteral("align"), QStringLiteral("Align %1").arg(titles.value(QLatin1String(edge))), params);
            }
        }
    }
    if (const auto distribute = matches("(?:distribute|space)(?: them| these| the selection| out)?(?: evenly)? (horizontally|vertically)(?: evenly)?");
        distribute.hasMatch()) {
        const bool horizontal = distribute.captured(1) == QLatin1String("horizontally");
        return command(QStringLiteral("distribute"), horizontal ? QStringLiteral("Distribute Horizontally") : QStringLiteral("Distribute Vertically"),
                       {{"axis", horizontal ? "horizontal" : "vertical"}});
    }
    if (const auto weight = matches("(?:(?:set|make|change) )?(?:the )?stroke(?: weight| width)?(?: to)? (\\d+(?:\\.\\d+)?)(?: (?:points?|px))?");
        weight.hasMatch()) {
        const double width = weight.captured(1).toDouble();
        return command(QStringLiteral("strokeWidth"), QStringLiteral("Stroke Weight %1 pt").arg(width), {{"width", width}});
    }
    if (const auto opacity = matches("(?:(?:set|make|change) )?(?:the )?opacity(?: to)? (\\d+(?:\\.\\d+)?)(?: percent)?"); opacity.hasMatch()) {
        double value = opacity.captured(1).toDouble();
        if (value > 1)
            value /= 100;
        value = std::clamp(value, 0.0, 1.0);
        return command(QStringLiteral("opacity"), QStringLiteral("Opacity %1%").arg(qRound(value * 100)), {{"value", value}});
    }
    if (const auto paint = matches("(?:(?:set|make|change|color|turn|paint) )?(?:the )?(fill|stroke)(?: color)?(?: to| as)? (.+)"); paint.hasMatch()) {
        if (const auto color = colorOf(paint.captured(2))) {
            const bool fill = paint.captured(1) == QLatin1String("fill");
            return command(fill ? QStringLiteral("fill") : QStringLiteral("stroke"),
                           QStringLiteral("%1 %2").arg(fill ? QStringLiteral("Fill") : QStringLiteral("Stroke"), color->name()), {{"color", color->name()}});
        }
    }
    if (const auto paint = matches("(?:make|color|paint|turn|fill) (?:it|this|them|that|these)(?: to)? (.+)"); paint.hasMatch()) {
        if (const auto color = colorOf(paint.captured(1)))
            return command(QStringLiteral("fill"), QStringLiteral("Fill %1").arg(color->name()), {{"color", color->name()}});
    }
    // Tools last: "zoom in" and "select all" are commands, not tools.
    for (const ToolName &name : toolNames) {
        const QString spoken = QLatin1String(name.spoken);
        const QRegularExpression withTool(QStringLiteral("^(?:(?:select|choose|use|switch to|pick|grab|get|give me)\\s+)?(?:the\\s+)?%1 tool$").arg(spoken));
        const QRegularExpression withVerb(QStringLiteral("^(?:choose|use|switch to|pick|grab|give me)\\s+(?:the\\s+)?%1$").arg(spoken));
        if (withTool.match(text).hasMatch() || withVerb.match(text).hasMatch()) {
            Command tool;
            tool.tier = 1;
            tool.kind = QStringLiteral("tool");
            tool.description = QStringLiteral("%1 tool").arg(QLatin1String(name.title));
            tool.method = QStringLiteral("select_tool");
            tool.params = {{"tool", QLatin1String(name.tool)}};
            return tool;
        }
    }
    // Everything else is a request for the agent.
    Command request;
    request.tier = 2;
    request.kind = QStringLiteral("instruction");
    request.description = QStringLiteral("Ask the agent: %1").arg(text);
    request.method = QStringLiteral("ai_start");
    request.params = {{"flow", "edit"}, {"prompt", text}};
    return request;
}

QString voxtype()
{
    const QString program = tool("OMASTRATOR_VOXTYPE", "voxtype");
    const QString found = QStandardPaths::findExecutable(program);
    return !found.isEmpty() ? found : QFileInfo(program).isExecutable() ? program : QString();
}

QString missingVoxtype()
{
    return QStringLiteral("Dictation uses Omarchy's voxtype, which isn't installed. Install it with: omarchy voxtype install");
}

QString initialPrompt()
{
    // Whisper reads about 224 tokens of prompt; the vocabulary's first terms go first.
    QString prompt = QStringLiteral("Omastrator design commands, with hex colours spelled like hash F F six six zero zero: ");
    prompt += Vocabulary::load().join(QStringLiteral(", "));
    return prompt.left(800);
}

QString transcribe(const QString &wav, QString *error)
{
    const QString program = voxtype();
    if (program.isEmpty()) {
        *error = missingVoxtype();
        return {};
    }
    // voxtype reads the user's model settings and changes nothing; transcribe never types.
    QProcess process;
    process.start(program, {QStringLiteral("-q"), QStringLiteral("--initial-prompt"), initialPrompt(), QStringLiteral("transcribe"), wav});
    if (!process.waitForStarted(5000) || !process.waitForFinished(180'000) || process.exitCode() != 0) {
        *error = QStringLiteral("voxtype couldn't transcribe the recording: %1").arg(QString::fromUtf8(process.readAllStandardError()).trimmed().right(300));
        return {};
    }
    const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    error->clear();
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

int runCli(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString verb = args.value(0);
    auto failed = [&](const QString &message) {
        Island::setActivity(message, 6);
        err << message << '\n';
        return 1;
    };
    auto hyprSubmap = [](const QString &name) {
        // Esc cancels while "Heard: …" waits, through the keys setup wrote; without them, nothing happens.
        const QString hyprctl = tool("OMASTRATOR_HYPRCTL", "hyprctl");
        const bool lua = Setup::hyprFormat(Setup::Environment::current()) == Setup::HyprFormat::lua;
        QProcess::execute(hyprctl, lua ? QStringList{QStringLiteral("dispatch"), QStringLiteral("hl.dsp.submap(\"%1\")").arg(name)}
                                       : QStringList{QStringLiteral("dispatch"), QStringLiteral("submap"), name});
    };
    // Runs what was heard: at once when the kind has run before, else after a moment Esc can cancel.
    auto act = [&](const QString &heard) -> int {
        const QString normalized = normalize(heard);
        Command command = parse(normalized);
        if (command.kind == QLatin1String("cancel")) {
            QJsonObject state = readJson(statePath());
            state["cancelled"] = true;
            writeJson(statePath(), state);
            Island::setActivity(QStringLiteral("Cancelled."), 2);
            return 0;
        }
        if (command.tier == 0) {
            Island::setActivity(QStringLiteral("Didn't catch that."), 3);
            return 0;
        }
        // In Live mode, requests go to the page's own agent task.
        if (command.tier == 2 && Island::read().mode == QLatin1String("live")) {
            command.method = QStringLiteral("live");
            command.params = {{"action", "ask"}, {"prompt", normalized}};
        }
        const QJsonArray seen = readJson(seenPath())["kinds"].toArray();
        const bool immediate = command.tier == 1 && seen.contains(command.kind);
        const qint64 id = QDateTime::currentMSecsSinceEpoch();
        writeJson(statePath(), {{"state", "heard"}, {"id", id}, {"heard", heard.trimmed()}, {"description", command.description}, {"cancelled", false}});
        Island::setActivity(QStringLiteral("Heard: “%1” → %2%3").arg(heard.trimmed(), command.description, immediate ? QString() : QStringLiteral(" · Esc cancels")),
                            immediate ? 3 : confirmMs / 1000 + 2);
        out << command.description << '\n';
        out.flush();
        if (!immediate) {
            hyprSubmap(QStringLiteral("omastrator-heard"));
            for (int waited = 0; waited < confirmMs && !readJson(statePath())["cancelled"].toBool(); waited += 100)
                QThread::msleep(100);
            hyprSubmap(QStringLiteral("reset"));
            if (readJson(statePath())["cancelled"].toBool()) {
                writeJson(statePath(), {{"state", "idle"}});
                Island::setActivity(QStringLiteral("Cancelled."), 2);
                return 0;
            }
        }
        writeJson(statePath(), {{"state", "idle"}});
        if (const QString failure = Island::ensureAppRunning(); !failure.isEmpty())
            return failed(failure);
        try {
            AgentClient::Connection connection;
            connection.call(command.method, command.params);
        } catch (const AgentProtocol::Error &failure) {
            return failed(failure.message());
        }
        if (command.tier == 1 && !seen.contains(command.kind)) {
            QJsonArray kinds = seen;
            kinds.append(command.kind);
            writeJson(seenPath(), {{"kinds", kinds}});
        }
        return 0;
    };

    if (verb == QLatin1String("parse")) {
        const QString normalized = normalize(args.mid(1).join(QLatin1Char(' ')));
        const Command command = parse(normalized);
        out << QString::fromUtf8(QJsonDocument(QJsonObject{{"normalized", normalized}, {"tier", command.tier}, {"description", command.description},
                                                           {"method", command.method}, {"params", command.params}})
                                     .toJson(QJsonDocument::Compact))
            << '\n';
        return 0;
    }
    if (verb == QLatin1String("transcribe") || verb == QLatin1String("file")) {
        const QString wav = args.value(1);
        if (wav.isEmpty() || !QFileInfo::exists(wav))
            return failed(QStringLiteral("Name a WAV file (16 kHz, mono)."));
        QString error;
        const QString heard = transcribe(wav, &error);
        if (!error.isEmpty())
            return failed(error);
        if (verb == QLatin1String("transcribe")) {
            out << heard << '\n';
            return 0;
        }
        return act(heard);
    }
    if (verb == QLatin1String("start")) {
        if (voxtype().isEmpty())
            return failed(missingVoxtype());
        const QString recorder = tool("OMASTRATOR_PW_RECORD", "pw-record");
        if (QStandardPaths::findExecutable(recorder).isEmpty() && !QFileInfo(recorder).isExecutable())
            return failed(QStringLiteral("pw-record isn't installed, so there's nothing to record with. It comes with PipeWire."));
        const QJsonObject state = readJson(statePath());
        if (state["state"].toString() == QLatin1String("listening") && alive(qint64(state["pid"].toDouble())))
            return 0;
        const QString wav = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("dictation.wav"));
        QDir().mkpath(Island::runtimeDirectory());
        QFile::remove(wav);
        qint64 pid = 0;
        // A minute at most, in case the key's release never arrives.
        if (!QProcess::startDetached(QStringLiteral("timeout"),
                                     {QStringLiteral("-s"), QStringLiteral("INT"), QStringLiteral("60"), recorder, QStringLiteral("--rate"),
                                      QStringLiteral("16000"), QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"),
                                      QStringLiteral("s16"), wav},
                                     QString(), &pid))
            return failed(QStringLiteral("Couldn't start recording."));
        writeJson(statePath(), {{"state", "listening"}, {"pid", pid}, {"wav", wav}});
        Island::setActivity(QStringLiteral("Listening…"), 60);
        return 0;
    }
    if (verb == QLatin1String("stop")) {
        const QJsonObject state = readJson(statePath());
        if (state["state"].toString() != QLatin1String("listening"))
            return 0;
        const qint64 pid = qint64(state["pid"].toDouble());
        // SIGINT lets pw-record finish the WAV header.
        if (alive(pid))
            ::kill(pid_t(pid), SIGINT);
        for (int waited = 0; waited < 3000 && alive(pid); waited += 50)
            QThread::msleep(50);
        writeJson(statePath(), {{"state", "transcribing"}});
        Island::setActivity(QStringLiteral("Transcribing…"), 30);
        const QString wav = state["wav"].toString();
        QString error;
        const QString heard = QFileInfo(wav).size() > 44 ? transcribe(wav, &error) : QString();
        QFile::remove(wav);
        writeJson(statePath(), {{"state", "idle"}});
        if (!error.isEmpty())
            return failed(error);
        return act(heard);
    }
    if (verb == QLatin1String("cancel")) {
        QJsonObject state = readJson(statePath());
        if (state["state"].toString() == QLatin1String("listening")) {
            const qint64 pid = qint64(state["pid"].toDouble());
            if (alive(pid))
                ::kill(pid_t(pid), SIGINT);
            QFile::remove(state["wav"].toString());
            writeJson(statePath(), {{"state", "idle"}});
            Island::setActivity(QStringLiteral("Cancelled."), 2);
            return 0;
        }
        state["cancelled"] = true;
        writeJson(statePath(), state);
        return 0;
    }
    err << "Usage: omastrator island dictate <start|stop|cancel|file WAV|transcribe WAV|parse TEXT>\n";
    return 1;
}
}
