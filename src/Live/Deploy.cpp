#include "Live/Deploy.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <map>

namespace {
QString home(const char *variable, const char *fallback)
{
    const QString given = qEnvironmentVariable(variable);
    return given.isEmpty() ? QDir::home().filePath(QLatin1String(fallback)) : given;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
    return {};
}

QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? QDir::cleanPath(folder) : resolved;
}

bool installed(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

// The value of one quoted dotenv value starting at `at` (the quote); `at` ends after the closing quote.
QString quoted(const QString &text, qsizetype &at)
{
    const QChar quote = text[at++];
    QString value;
    while (at < text.size() && text[at] != quote) {
        if (quote == QLatin1Char('"') && text[at] == QLatin1Char('\\') && at + 1 < text.size()) {
            const QChar next = text[++at];
            value += next == QLatin1Char('n') ? QLatin1Char('\n') : next == QLatin1Char('r') ? QLatin1Char('\r') : next == QLatin1Char('t') ? QLatin1Char('\t') : next;
        } else {
            value += text[at];
        }
        ++at;
    }
    ++at;
    return value;
}

const QStringList dryLines{
    QStringLiteral("It worked on localhost, which the client will find less reassuring than you do."),
    QStringLiteral("Somewhere, a stakeholder is refreshing the page."),
    QStringLiteral("The client has already been told it's live."),
};
}

namespace Deploy {
Variables parseEnv(const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression key(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    std::map<QString, qsizetype> index;
    Variables result;
    qsizetype at = 0;
    while (at < text.size()) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), at);
        if (end < 0)
            end = text.size();
        qsizetype lineStart = at;
        while (lineStart < end && text[lineStart].isSpace())
            ++lineStart;
        const QString line = text.mid(lineStart, end - lineStart).trimmed();
        at = end + 1;
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        qsizetype skip = 0;
        if (line.startsWith(QLatin1String("export ")))
            skip = 7;
        const qsizetype equals = line.indexOf(QLatin1Char('='), skip);
        if (equals < 0)
            continue;
        const QString name = line.mid(skip, equals - skip).trimmed();
        if (!key.match(name).hasMatch())
            continue;
        // The value may be quoted across lines, so it is read from the text itself.
        qsizetype valueAt = lineStart + equals + 1;
        while (valueAt < text.size() && (text[valueAt] == QLatin1Char(' ') || text[valueAt] == QLatin1Char('\t')))
            ++valueAt;
        QString value;
        if (valueAt < text.size() && (text[valueAt] == QLatin1Char('"') || text[valueAt] == QLatin1Char('\'') || text[valueAt] == QLatin1Char('`'))) {
            value = quoted(text, valueAt);
            const qsizetype next = text.indexOf(QLatin1Char('\n'), valueAt);
            at = next < 0 ? text.size() : next + 1;
        } else {
            value = line.mid(equals + 1);
            static const QRegularExpression comment(QStringLiteral("\\s+#.*$"));
            value.remove(comment);
            value = value.trimmed();
        }
        if (const auto found = index.find(name); found != index.end()) {
            result[found->second].second = value;
        } else {
            index[name] = qsizetype(result.size());
            result.emplace_back(name, value);
        }
    }
    return result;
}

Variables projectEnv(const QString &folder, const QString &cwd, QStringList *files)
{
    QStringList folders{folder};
    if (!cwd.isEmpty() && canonical(cwd) != canonical(folder))
        folders << cwd;
    Variables merged;
    for (const QString &where : std::as_const(folders)) {
        for (const char *name : {".env", ".env.local", ".env.production", ".env.production.local"}) {
            const QString path = QDir(where).filePath(QLatin1String(name));
            if (!QFileInfo(path).isFile())
                continue;
            if (files)
                *files << path;
            for (const auto &[key, value] : parseEnv(readFile(path))) {
                const auto existing = std::find_if(merged.begin(), merged.end(), [&](const auto &each) { return each.first == key; });
                if (existing != merged.end())
                    existing->second = value;
                else
                    merged.emplace_back(key, value);
            }
        }
    }
    return merged;
}

QProcessEnvironment environment(const Variables &variables)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto &[key, value] : variables) {
        // The project's files decide everything but where programs and home are.
        if (key != QLatin1String("PATH") && key != QLatin1String("HOME"))
            environment.insert(key, value);
    }
    return environment;
}

QStringList keys(const Variables &variables)
{
    QStringList names;
    for (const auto &[key, value] : variables)
        names << key;
    return names;
}

