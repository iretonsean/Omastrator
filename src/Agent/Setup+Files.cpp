#include "Agent/Setup.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace {
const QByteArray menuBegin = "  // BEGIN omastrator setup";
const QByteArray menuEnd = "  // END omastrator setup";

QString luaString(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString jsonString(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// Whether the key in Lua's "SUPER + ALT + O" form is one setup leaves to the user.
bool skipping(const QStringList &skip, const QString &lua)
{
    return skip.contains(Setup::normalizeCombo(lua));
}

// The line at the top of a generated file naming the keys it leaves out, in `comment`'s style.
QString skippedNote(const QStringList &skip, const QString &comment)
{
    if (skip.isEmpty())
        return {};
    QStringList shown;
    for (const QString &key : skip)
        shown << Setup::displayCombo(key);
    return comment + QStringLiteral(" Not bound, because you already use them: %1.\n").arg(shown.join(QStringLiteral(", ")));
}

// Lines of `text`, each keeping no newline.
QList<QByteArray> lines(const QByteArray &text)
{
    QList<QByteArray> result = text.split('\n');
    if (!result.isEmpty() && result.last().isEmpty())
        result.removeLast();
    return result;
}
}

namespace Setup {
QString shellQuote(const QString &word)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./+:@%-]+$"));
    if (safe.match(word).hasMatch())
        return word;
    QString quoted = word;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

DesignKeys DesignKeys::from(const Environment &environment)
{
    DesignKeys keys;
    QFile file(QDir(environment.omastratorConfig()).filePath(QStringLiteral("anywhere.json")));
    if (!file.open(QIODevice::ReadOnly))
        return keys;
    const QJsonObject chosen = QJsonDocument::fromJson(file.readAll()).object()["keys"].toObject();
    // Only what reads as a key combination, so a typo can't break the Hyprland file.
    static const QRegularExpression combination(QStringLiteral("^[A-Za-z0-9_ +:]+$"));
    if (combination.match(chosen["design"].toString()).hasMatch())
        keys.design = chosen["design"].toString().trimmed();
    if (combination.match(chosen["desk"].toString()).hasMatch())
        keys.desk = chosen["desk"].toString().trimmed();
    return keys;
}

// "SUPER + ALT + O" as hyprlang writes it: "SUPER ALT, O".
static QString confKey(const QString &luaKey)
{
    QStringList parts = luaKey.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (QString &part : parts)
        part = part.trimmed();
    const QString key = parts.isEmpty() ? QString() : parts.takeLast();
    return parts.join(QLatin1Char(' ')) + QStringLiteral(", ") + key;
}

static QString designLua(const DesignKeys &keys, const QStringList &skip)
{
    QString text = QStringLiteral(
               "\n-- Design mode everywhere (docs/ANYWHERE.md): %1 inspects, measures and draws on any\n"
               "-- window or page; clicks still reach the apps. Hold Alt to measure; Escape leaves.\n"
               "-- %2 opens the Desk on its own workspace. Remap both with \"keys\" in\n"
               "-- ~/.config/omastrator/anywhere.json, then run `omastrator setup` again.\n"
               "local function design(args)\n"
               "  return hl.dsp.exec_cmd(omastrator .. \" design \" .. args)\n"
               "end\n"
               "\n"
               "local function leaveDesign()\n"
               "  run(hl.dsp.submap(\"reset\"))\n"
               "  run(design(\"off\"))\n"
               "end\n"
               "\n").arg(keys.design, keys.desk);
    if (!skipping(skip, keys.design))
        text += QStringLiteral(
                    "bind(\"%1\", function()\n"
                    "  run(design(\"on\"))\n"
                    "  run(hl.dsp.submap(\"omastrator-design\"))\n"
                    "end, { description = \"Omastrator: design mode\" })\n").arg(keys.design);
    if (!skipping(skip, keys.desk))
        text += QStringLiteral("bind(\"%1\", hl.dsp.exec_cmd(omastrator .. \" desk toggle\"), { description = \"Omastrator: the Desk\" })\n").arg(keys.desk);
    if (!skipping(skip, QStringLiteral("SUPER + ALT + Escape")))
        text += QStringLiteral(
            "-- The escape hatch: gives the keyboard back, ends design mode and drops previews. It works inside any\n"
            "-- submap (submap_universal) and closes the submap itself, so it needs neither the app nor the shell.\n"
            "bind(\"SUPER + ALT + Escape\", function()\n"
            "  run(hl.dsp.submap(\"reset\"))\n"
            "  run(hl.dsp.exec_cmd(omastrator .. \" reset\"))\n"
            "end, { description = \"Omastrator: reset\", submap_universal = true })\n");
    text += QStringLiteral(
               "submap(\"omastrator-design\", function()\n"
               "  bind(\"Escape\", leaveDesign, { description = \"Leave design mode\" })\n"
               "  bind(\"%1\", leaveDesign, { description = \"Leave design mode\" })\n"
               "  bind(\"Alt_L\", design(\"alt on\"), { description = \"Measure (hold)\" })\n"
               "  bind(\"ALT + Alt_L\", design(\"alt off\"), { release = true, description = \"Measure (release)\" })\n"
               "end)\n"
               "\n"
               "-- Omastrator in the background: the overlays, the Desk and the agent socket, no window until asked.\n"
               "attempt(\"hyprland.start\", hl.on, \"hyprland.start\", function()\n"
               "  hl.exec_cmd(omastrator .. \" --daemon\")\n"
               "end)\n"
               "\n"
               "loading = false\n"
               "if #failures > 0 then\n"
               "  notify(\"Omastrator: \" .. #failures .. \" of its keys didn't load (details in ~/.local/state/omastrator/setup.log). First: \" .. failures[1])\n"
               "end\n").arg(keys.design);
    return text;
}

QByteArray hyprlandLua(const QString &command, const DesignKeys &keys, const QStringList &skip)
{
    QString text = QStringLiteral(
        "-- Omastrator's keys, written by `omastrator setup` (docs/OS-SUITE.md): design mode, the Desk,\n"
        "-- dictation and the reset key. While design mode's keys are held, other shortcuts wait\n"
        "-- until Escape; unbound keys still reach apps.\n"
        "-- `omastrator setup` rewrites this file; `omastrator setup --remove` deletes it.\n"
        "%2"
        "\n"
        "local omastrator = %1\n"
        "\n"
        "-- A failure in this file is written to ~/.local/state/omastrator/setup.log and shown as one\n"
        "-- notification once the file has loaded. It never stops the lines after it or the user's own config.\n"
        "local failures, loading = {}, true\n"
        "\n"
        "local function notify(message)\n"
        "  pcall(hl.notification.create, { text = message, timeout = 10000 })\n"
        "end\n"
        "\n"
        "local function report(what, err)\n"
        "  local message = \"Omastrator: \" .. what .. \": \" .. tostring(err)\n"
        "  pcall(function()\n"
        "    local state = os.getenv(\"XDG_STATE_HOME\") or ((os.getenv(\"HOME\") or \"\") .. \"/.local/state\")\n"
        "    local path = state .. \"/omastrator/setup.log\"\n"
        "    local log = io.open(path, \"a\")\n"
        "    -- Every reload appends, so start over once it is large.\n"
        "    if log and log:seek(\"end\") > 65536 then log:close(); log = io.open(path, \"w\") end\n"
        "    if log then\n"
        "      log:write(os.date(\"%Y-%m-%d %H:%M:%S \"), message, \"\\n\")\n"
        "      log:close()\n"
        "    end\n"
        "  end)\n"
        "  if loading then failures[#failures + 1] = message else notify(message) end\n"
        "end\n"
        "\n"
        "local function attempt(what, fn, ...)\n"
        "  local ok, err = pcall(fn, ...)\n"
        "  if not ok then report(what, err) end\n"
        "  return ok\n"
        "end\n"
        "\n"
        "local function bind(keys, action, options)\n"
        "  attempt(\"couldn't bind \" .. keys, hl.bind, keys, action, options)\n"
        "end\n"
        "\n"
        "-- Hyprland closes a submap when its body ends or fails; the body is guarded too so the failure is reported.\n"
        "local function submap(name, body)\n"
        "  attempt(\"submap \" .. name, hl.define_submap, name, function()\n"
        "    attempt(\"submap \" .. name, body)\n"
        "  end)\n"
        "end\n"
        "\n"
        "local function run(action)\n"
        "  attempt(\"a key failed\", hl.dispatch, action)\n"
        "end\n"
        "\n"
        "local function island(args)\n"
        "  return hl.dsp.exec_cmd(omastrator .. \" island \" .. args)\n"
        "end\n"
        "\n"
        "-- Hands the keyboard back first, so a failing command can't leave the submap's keys held; then runs one command.\n"
        "local function leave(args)\n"
        "  return function()\n"
        "    run(hl.dsp.submap(\"reset\"))\n"
        "    run(island(args))\n"
        "  end\n"
        "end\n"
        "\n").arg(luaString(command), skippedNote(skip, QStringLiteral("--")));
    text += QStringLiteral("\n-- Dictation: hold Super+Alt+V and speak; release to hear it back. Esc cancels while it waits.\n");
    if (!skipping(skip, QStringLiteral("SUPER + ALT + V")))
        text += QStringLiteral(
            "bind(\"SUPER + ALT + V\", island(\"dictate start\"), { description = \"Omastrator: dictate (hold)\" })\n"
            "bind(\"SUPER + ALT + V\", island(\"dictate stop\"), { release = true, description = \"Omastrator: dictate (release)\" })\n");
    text += QStringLiteral(
        "submap(\"omastrator-heard\", function()\n"
        "  bind(\"Escape\", leave(\"dictate cancel\"), { description = \"Cancel what was heard\" })\n"
        "end)\n");
    text += designLua(keys, skip);
    return text.toUtf8();
}

QByteArray hyprlandConf(const QString &command, const DesignKeys &keys, const QStringList &skip)
{
    const QString island = shellQuote(command) + QStringLiteral(" island ");
    QString text = QStringLiteral(
        "# Omastrator's keys, written by `omastrator setup` (docs/OS-SUITE.md): design mode, the Desk, dictation and the reset key.\n"
        "# Every submap ends with `submap = reset`. This first line makes sure nothing\n"
        "# above this file (an unclosed submap in the user's own config) holds the binds below.\n"
        "submap = reset\n");
    text += skippedNote(skip, QStringLiteral("#")) + QLatin1Char('\n');
    auto back = [&](const QString &key, const QString &args) {
        return QStringLiteral("bind = , %1, exec, %2%3\nbind = , %1, submap, reset\n").arg(key, island, args);
    };
    text += QLatin1Char('\n');
    if (!skipping(skip, QStringLiteral("SUPER + ALT + V")))
        text += QStringLiteral("bindd = SUPER ALT, V, Omastrator: dictate (hold), exec, %1dictate start\n"
                               "bindr = SUPER ALT, V, exec, %1dictate stop\n\n").arg(island);
    text += QStringLiteral("submap = omastrator-heard\n")
            + back(QStringLiteral("escape"), QStringLiteral("dictate cancel")) + QStringLiteral("submap = reset\n");
    // Design mode everywhere: its key, Escape and Alt inside, the Desk, and the background app.
    const QString design = shellQuote(command) + QStringLiteral(" design ");
    text += QLatin1Char('\n');
    if (!skipping(skip, keys.design))
        text += QStringLiteral("bindd = %1, Omastrator: design mode, exec, %2on\n"
                               "bind = %1, submap, omastrator-design\n").arg(confKey(keys.design), design);
    if (!skipping(skip, keys.desk))
        text += QStringLiteral("bindd = %1, Omastrator: the Desk, exec, %2 desk toggle\n").arg(confKey(keys.desk), shellQuote(command));
    if (!skipping(skip, QStringLiteral("SUPER + ALT + Escape")))
        // The u flag (submap universal) keeps the hatch working inside a submap; it closes the submap itself.
        // Both binds carry the description, so a check of Hyprland's binds can tell they are ours.
        text += QStringLiteral("binddu = SUPER ALT, escape, Omastrator: reset, exec, %1 reset\n"
                               "binddu = SUPER ALT, escape, Omastrator: reset, submap, reset\n").arg(shellQuote(command));
    text += QStringLiteral("\nsubmap = omastrator-design\n"
                           "bind = , escape, exec, %2off\n"
                           "bind = , escape, submap, reset\n"
                           "bind = %1, exec, %2off\n"
                           "bind = %1, submap, reset\n"
                           "bind = , Alt_L, exec, %2alt on\n"
                           "bindr = ALT, Alt_L, exec, %2alt off\n"
                           "submap = reset\n"
                           "\nexec-once = %3 --daemon\n")
                .arg(confKey(keys.design), design, shellQuote(command));
    return text.toUtf8();
}

QByteArray sourceBlock(HyprFormat format)
{
    if (format == HyprFormat::lua)
        return "\n-- Omastrator's keys (added by `omastrator setup --apply`).\n"
               "do\n"
               "  local ok, err = pcall(dofile, (os.getenv(\"XDG_CONFIG_HOME\") or (os.getenv(\"HOME\") .. \"/.config\")) .. \"/omastrator/hyprland.lua\")\n"
               "  if not ok then pcall(hl.notification.create, { text = \"Omastrator: its key file didn't load: \" .. tostring(err), timeout = 10000 }) end\n"
               "end\n";
    return "\n# Omastrator's keys (added by `omastrator setup --apply`).\n"
           "source = ~/.config/omastrator/hyprland.conf\n";
}

QByteArray menuBlock(const QString &command)
{
    const QString island = shellQuote(command) + QStringLiteral(" island ");
    struct Entry {
        const char *id;
        const char *icon;
        const char *label;
        const char *args;
        const char *description;
    };
    // Icons are Nerd Font glyphs, as Omarchy's own menu uses.
    static const Entry entries[] = {
        {"omastrator", "\U000F03D8", "Omastrator", nullptr, "Vector illustration, and its desktop tools"},
        {"omastrator.new", "", "New Document", "new", nullptr},
        {"omastrator.design", "\U000F0E0C", "Design Mode", "mode design", "Inspect, measure and draw on any window or page"},
        {"omastrator.desk", "\U000F0E0C", "The Desk", "@desk window", "Everything sent from any surface, on one canvas"},
        {"omastrator.capture", "", "Capture", nullptr, nullptr},
        {"omastrator.capture.fill", "\U000F00C9", "Pick Colour for Fill", "capture color fill", nullptr},
        {"omastrator.capture.stroke", "\U000F00C9", "Pick Colour for Stroke", "capture color stroke", nullptr},
        {"omastrator.capture.swatch", "\U000F00C9", "Pick Colour as a Swatch", "capture color swatch", nullptr},
        {"omastrator.capture.screenshot", "", "Screenshot Region", "capture screenshot", "Open it in Omastrator and trace it"},
        {"omastrator.capture.window", "\U000F0379", "Capture Window", "capture window", "The focused app, to redesign in Omastrator"},
        {"omastrator.capture.paste", "", "Paste SVG", "capture paste-svg", "The clipboard's SVG as editable paths"},
        {"omastrator.capture.theme", "\U000F0E0C", "Theme Swatches", "capture theme-swatches", "The Omarchy theme's colours as a swatch group"},
        {"omastrator.generate", "\U000F16A4", "Generate…", "ai generate", "Describe it; your agent draws variations"},
        {"omastrator.roast", "\uF06D", "Roast My Design", "ai roast", nullptr},
        {"omastrator.handoff", "\U000F06A9", "Hand to Agent…", "ai handoff", "The document as a mockup, for an app's source"},
        {"omastrator.connect", "\U000F06A9", "Connect an Agent", "show connect-agent", "Drive Omastrator from any agent"},
    };
    QByteArray block = menuBegin + ": `omastrator setup --remove` takes these out.\n";
    for (const Entry &entry : entries) {
        QString line = QStringLiteral("  \"%1\": {\"icon\": \"%2\", \"label\": \"%3\"").arg(QLatin1String(entry.id), QString::fromUtf8(entry.icon), QString::fromUtf8(entry.label));
        if (entry.description)
            line += QStringLiteral(", \"description\": %1").arg(jsonString(QString::fromUtf8(entry.description)));
        if (entry.args)
            // "@…" is a command of its own rather than an island verb.
            line += QStringLiteral(", \"action\": %1")
                        .arg(jsonString(entry.args[0] == '@' ? shellQuote(command) + QLatin1Char(' ') + QLatin1String(entry.args + 1)
                                                               : island + QLatin1String(entry.args)));
        block += line.toUtf8() + "},\n";
    }
    block += menuEnd + "\n";
    return block;
}

QByteArray withMenuBlock(const QByteArray &current, const QByteArray &block, bool *addedComma)
{
    *addedComma = false;
    if (current.trimmed().isEmpty())
        return "{\n" + block + "}\n";
    const qsizetype begin = current.indexOf(menuBegin);
    if (begin >= 0) {
        const qsizetype end = current.indexOf(menuEnd, begin);
        if (end >= 0) {
            const qsizetype after = current.indexOf('\n', end);
            QByteArray replaced = current;
            replaced.replace(begin, (after < 0 ? current.size() : after + 1) - begin, block);
            return replaced;
        }
    }
    const qsizetype close = current.lastIndexOf('}');
    if (close < 0)
        return current;
    // The last entry before the closing brace needs a comma once more follow it.
    qsizetype last = close - 1;
    while (last >= 0) {
        const char c = current[last];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            --last;
            continue;
        }
        // Skip a trailing // comment line.
        const qsizetype lineStart = current.lastIndexOf('\n', last) + 1;
        const QByteArray line = current.mid(lineStart, last - lineStart + 1).trimmed();
        if (line.startsWith("//")) {
            last = lineStart - 1;
            continue;
        }
        break;
    }
    QByteArray result = current;
    qsizetype insertAt = close;
    if (last >= 0 && result[last] != '{' && result[last] != ',') {
        result.insert(last + 1, ',');
        *addedComma = true;
        ++insertAt;
    }
    // The block starts on a line of its own.
    const qsizetype lineStart = result.lastIndexOf('\n', insertAt - 1) + 1;
    if (result.mid(lineStart, insertAt - lineStart).trimmed().isEmpty())
        insertAt = lineStart;
    else
        result.insert(insertAt++, '\n');
    result.insert(insertAt, block);
    return result;
}

