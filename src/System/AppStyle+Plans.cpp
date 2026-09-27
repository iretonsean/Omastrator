#include "System/AppStyle.h"
#include "System/ConfigBackup.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

// Restyling an app: the confirmed plan, and a preview that starts a second
// copy of the app with the change from a temporary folder.

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    // Never through a link: that would be the user's real file.
    if (QFileInfo(path).isSymLink() || QFileInfo(QFileInfo(path).absolutePath()).isSymLink())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

int rolesFor(AppStyle::Toolkit toolkit)
{
    // qt5ct lists 21 roles; qt6ct every role this Qt has (Accent included from 6.6).
    return toolkit == AppStyle::Toolkit::qt5 ? 21 : int(QPalette::NColorRoles);
}

QString what(const AppStyle::App &app)
{
    return app.className.isEmpty() ? QStringLiteral("this app") : app.className;
}

// A copy of `real` in which every entry is a link to the real one, except `keep`, which is a real folder
// of links in turn, less `skip`: the preview writes those itself, so none of them may lead to a real file.
bool shadow(const QString &real, const QString &copy, const QString &keep, const QStringList &skip)
{
    if (!QDir().mkpath(copy))
        return false;
    const QFileInfoList entries = QDir(real).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName() == keep)
            continue;
        QFile::link(entry.absoluteFilePath(), QDir(copy).filePath(entry.fileName()));
    }
    const QString inner = QDir(copy).filePath(keep);
    if (!QDir().mkpath(inner))
        return false;
    const QFileInfoList kept = QDir(QDir(real).filePath(keep)).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : kept) {
        if (!skip.contains(entry.fileName()))
            QFile::link(entry.absoluteFilePath(), QDir(inner).filePath(entry.fileName()));
    }
    return true;
}

// Relative url()s in a moved gtk.css still point where they did.
QByteArray absoluteUrls(const QByteArray &css, const QString &folder)
{
    QString text = QString::fromUtf8(css);
    static const QRegularExpression url(QStringLiteral(R"re(url\("([^"/:][^":]*)"\))re"));
    QString out;
    qsizetype last = 0;
    for (auto it = url.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        out += text.mid(last, match.capturedStart() - last);
        out += QStringLiteral("url(\"%1\")").arg(QUrl::fromLocalFile(QDir::cleanPath(QDir(folder).filePath(match.captured(1)))).toString());
        last = match.capturedEnd();
    }
    out += text.mid(last);
    return out.toUtf8();
}
}