QString redact(QString text, const Variables &variables)
{
    // Longest first, so a value that contains another is replaced whole.
    Variables sorted = variables;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second.size() > b.second.size(); });
    for (const auto &[key, value] : sorted) {
        if (value.trimmed().size() >= 4)
            text.replace(value, QStringLiteral("[%1]").arg(key));
    }
    return text;
}

Command resolve(const QString &folder)
{
    const QDir dir(folder);
    const QJsonObject config = readObject(dir.filePath(QStringLiteral("omastrator.json")))["deploy"].toObject();
    if (const QString command = config["command"].toString().trimmed(); !command.isEmpty()) {
        const QString cwd = config["cwd"].toString().trimmed();
        return {command, cwd.isEmpty() ? folder : QDir::cleanPath(dir.filePath(cwd)), QStringLiteral("omastrator.json")};
    }
    const QJsonObject scripts = readObject(dir.filePath(QStringLiteral("package.json")))["scripts"].toObject();
    for (const char *script : {"deploy", "deploy:prod"}) {
        if (scripts.contains(QLatin1String(script))) {
            QString manager = QStringLiteral("npm");
            if (dir.exists(QStringLiteral("pnpm-lock.yaml")))
                manager = QStringLiteral("pnpm");
            else if (dir.exists(QStringLiteral("yarn.lock")))
                manager = QStringLiteral("yarn");
            else if (dir.exists(QStringLiteral("bun.lockb")) || dir.exists(QStringLiteral("bun.lock")))
                manager = QStringLiteral("bun");
            return {QStringLiteral("%1 run %2").arg(manager, QLatin1String(script)), folder, QStringLiteral("package.json")};
        }
    }
    if ((dir.exists(QStringLiteral(".vercel/project.json")) || dir.exists(QStringLiteral("vercel.json"))) && installed(QStringLiteral("vercel")))
        return {QStringLiteral("vercel deploy --prod"), folder, QStringLiteral("vercel")};
    if ((dir.exists(QStringLiteral("netlify.toml")) || dir.exists(QStringLiteral(".netlify/state.json"))) && installed(QStringLiteral("netlify")))
        return {QStringLiteral("netlify deploy --prod"), folder, QStringLiteral("netlify")};
    for (const char *name : {"wrangler.toml", "wrangler.json", "wrangler.jsonc"}) {
        if (dir.exists(QLatin1String(name)) && installed(QStringLiteral("wrangler"))) {
            // A Pages project names its output folder; a Worker doesn't.
            const bool pages = readFile(dir.filePath(QLatin1String(name))).contains("pages_build_output_dir");
            return {pages ? QStringLiteral("wrangler pages deploy") : QStringLiteral("wrangler deploy"), folder, QStringLiteral("wrangler")};
        }
    }
    if (dir.exists(QStringLiteral("fly.toml")) && (installed(QStringLiteral("fly")) || installed(QStringLiteral("flyctl"))))
        return {installed(QStringLiteral("fly")) ? QStringLiteral("fly deploy") : QStringLiteral("flyctl deploy"), folder, QStringLiteral("fly")};
    static const QRegularExpression target(QStringLiteral("^deploy\\s*:"), QRegularExpression::MultilineOption);
    if (target.match(QString::fromUtf8(readFile(dir.filePath(QStringLiteral("Makefile"))))).hasMatch() && installed(QStringLiteral("make")))
        return {QStringLiteral("make deploy"), folder, QStringLiteral("make")};
    if (QFileInfo(dir.filePath(QStringLiteral("deploy.sh"))).isFile()) {
        const bool executable = QFileInfo(dir.filePath(QStringLiteral("deploy.sh"))).isExecutable();
        return {executable ? QStringLiteral("./deploy.sh") : QStringLiteral("sh ./deploy.sh"), folder, QStringLiteral("deploy.sh")};
    }
    return {QString(), folder, QStringLiteral("agent")};
}

