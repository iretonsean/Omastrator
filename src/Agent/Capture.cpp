#include "Agent/Capture.h"
#include "Agent/AgentClient.h"
#include "Agent/Island.h"
#include "Document/Swatches.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
const QHash<QString, QString> overrides{{"hyprctl", "OMASTRATOR_HYPRCTL"},
                                        {"hyprpicker", "OMASTRATOR_HYPRPICKER"},
                                        {"slurp", "OMASTRATOR_SLURP"},
                                        {"grim", "OMASTRATOR_GRIM"},
                                        {"wl-paste", "OMASTRATOR_WL_PASTE"}};
const QHash<QString, QString> packages{{"hyprctl", "hyprland"}, {"hyprpicker", "hyprpicker"}, {"slurp", "slurp"}, {"grim", "grim"}, {"wl-paste", "wl-clipboard"}};

QString programFor(const QString &name)
{
    const QString overridden = qEnvironmentVariable(overrides.value(name).toUtf8().constData());
    return overridden.isEmpty() ? name : overridden;
}

// The app's answer to one method, starting it first when needed.
QJsonObject callApp(const QString &method, const QJsonObject &params)
{
    if (const QString failure = Island::ensureAppRunning(); !failure.isEmpty())
        throw AgentProtocol::Error(AgentProtocol::notRunning, failure);
    AgentClient::Connection connection;
    return connection.call(method, params);
}
}

namespace Capture {
Run run(const QString &program, const QStringList &args, int timeoutMs)
{
    Run result;
    const QString path = programFor(program);
    if (QStandardPaths::findExecutable(path).isEmpty() && !QFileInfo(path).isExecutable()) {
        result.error = missing(program);
        return result;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(path, args);
    if (!process.waitForStarted(5000)) {
        result.error = QStringLiteral("Could not start %1: %2").arg(program, process.errorString());
        return result;
    }
    result.started = true;
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("%1 did not finish.").arg(program);
        return result;
    }
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = process.readAllStandardOutput();
    return result;
}

QString missing(const QString &program)
{
    return QStringLiteral("%1 isn't installed. Install it with: sudo pacman -S %2").arg(program, packages.value(program, program));
}

std::vector<std::pair<QString, QColor>> themeColors(const QString &colorsToml)
{
    std::vector<std::pair<QString, QColor>> colors;
    static const QRegularExpression line(QStringLiteral(R"(^\s*([A-Za-z0-9_\-]+)\s*=\s*["'](#[0-9A-Fa-f]{6}(?:[0-9A-Fa-f]{2})?)["'])"),
                                         QRegularExpression::MultilineOption);
    for (auto match = line.globalMatch(colorsToml); match.hasNext();) {
        const auto found = match.next();
        const QColor color = QColor::fromString(found.captured(2));
        if (color.isValid())
            colors.emplace_back(found.captured(1), color);
    }
    return colors;
}

QString themeDirectory()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_THEME_DIR");
    return overridden.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/state/omarchy/current/theme")) : overridden;
}

QString themeName(const QString &directory)
{
    QFile file(QFileInfo(directory).dir().filePath(QStringLiteral("theme.name")));
    if (file.open(QIODevice::ReadOnly)) {
        QString name = QString::fromUtf8(file.readAll()).trimmed();
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();
            return name;
        }
    }
    return QStringLiteral("Omarchy theme");
}

QString capturesDirectory()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    const QString data = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
    return QDir(data).filePath(QStringLiteral("omastrator/captures"));
}

int pruneCaptures(const QString &folder, const QDateTime &now)
{
    constexpr int keepNewest = 20, keepSeconds = 30 * 24 * 3600;
    // Newest first; symlinks are skipped so nothing outside the folder can be reached through one.
    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.png")}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    int removed = 0;
    for (qsizetype at = keepNewest; at < files.size(); ++at) {
        if (files[at].lastModified().secsTo(now) > keepSeconds && QFile::remove(files[at].absoluteFilePath()))
            ++removed;
    }
    return removed;
}