QByteArray withoutMenuBlock(const QByteArray &current, bool removeComma)
{
    const qsizetype begin = current.indexOf(menuBegin);
    const qsizetype end = begin < 0 ? -1 : current.indexOf(menuEnd, begin);
    if (end < 0)
        return current;
    const qsizetype after = current.indexOf('\n', end);
    QByteArray result = current;
    result.remove(begin, (after < 0 ? current.size() : after + 1) - begin);
    if (removeComma) {
        // The comma setup added sits right before the block, after the last entry.
        qsizetype at = begin - 1;
        while (at >= 0 && (result[at] == ' ' || result[at] == '\t' || result[at] == '\n' || result[at] == '\r'))
            --at;
        if (at >= 0 && result[at] == ',')
            result.remove(at, 1);
    }
    return result;
}

// jq writes "\u2014" as "—"; a line that only differs from the file's own by such escapes keeps the file's bytes.
QByteArray keepEscapes(const QByteArray &original, const QByteArray &edited)
{
    auto decoded = [](const QByteArray &line) {
        static const QRegularExpression escape(QStringLiteral("\\\\u([0-9a-fA-F]{4})"));
        QString text = QString::fromUtf8(line);
        QString out;
        qsizetype last = 0;
        for (auto match = escape.globalMatch(text); match.hasNext();) {
            const auto found = match.next();
            // An escaped backslash before it means it isn't an escape.
            qsizetype slashes = 0;
            for (qsizetype at = found.capturedStart() - 1; at >= 0 && text[at] == QLatin1Char('\\'); --at)
                ++slashes;
            if (slashes % 2)
                continue;
            out += text.mid(last, found.capturedStart() - last) + QChar(char16_t(found.captured(1).toUShort(nullptr, 16)));
            last = found.capturedEnd();
        }
        return out + text.mid(last);
    };
    QHash<QString, QByteArray> originals;
    for (const QByteArray &line : original.split('\n'))
        if (line.contains("\\u"))
            originals.insert(decoded(line), line);
    if (originals.isEmpty())
        return edited;
    QList<QByteArray> lines = edited.split('\n');
    for (QByteArray &line : lines) {
        if (const auto found = originals.constFind(QString::fromUtf8(line)); found != originals.constEnd())
            line = found.value();
    }
    return lines.join('\n');
}