std::optional<Command> resolvePreview(const QString &folder)
{
    const QDir dir(folder);
    const QJsonObject config = readObject(dir.filePath(QStringLiteral("omastrator.json")))["preview"].toObject();
    if (const QString command = config["command"].toString().trimmed(); !command.isEmpty()) {
        const QString cwd = config["cwd"].toString().trimmed();
        return Command{command, cwd.isEmpty() ? folder : QDir::cleanPath(dir.filePath(cwd)), QStringLiteral("omastrator.json")};
    }
    const QJsonObject scripts = readObject(dir.filePath(QStringLiteral("package.json")))["scripts"].toObject();
    for (const char *script : {"deploy:preview", "preview:deploy"}) {
        if (scripts.contains(QLatin1String(script))) {
            QString manager = QStringLiteral("npm");
            if (dir.exists(QStringLiteral("pnpm-lock.yaml")))
                manager = QStringLiteral("pnpm");
            else if (dir.exists(QStringLiteral("yarn.lock")))
                manager = QStringLiteral("yarn");
            else if (dir.exists(QStringLiteral("bun.lockb")) || dir.exists(QStringLiteral("bun.lock")))
                manager = QStringLiteral("bun");
            return Command{QStringLiteral("%1 run %2").arg(manager, QLatin1String(script)), folder, QStringLiteral("package.json")};
        }
    }
    if ((dir.exists(QStringLiteral(".vercel/project.json")) || dir.exists(QStringLiteral("vercel.json"))) && installed(QStringLiteral("vercel")))
        return Command{QStringLiteral("vercel deploy"), folder, QStringLiteral("vercel")};
    if ((dir.exists(QStringLiteral("netlify.toml")) || dir.exists(QStringLiteral(".netlify/state.json"))) && installed(QStringLiteral("netlify")))
        return Command{QStringLiteral("netlify deploy"), folder, QStringLiteral("netlify")};
    for (const char *name : {"wrangler.toml", "wrangler.json", "wrangler.jsonc"}) {
        if (dir.exists(QLatin1String(name)) && installed(QStringLiteral("wrangler"))) {
            const bool pages = readFile(dir.filePath(QLatin1String(name))).contains("pages_build_output_dir");
            return Command{pages ? QStringLiteral("wrangler pages deploy --branch preview") : QStringLiteral("wrangler versions upload"), folder,
                           QStringLiteral("wrangler")};
        }
    }
    return std::nullopt;
}

QString remember(const QString &folder, const QString &command, const QString &cwd)
{
    const QString path = QDir(folder).filePath(QStringLiteral("omastrator.json"));
    QJsonObject config = readObject(path);
    QJsonObject deploy{{"command", command.trimmed()}};
    if (!cwd.isEmpty() && canonical(cwd) != canonical(folder))
        deploy["cwd"] = QDir(folder).relativeFilePath(cwd);
    config["deploy"] = deploy;
    return writeFile(path, QJsonDocument(config).toJson(QJsonDocument::Indented));
}

QString settingsPath()
{
    return QDir(home("XDG_CONFIG_HOME", ".config")).filePath(QStringLiteral("omastrator/deploy.json"));
}

Settings settings(const QString &folder)
{
    const QJsonObject entry = readObject(settingsPath())[canonical(folder)].toObject();
    return {entry["confirmed"].toBool(), entry["github"].toString() == QLatin1String("declined")};
}

QString saveSettings(const QString &folder, const Settings &settings)
{
    QJsonObject all = readObject(settingsPath());
    QJsonObject entry;
    if (settings.confirmed)
        entry["confirmed"] = true;
    if (settings.githubDeclined)
        entry["github"] = QStringLiteral("declined");
    all[canonical(folder)] = entry;
    return writeFile(settingsPath(), QJsonDocument(all).toJson(QJsonDocument::Indented));
}

QString logDirectory()
{
    return QDir(home("XDG_STATE_HOME", ".local/state")).filePath(QStringLiteral("omastrator/deploys"));
}

QString newLogPath(const QString &folder)
{
    QDir().mkpath(logDirectory());
    const QString stem = QStringLiteral("%1-%2").arg(QFileInfo(folder).fileName(), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));
    QString path = QDir(logDirectory()).filePath(stem + QStringLiteral(".log"));
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(logDirectory()).filePath(QStringLiteral("%1-%2.log").arg(stem).arg(n));
    return path;
}

std::vector<Record> records(const QString &folder)
{
    std::vector<Record> found;
    const QString wanted = folder.isEmpty() ? QString() : canonical(folder);
    for (const QJsonValue &value : QJsonDocument::fromJson(readFile(QDir(logDirectory()).filePath(QStringLiteral("index.json")))).array()) {
        const QJsonObject each = value.toObject();
        if (!wanted.isEmpty() && each["project"].toString() != wanted)
            continue;
        found.push_back({each["project"].toString(), each["commit"].toString(), each["url"].toString(), each["command"].toString(),
                         each["log"].toString(), QDateTime::fromString(each["time"].toString(), Qt::ISODate), each["ok"].toBool(),
                         each["preview"].toBool()});
    }
    return found;
}