namespace AppStyle {
QString honesty(const App &app)
{
    const QString who = what(app);
    switch (app.toolkit) {
    case Toolkit::gtk3:
    case Toolkit::gtk4:
        return QStringLiteral("%1 apps read gtk.css only when they start, so %2 shows the change after a restart; the preview opens a second copy with it. "
                              "GTK has no selector for one app, so this reaches every %1 app's %3.")
            .arg(name(app.toolkit), who, QStringLiteral("widgets of this kind"));
    case Toolkit::qt5:
    case Toolkit::qt6:
        if (app.usesQtct())
            return QStringLiteral("%1 reads %2's palette, font and stylesheet when it starts, so it shows the change after a restart; the preview opens a "
                                  "second copy with it. Every Qt app using %2 changes too.")
                .arg(who, app.platformTheme);
        if (!app.widgets)
            return QStringLiteral("%1 is a Qt Quick app. Stylesheets don't reach Qt Quick, and it runs with the %2 platform theme, so qt5ct and qt6ct "
                                  "don't either. Mock it up on the overlay and hand it to the agent instead.")
                .arg(who, app.platformTheme.isEmpty() ? QStringLiteral("default") : app.platformTheme);
        return QStringLiteral("%1 gets its own stylesheet, passed by its launcher entry, so it shows the change the next time it's opened from the "
                              "launcher; the preview opens a second copy with it. Only this app changes.")
            .arg(who);
    case Toolkit::unknown:
        break;
    }
    return QStringLiteral("%1 isn't a GTK or Qt app, so its toolkit can't be restyled. Mock it up on the overlay and hand it to the agent instead.").arg(who);
}

SyncPlan plan(const App &app, const Style &style, const QPalette &base, const DesktopLook::Paths &paths)
{
    SyncPlan plan;
    plan.title = QStringLiteral("Restyle %1").arg(what(app));
    if (const QString problem = validate(style); !problem.isEmpty()) {
        plan.problem = problem;
        return plan;
    }
    if (app.toolkit == Toolkit::unknown || (isQt(app.toolkit) && !app.widgets && !app.usesQtct())) {
        plan.problem = honesty(app);
        return plan;
    }
    const QString who = what(app);
    if (!isQt(app.toolkit)) {
        const QString path = gtkCss(paths, app.toolkit);
        const std::optional<QByteArray> before = contents(path);
        plan.writes.emplace_back(path, before, withGtkStyle(before.value_or(QByteArray()), style, app.toolkit));
        plan.destination = QStringLiteral("%1's user styles for %2, in %3. Nothing is committed or published.").arg(name(app.toolkit), who, path);
    } else if (app.usesQtct()) {
        const QString folder = qtctFolder(paths, app.toolkit);
        const QString scheme = folder + QStringLiteral("/colors/Omastrator.conf");
        const QString sheet = folder + QStringLiteral("/qss/omastrator.qss");
        const QString conf = folder + QLatin1Char('/') + app.platformTheme + QStringLiteral(".conf");
        plan.writes.emplace_back(scheme, contents(scheme), qtctScheme(style, base, rolesFor(app.toolkit)));
        plan.writes.emplace_back(sheet, contents(sheet), qss(style));
        const std::optional<QByteArray> before = contents(conf);
        plan.writes.emplace_back(conf, before, qtctConf(before.value_or(QByteArray()), scheme, sheet, style));
        plan.destination = QStringLiteral("%1's palette, font and stylesheet for Qt apps, in %2. Nothing is committed or published.").arg(app.platformTheme, folder);
    } else {
        const QString sheet = appQss(paths, app);
        plan.writes.emplace_back(sheet, contents(sheet), qss(style));
        const QString entry = userDesktopFile(paths, app);
        if (!entry.isEmpty()) {
            const std::optional<QByteArray> before = contents(entry);
            const QByteArray source = before.value_or(contents(app.desktopFile).value_or(QByteArray()));
            plan.writes.emplace_back(entry, before, withStylesheet(source, sheet));
        }
        plan.destination = QStringLiteral("A stylesheet for %1 in %2%3. Nothing is committed or published.")
                               .arg(who, sheet, entry.isEmpty() ? QString() : QStringLiteral(", and its launcher entry in %1").arg(entry));
        if (entry.isEmpty())
            plan.note = QStringLiteral("No launcher entry was found for %1, so start it with -stylesheet %2 to use it. ").arg(who, sheet);
    }
    plan.note += honesty(app) + QStringLiteral(" Revert puts every file back from the backup.");
    plan.backupFolder = ConfigBackup::newFolder(QStringLiteral("restyle ") + who);
    return plan;
}

Preview preview(const App &app, const Style &style, const QPalette &base, const DesktopLook::Paths &paths, const QString &folder, QString *error)
{
    Preview result;
    const auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return Preview();
    };
    if (const QString problem = validate(style); !problem.isEmpty())
        return fail(problem);
    if (app.command.isEmpty())
        return fail(QStringLiteral("There's no way to start a second copy of %1: its command line couldn't be read.").arg(what(app)));
    if (app.toolkit == Toolkit::unknown || (isQt(app.toolkit) && !app.widgets && !app.usesQtct()))
        return fail(honesty(app));
    QDir(folder).removeRecursively();
    if (!QDir().mkpath(folder))
        return fail(QStringLiteral("Couldn't make the preview folder %1.").arg(folder));
    result.folder = folder;
    result.command = app.command;
    const QString config = QDir(folder).filePath(QStringLiteral("config"));
    if (!isQt(app.toolkit)) {
        const QString gtk = app.toolkit == Toolkit::gtk4 ? QStringLiteral("gtk-4.0") : QStringLiteral("gtk-3.0");
        if (!shadow(paths.config, config, gtk, {QStringLiteral("gtk.css")}))
            return fail(QStringLiteral("Couldn't make the preview folder %1.").arg(config));
        const QString real = gtkCss(paths, app.toolkit);
        const QByteArray css = withGtkStyle(absoluteUrls(contents(real).value_or(QByteArray()), QFileInfo(real).absolutePath()), style, app.toolkit);
        const QString copy = QDir(config).filePath(gtk + QStringLiteral("/gtk.css"));
        if (!writeFile(copy, css))
            return fail(QStringLiteral("Couldn't write the preview's %1.").arg(copy));
        result.environment = {QStringLiteral("XDG_CONFIG_HOME=") + config};
    } else if (app.usesQtct()) {
        const QString name = app.platformTheme;
        if (!shadow(paths.config, config, name, {QStringLiteral("colors"), QStringLiteral("qss"), name + QStringLiteral(".conf")}))
            return fail(QStringLiteral("Couldn't make the preview folder %1.").arg(config));
        const QString inner = QDir(config).filePath(name);
        const QString scheme = inner + QStringLiteral("/colors/Omastrator.conf");
        const QString sheet = inner + QStringLiteral("/qss/omastrator.qss");
        const QString conf = inner + QLatin1Char('/') + name + QStringLiteral(".conf");
        const QByteArray realConf = contents(QDir(qtctFolder(paths, app.toolkit)).filePath(name + QStringLiteral(".conf"))).value_or(QByteArray());
        if (!writeFile(scheme, qtctScheme(style, base, rolesFor(app.toolkit))) || !writeFile(sheet, qss(style))
            || !writeFile(conf, qtctConf(realConf, scheme, sheet, style)))
            return fail(QStringLiteral("Couldn't write the preview's %1 files.").arg(name));
        result.environment = {QStringLiteral("XDG_CONFIG_HOME=") + config, QStringLiteral("QT_QPA_PLATFORMTHEME=") + name};
    } else {
        const QString sheet = QDir(folder).filePath(QStringLiteral("preview.qss"));
        if (!writeFile(sheet, qss(style)))
            return fail(QStringLiteral("Couldn't write the preview's stylesheet."));
        result.command << QStringLiteral("-stylesheet") << sheet;
    }
    result.note = QStringLiteral("A second %1 opens with the change; the one already open stays as it was. Apps that hand a second launch to the "
                                 "copy already open show nothing new: close it first.")
                      .arg(what(app));
    return result;
}
}