QString clipboardSvg(QString *error)
{
    const Run types = run(QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, 5000);
    if (!types.error.isEmpty()) {
        *error = types.error;
        return {};
    }
    const QStringList offered = QString::fromUtf8(types.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // Design tools copy SVG as its own type; code editors copy it as text.
    QString type;
    for (const QString &candidate : {QStringLiteral("image/svg+xml"), QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain"),
                                     QStringLiteral("UTF8_STRING")}) {
        if (offered.contains(candidate)) {
            type = candidate;
            break;
        }
    }
    if (type.isEmpty()) {
        *error = QStringLiteral("The clipboard has no SVG. Copy SVG code or an SVG layer, then try again.");
        return {};
    }
    const Run content = run(QStringLiteral("wl-paste"), {QStringLiteral("--no-newline"), QStringLiteral("--type"), type}, 5000);
    const QString svg = QString::fromUtf8(content.out).trimmed();
    if (!content.error.isEmpty() || !svg.contains(QLatin1String("<svg"))) {
        *error = content.error.isEmpty() ? QStringLiteral("The clipboard has no SVG. Copy SVG code or an SVG layer, then try again.") : content.error;
        return {};
    }
    return svg;
}

int runCli(const QStringList &args, QTextStream &out, QTextStream &err)
{
    const QString action = args.value(0);
    // A notification shows the outcome either way; the terminal gets it too.
    auto done = [&](const QString &message) {
        Island::setActivity(message, 4);
        out << message << '\n';
        return 0;
    };
    auto failed = [&](const QString &message) {
        Island::setActivity(message, 6);
        err << message << '\n';
        return 1;
    };
    try {
        if (action == QLatin1String("color")) {
            const QString target = args.value(1, QStringLiteral("fill"));
            if (!QStringList{QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("swatch")}.contains(target))
                return failed(QStringLiteral("Pick a colour for fill, stroke or swatch."));
            const Run picked = run(QStringLiteral("hyprpicker"), {QStringLiteral("-f"), QStringLiteral("hex"), QStringLiteral("-b"), QStringLiteral("-q")});
            if (!picked.error.isEmpty())
                return failed(picked.error);
            const QColor color = QColor::fromString(QString::fromUtf8(picked.out).trimmed());
            // Escape in hyprpicker picks nothing; that is not an error.
            if (!color.isValid())
                return done(QStringLiteral("No colour picked."));
            if (target == QLatin1String("swatch")) {
                const QJsonObject result = callApp(QStringLiteral("swatches_add"),
                                                   {{"swatches", QJsonArray{QJsonObject{{"color", color.name()}}}}});
                return done(result["added"].toInt() ? QStringLiteral("Swatch added: %1").arg(color.name())
                                                    : QStringLiteral("%1 is already a swatch.").arg(color.name()));
            }
            callApp(QStringLiteral("apply_color"), {{"color", color.name()}, {"target", target}});
            return done(QStringLiteral("%1: %2").arg(target == QLatin1String("fill") ? QStringLiteral("Fill") : QStringLiteral("Stroke"), color.name()));
        }
        if (action == QLatin1String("screenshot")) {
            const Run region = run(QStringLiteral("slurp"), {});
            if (!region.error.isEmpty())
                return failed(region.error);
            const QString geometry = QString::fromUtf8(region.out).trimmed();
            if (region.exitCode != 0 || geometry.isEmpty())
                return done(QStringLiteral("No region chosen."));
            const QString folder = capturesDirectory();
            QDir().mkpath(folder);
            const QString path = QDir(folder).filePath(
                QStringLiteral("screenshot-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
            const Run shot = run(QStringLiteral("grim"), {QStringLiteral("-g"), geometry, path}, 30'000);
            if (!shot.error.isEmpty())
                return failed(shot.error);
            if (shot.exitCode != 0 || !QFileInfo::exists(path))
                return failed(QStringLiteral("grim could not take the screenshot."));
            const QJsonObject result = callApp(QStringLiteral("open_capture"), {{"path", path}});
            if (!result["traced"].toBool())
                return done(QStringLiteral("Screenshot opened. Image Trace found no shapes in it."));
            return done(QStringLiteral("Traced %1 paths. Vectorize with AI is next.").arg(result["paths"].toInt()));
        }
        if (action == QLatin1String("window")) {
            // The focused window, as Hyprland places it: a GTK or Qt app to redesign from.
            const Run active = run(QStringLiteral("hyprctl"), {QStringLiteral("activewindow"), QStringLiteral("-j")}, 5000);
            if (!active.error.isEmpty())
                return failed(active.error);
            const QJsonObject window = QJsonDocument::fromJson(active.out).object();
            const QJsonArray at = window["at"].toArray(), size = window["size"].toArray();
            if (at.size() != 2 || size.size() != 2 || size[0].toInt() <= 0)
                return failed(QStringLiteral("No window has focus to capture."));
            const QString geometry = QStringLiteral("%1,%2 %3x%4").arg(at[0].toInt()).arg(at[1].toInt()).arg(size[0].toInt()).arg(size[1].toInt());
            const QString folder = capturesDirectory();
            QDir().mkpath(folder);
            const QString path = QDir(folder).filePath(
                QStringLiteral("window-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
            const Run shot = run(QStringLiteral("grim"), {QStringLiteral("-g"), geometry, path}, 30'000);
            if (!shot.error.isEmpty())
                return failed(shot.error);
            if (shot.exitCode != 0 || !QFileInfo::exists(path))
                return failed(QStringLiteral("grim could not capture the window."));
            const QJsonObject result = callApp(QStringLiteral("open_capture"), {{"path", path}});
            const QString name = window["title"].toString().isEmpty() ? window["class"].toString() : window["title"].toString();
            return done(result["traced"].toBool() ? QStringLiteral("Captured %1 and traced it. Vectorize with AI is next.").arg(name.left(40))
                                                  : QStringLiteral("Captured %1.").arg(name.left(40)));
        }
        if (action == QLatin1String("image")) {
            // A picture already taken (the dock's window thumbnails grab it): open and trace it.
            const QString path = args.value(1);
            if (!QFileInfo::exists(path))
                return failed(QStringLiteral("There is no picture at %1.").arg(path));
            const QJsonObject result = callApp(QStringLiteral("open_capture"), {{"path", path}});
            return done(result["traced"].toBool() ? QStringLiteral("Captured the window and traced it. Vectorize with AI is next.")
                                                  : QStringLiteral("Captured the window."));
        }
        if (action == QLatin1String("paste-svg")) {
            QString error;
            const QString svg = clipboardSvg(&error);
            if (svg.isEmpty())
                return failed(error);
            const QJsonObject result = callApp(QStringLiteral("paste_svg"), {{"svg", svg}});
            return done(QStringLiteral("Pasted %1 editable paths.").arg(result["paths"].toInt()));
        }
        if (action == QLatin1String("theme-swatches")) {
            const QString directory = themeDirectory();
            QFile file(QDir(directory).filePath(QStringLiteral("colors.toml")));
            if (!file.open(QIODevice::ReadOnly))
                return failed(QStringLiteral("Couldn't read the Omarchy theme's colours at %1.").arg(file.fileName()));
            QJsonArray swatches;
            for (const auto &[key, color] : themeColors(QString::fromUtf8(file.readAll())))
                swatches.append(QJsonObject{{"name", Swatches::nameFromKey(key)}, {"color", color.name()}});
            if (swatches.isEmpty())
                return failed(QStringLiteral("The Omarchy theme's colors.toml has no colours."));
            const QString group = QStringLiteral("Omarchy: %1").arg(themeName(directory));
            const QJsonObject result = callApp(QStringLiteral("swatches_add"), {{"group", group}, {"swatches", swatches}, {"replace", true}});
            return done(QStringLiteral("Loaded %1 swatches from %2.").arg(QString::number(result["added"].toInt()), themeName(directory)));
        }
    } catch (const AgentProtocol::Error &failure) {
        return failed(failure.message());
    }
    err << "Usage: omastrator island capture <color [fill|stroke|swatch] | screenshot | window | image <png> | paste-svg | theme-swatches>\n";
    return 1;
}
}