QString addRecord(const Record &record)
{
    const QString path = QDir(logDirectory()).filePath(QStringLiteral("index.json"));
    QJsonArray all = QJsonDocument::fromJson(readFile(path)).array();
    QJsonObject entry{{"project", canonical(record.project)}, {"commit", record.commit}, {"url", record.url}, {"command", record.command},
                      {"log", record.log}, {"time", record.time.toString(Qt::ISODate)}, {"ok", record.ok}};
    if (record.preview)
        entry["preview"] = true;
    all.append(entry);
    // The index is for History, not an archive: the newest few hundred.
    while (all.size() > 500)
        all.removeFirst();
    return writeFile(path, QJsonDocument(all).toJson(QJsonDocument::Indented));
}

std::optional<Record> deployed(const QString &folder, const QString &commit)
{
    if (commit.isEmpty())
        return std::nullopt;
    const std::vector<Record> all = records(folder);
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (it->ok && !it->preview && it->commit == commit)
            return *it;
    return std::nullopt;
}

std::optional<Record> latest(const QString &folder)
{
    const std::vector<Record> all = records(folder);
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (it->ok && !it->url.isEmpty())
            return *it;
    return std::nullopt;
}

QString agentPrompt(const AgentBrief &brief)
{
    QStringList files;
    const Variables variables = projectEnv(brief.folder, QString(), &files);
    for (QString &file : files)
        file = QDir(brief.folder).relativeFilePath(file);
    QString text = QStringLiteral("This is an Omastrator deploy (request %1). Deploy this project to production.\n\n"
                                  "The project is this folder, the user's own checkout: %2\n")
                       .arg(brief.requestId, brief.folder);
    if (!brief.commit.isEmpty())
        text += QStringLiteral("Deploy exactly commit %1 (\"%2\"), which is committed%3. Don't change, commit or push any code.\n")
                    .arg(brief.commit.left(12), brief.subject, brief.pushedTo.isEmpty() ? QString() : QStringLiteral(" and pushed to %1").arg(brief.pushedTo));
    text += QStringLiteral("\nUse the project's existing deploy configuration: its host's CLI and config files, scripts, CI and README. "
                           "The user has already set up the environment variables it needs in the project's .env files");
    if (files.isEmpty())
        text += QStringLiteral(" (there are none in this folder, so use the host's own settings).\n");
    else
        text += QStringLiteral(": %1, with the keys %2. Load them from those files. Never print, log or copy their values anywhere.\n")
                    .arg(files.join(QStringLiteral(", ")), keys(variables).join(QStringLiteral(", ")));
    text += QStringLiteral(
                "Ask the user only if something truly can't be worked out, such as a login.\n\n"
                "When it's live, run:\n  %1 agent live_deployed '{\"requestId\": \"%2\", \"url\": \"<the live URL>\", \"command\": \"<one shell command that "
                "deploys from this folder with no questions, if there is one>\"}'\n"
                "The command is offered to the user to run next time, so it must not contain any secret. If you can't deploy, run the same "
                "with \"error\": \"<why, in one line>\" instead of url.\n")
                .arg(brief.binary, brief.requestId);
    return text;
}

QString firstUrl(const QString &output)
{
    static const QRegularExpression url(QStringLiteral("https://[^\\s\"'<>`\\])}]+"));
    QString found = url.match(output).captured(0);
    while (!found.isEmpty() && QStringLiteral(".,;:!?").contains(found.back()))
        found.chop(1);
    return found;
}

QString lastLine(const QString &output)
{
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const QString line = it->trimmed();
        if (!line.isEmpty())
            return line.size() > 160 ? line.left(159) + QStringLiteral("…") : line;
    }
    return {};
}

QString dryLine()
{
    return dryLine(dryLines);
}

QString dryLine(const QStringList &lines)
{
    const QString path = QDir(home("XDG_STATE_HOME", ".local/state")).filePath(QStringLiteral("omastrator/lines-seen.json"));
    QJsonArray seen = QJsonDocument::fromJson(readFile(path)).array();
    for (const QString &line : lines) {
        if (!seen.contains(line)) {
            seen.append(line);
            writeFile(path, QJsonDocument(seen).toJson(QJsonDocument::Compact));
            return line;
        }
    }
    return {};
}
}