std::optional<QByteArray> jq(const QByteArray &input, const QString &filter, QString *error)
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("jq"));
    QString ignored;
    if (!error)
        error = &ignored;
    if (program.isEmpty()) {
        *error = QStringLiteral("jq isn't installed, and setup edits shell.json with it. Install it with: sudo pacman -S jq");
        return std::nullopt;
    }
    QProcess process;
    process.start(program, {filter});
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10'000) || process.exitCode() != 0) {
        *error = QStringLiteral("jq could not edit shell.json: %1").arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
        return std::nullopt;
    }
    return keepEscapes(input, process.readAllStandardOutput());
}

QString unifiedDiff(const QString &path, const std::optional<QByteArray> &before, const std::optional<QByteArray> &after)
{
    const QList<QByteArray> a = lines(before.value_or(QByteArray()));
    const QList<QByteArray> b = lines(after.value_or(QByteArray()));
    // Longest common subsequence, from the end, so the walk below goes forward.
    const qsizetype n = a.size(), m = b.size();
    std::vector<std::vector<int>> common(size_t(n + 1), std::vector<int>(size_t(m + 1), 0));
    for (qsizetype i = n - 1; i >= 0; --i)
        for (qsizetype j = m - 1; j >= 0; --j)
            common[size_t(i)][size_t(j)] = a[i] == b[j] ? common[size_t(i + 1)][size_t(j + 1)] + 1
                                                        : std::max(common[size_t(i + 1)][size_t(j)], common[size_t(i)][size_t(j + 1)]);
    struct Line {
        char kind;
        QByteArray text;
        qsizetype oldLine, newLine;
    };
    std::vector<Line> script;
    qsizetype i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            script.push_back({' ', a[i], i, j});
            ++i, ++j;
        } else if (i < n && (j == m || common[size_t(i + 1)][size_t(j)] >= common[size_t(i)][size_t(j + 1)])) {
            script.push_back({'-', a[i], i, j});
            ++i;
        } else {
            script.push_back({'+', b[j], i, j});
            ++j;
        }
    }
    const QString none = QStringLiteral("/dev/null");
    QString text = QStringLiteral("--- %1\n+++ %2\n").arg(before ? path : none, after ? path : none);
    constexpr qsizetype context = 3;
    size_t at = 0;
    while (at < script.size()) {
        if (script[at].kind == ' ') {
            ++at;
            continue;
        }
        // One hunk: the change, its neighbours within the context, and the context itself.
        size_t start = at >= size_t(context) ? at - size_t(context) : 0;
        while (start < at && script[start].kind != ' ')
            ++start;
        size_t end = at;
        size_t quiet = 0;
        while (end < script.size() && quiet <= size_t(context * 2)) {
            quiet = script[end].kind == ' ' ? quiet + 1 : 0;
            ++end;
        }
        end = std::min(script.size(), end - (quiet > size_t(context) ? quiet - size_t(context) : 0));
        qsizetype oldCount = 0, newCount = 0;
        for (size_t k = start; k < end; ++k) {
            oldCount += script[k].kind != '+';
            newCount += script[k].kind != '-';
        }
        text += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
                    .arg(oldCount ? script[start].oldLine + 1 : script[start].oldLine)
                    .arg(oldCount)
                    .arg(newCount ? script[start].newLine + 1 : script[start].newLine)
                    .arg(newCount);
        for (size_t k = start; k < end; ++k)
            text += QLatin1Char(script[k].kind) + QString::fromUtf8(script[k].text) + QLatin1Char('\n');
        at = end;
    }
    return text;
}

QStringList missingTools()
{
    struct Tool {
        const char *program;
        const char *install;
    };
    static const Tool tools[] = {{"hyprpicker", "sudo pacman -S hyprpicker"}, {"grim", "sudo pacman -S grim"},
                                 {"slurp", "sudo pacman -S slurp"},           {"wl-paste", "sudo pacman -S wl-clipboard"},
                                 {"chromium", "sudo pacman -S chromium"},     {"voxtype", "omarchy voxtype install"},
                                 {"jq", "sudo pacman -S jq"}};
    QStringList missing;
    for (const Tool &tool : tools) {
        if (QStandardPaths::findExecutable(QLatin1String(tool.program)).isEmpty())
            missing << QStringLiteral("%1 (%2)").arg(QLatin1String(tool.program), QLatin1String(tool.install));
    }
    return missing;
}
}
