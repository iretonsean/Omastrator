#include "Agent/Island.h"
#include "IO/DocumentExporter.h"
#include "System/AppStyle.h"
#include "System/ConfigBackup.h"
#include "System/DesktopLook.h"
#include "UI/AgentBridge.h"
#include "UI/DesignController.h"
#include "UI/DesktopLookPanel.h"
#include "UI/ProjectWorkspace.h"
#include "UI/SyncConfirmDialog.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <csignal>

// Changing the real thing on the desktop (docs/ANYWHERE.md, phase 4): the
// `design` method's `look` (Omarchy's gaps, borders, bar, font, wallpaper and
// theme colours) and `restyle` (a GTK or Qt app through its toolkit). Edits
// preview live; nothing is written without the confirmation dialog.

namespace {
QString runAll(const std::vector<QStringList> &commands)
{
    QStringList failures;
    for (const QStringList &command : commands) {
        QProcess process;
        process.start(command.front(), command.mid(1));
        if (!process.waitForStarted(3000)) {
            failures.append(QStringLiteral("Couldn't start %1.").arg(QFileInfo(command.front()).fileName()));
            continue;
        }
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(500);
        } else if (process.exitCode() != 0) {
            const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            failures.append(QStringLiteral("%1 %2 failed%3").arg(QFileInfo(command.front()).fileName(), command.value(1),
                                                                   error.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + error));
        }
    }
    return failures.join(QLatin1Char(' '));
}

QJsonArray history()
{
    QJsonArray list;
    for (const ConfigBackup::Backup &backup : ConfigBackup::list()) {
        QJsonArray files;
        for (const ConfigBackup::Entry &entry : backup.entries)
            files.append(entry.path);
        list.append(QJsonObject{{"id", backup.id},
                                {"title", backup.title},
                                {"when", backup.when.toString(Qt::ISODate)},
                                {"reverted", backup.reverted},
                                {"files", files}});
    }
    return list;
}

// The Omarchy theme's colours as a palette, for a Qt app's qt6ct scheme.
QPalette themePalette(const DesktopLook::Look &look)
{
    QPalette palette;
    for (const auto &[key, colour] : look.colors) {
        if (key == QLatin1String("background"))
            for (const QPalette::ColorRole role : {QPalette::Window, QPalette::Base, QPalette::Button})
                palette.setColor(role, colour);
        else if (key == QLatin1String("foreground"))
            for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
                palette.setColor(role, colour);
        else if (key == QLatin1String("accent"))
            palette.setColor(QPalette::Highlight, colour);
    }
    return palette;
}
}

QString DesignController::look(const QJsonObject &params, QJsonObject &result)
{
    const QString op = params["op"].toString(QStringLiteral("get"));
    const DesktopLook::Paths paths = DesktopLook::Paths::current();
    if (op == QLatin1String("get") || op == QLatin1String("status")) {
        result = DesktopLook::read(paths).toJson();
        result["pending"] = m_lookEdits;
        result["note"] = m_lookEdits.isEmpty() ? QString() : DesktopLook::previewNote(m_lookEdits, paths);
        result["history"] = history();
        return {};
    }
    if (op == QLatin1String("fonts")) {
        result["fonts"] = QJsonArray::fromStringList(DesktopLook::fontFamilies());
        return {};
    }
    if (op == QLatin1String("history")) {
        result["history"] = history();
        return {};
    }
    if (op == QLatin1String("panel")) {
        openLookPanel(params["section"].toString(), std::nullopt);
        return {};
    }
    if (op == QLatin1String("handles")) {
        m_gapHandles = params["on"].toBool(true);
        emit changed();
        return {};
    }
    if (op == QLatin1String("wallpaperFromArtboard")) {
        // The front document's artboard, at the monitor's size, as a picture to preview.
        ProjectTab &tab = m_workspace.current();
        if (!tab.session.hasDocument() || (tab.path && QFileInfo(*tab.path).fileName() == QLatin1String("desk.omai")))
            return QStringLiteral("Open the document whose artboard should be the wallpaper, then try again.");
        m_mode->refresh();
        const auto monitor = Hyprland::focusedMonitor(m_mode->monitors());
        const VectorDocument &document = *tab.session.document();
        const double width = monitor ? monitor->rect.width() * monitor->scale : 1920;
        const double scale = document.size.width() > 0 ? width / document.size.width() : 1;
        const QString folder = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("wallpapers"));
        QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("artboard-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        try {
            DocumentExporter::writePng(document, path, scale);
        } catch (const FileError &failure) {
            return QStringLiteral("The artboard couldn't be made into a picture: %1").arg(failure.message());
        }
        QJsonObject preview{{"op", "preview"}, {"edits", QJsonObject{{"wallpaper", path}}}};
        result["wallpaper"] = path;
        return look(preview, result);
    }
    if (op == QLatin1String("preview")) {
        QJsonObject merged = m_lookEdits;
        const QJsonObject edits = params["edits"].toObject();
        for (auto it = edits.begin(); it != edits.end(); ++it) {
            if (it.key() == QLatin1String("colors")) {
                QJsonObject colours = merged["colors"].toObject();
                const QJsonObject more = it.value().toObject();
                for (auto colour = more.begin(); colour != more.end(); ++colour)
                    colours[colour.key()] = colour.value();
                merged["colors"] = colours;
            } else {
                merged[it.key()] = it.value();
            }
        }
        if (const QString problem = DesktopLook::validate(merged); !problem.isEmpty())
            return problem;
        m_lookEdits = merged;
        const QString failed = runAll(DesktopLook::previewCommands(m_lookEdits, DesktopLook::read(paths, false), paths));
        result["pending"] = m_lookEdits;
        result["note"] = DesktopLook::previewNote(m_lookEdits, paths);
        m_message = failed.isEmpty() ? result["note"].toString() : QStringLiteral("The preview couldn't all be shown: ") + failed;
        emit changed();
        return {};
    }
    if (op == QLatin1String("discard")) {
        if (m_lookEdits.isEmpty())
            return {};
        const QString failed = runAll(DesktopLook::discardCommands(m_lookEdits, DesktopLook::read(paths, false), paths));
        m_lookEdits = {};
        say(failed.isEmpty() ? QStringLiteral("Preview discarded. The desktop is as it was.") : QStringLiteral("Preview discarded, but ") + failed);
        emit lookChanged();
        return {};
    }
    if (op == QLatin1String("save")) {
        if (m_lookEdits.isEmpty())
            return QStringLiteral("There's nothing to save. Change something first.");
        const SyncPlan plan = DesktopLook::savePlan(m_lookEdits, DesktopLook::read(paths), paths);
        bool confirmed = false;
        const QString failed = SyncConfirmDialog::run(plan, m_lookPanel ? static_cast<QWidget *>(m_lookPanel.data()) : &m_window, &confirmed);
        result["confirmed"] = confirmed;
        if (!confirmed)
            return failed;
        result["backup"] = QFileInfo(plan.backupFolder).fileName();
        if (!failed.isEmpty())
            return failed + QStringLiteral(" The backup is in %1.").arg(plan.backupFolder);
        m_lookEdits = {};
        say(QStringLiteral("Saved to your desktop's config. Revert is in Desktop Look's history."));
        emit lookChanged();
        return {};
    }
    if (op == QLatin1String("revert")) {
        const QString id = params["id"].toString();
        const auto backup = ConfigBackup::read(QDir(ConfigBackup::root()).filePath(id.isEmpty() ? QStringLiteral("-") : id));
        if (!backup || id.contains(QLatin1Char('/')))
            return QStringLiteral("There's no backup called “%1”.").arg(id);
        const SyncPlan plan = ConfigBackup::revertPlan(*backup);
        bool confirmed = false;
        const QString failed = SyncConfirmDialog::run(plan, m_lookPanel ? static_cast<QWidget *>(m_lookPanel.data()) : &m_window, &confirmed);
        result["confirmed"] = confirmed;
        if (confirmed && failed.isEmpty())
            say(QStringLiteral("Reverted “%1”.").arg(backup->title));
        emit lookChanged();
        return failed;
    }
    return QStringLiteral("There is no look action “%1”.").arg(op);
}

QString DesignController::restyle(const QJsonObject &params, QJsonObject &result)
{
    const QString op = params["op"].toString(QStringLiteral("get"));
    const DesktopLook::Paths paths = DesktopLook::Paths::current();
    if (op == QLatin1String("discard")) {
        stopStylePreview();
        m_styleApp.reset();
        emit lookChanged();
        return {};
    }
    // The app: the window pointed at, else the one chosen before.
    if (params.contains(QLatin1String("target")) || !m_styleApp) {
        QString error;
        const std::optional<Target> chosen = target(params, &error);
        if (!chosen)
            return error;
        if (!chosen->inspection || chosen->inspection->surface.kind == Surface::Kind::desktop || chosen->inspection->surface.pid <= 0)
            return QStringLiteral("Point at an app's window to restyle it.");
        const Inspection &inspection = *chosen->inspection;
        m_styleApp = describeApp ? describeApp(inspection.surface.pid, inspection.surface.app)
                                   : AppStyle::App::describe(inspection.surface.pid, inspection.surface.app);
        m_styleRole = inspection.source == QLatin1String("accessibility") ? inspection.role : QString();
        m_styleSeen = inspection;
    }
    const AppStyle::App &app = *m_styleApp;
    AppStyle::Style style = AppStyle::Style::fromJson(params["style"].toObject());
    if (style.role.isEmpty() && !params["style"].toObject().contains(QLatin1String("role")))
        style.role = m_styleRole;
    const QPalette base = themePalette(DesktopLook::read(paths, false));
    if (op == QLatin1String("get")) {
        result = QJsonObject{{"app", app.className},
                             {"toolkit", AppStyle::name(app.toolkit)},
                             {"platformTheme", app.platformTheme},
                             {"role", m_styleRole},
                             {"widget", AppStyle::isQt(app.toolkit) ? AppStyle::qtSelector(m_styleRole) : AppStyle::gtkSelector(m_styleRole)},
                             {"honesty", AppStyle::honesty(app)},
                             {"command", QJsonArray::fromStringList(app.command)}};
        // What was seen under the pointer, to start from.
        if (m_styleSeen) {
            if (m_styleSeen->background.isValid())
                result["background"] = m_styleSeen->background.name();
            if (m_styleSeen->color.isValid())
                result["foreground"] = m_styleSeen->color.name();
            if (!m_styleSeen->fontFamily.isEmpty())
                result["font"] = m_styleSeen->fontFamily;
            if (m_styleSeen->fontSize > 0)
                result["fontSize"] = m_styleSeen->fontSize;
        }
        return {};
    }
    if (op == QLatin1String("preview")) {
        QString error;
        const QString folder = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("restyle-preview"));
        const AppStyle::Preview preview = AppStyle::preview(app, style, base, paths, folder, &error);
        if (!error.isEmpty())
            return error;
        stopStylePreview();
        QProcess process;
        process.setProgram(preview.command.front());
        process.setArguments(preview.command.mid(1));
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString &pair : preview.environment)
            environment.insert(pair.section(QLatin1Char('='), 0, 0), pair.section(QLatin1Char('='), 1));
        process.setProcessEnvironment(environment);
        qint64 pid = 0;
        if (!process.startDetached(&pid))
            return QStringLiteral("Couldn't start a second %1 for the preview.").arg(app.className);
        m_stylePreviewPid = pid;
        m_stylePreviewFolder = folder;
        result["note"] = preview.note;
        result["command"] = QJsonArray::fromStringList(preview.command);
        result["environment"] = QJsonArray::fromStringList(preview.environment);
        say(preview.note);
        return {};
    }
    if (op == QLatin1String("save")) {
        const SyncPlan plan = AppStyle::plan(app, style, base, paths);
        bool confirmed = false;
        const QString failed = SyncConfirmDialog::run(plan, m_lookPanel ? static_cast<QWidget *>(m_lookPanel.data()) : &m_window, &confirmed);
        result["confirmed"] = confirmed;
        if (confirmed && failed.isEmpty()) {
            result["backup"] = QFileInfo(plan.backupFolder).fileName();
            say(QStringLiteral("Saved. %1 shows it after a restart; Revert is in Desktop Look's history.").arg(app.className));
            emit lookChanged();
        }
        return failed;
    }
    return QStringLiteral("There is no restyle action “%1”.").arg(op);
}

void DesignController::stopStylePreview()
{
    // Only the copy this started: the pid must still be a process whose environment or arguments name the preview folder.
    if (m_stylePreviewPid > 0 && !m_stylePreviewFolder.isEmpty()) {
        QFile environ(QStringLiteral("/proc/%1/environ").arg(m_stylePreviewPid));
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(m_stylePreviewPid));
        const QByteArray folder = m_stylePreviewFolder.toLocal8Bit();
        if ((environ.open(QIODevice::ReadOnly) && environ.readAll().contains(folder)) || (cmdline.open(QIODevice::ReadOnly) && cmdline.readAll().contains(folder)))
            ::kill(pid_t(m_stylePreviewPid), SIGTERM);
    }
    m_stylePreviewPid = 0;
}

void DesignController::openLookPanel(const QString &section, const std::optional<Inspection> &window)
{
    if (!m_lookPanel) {
        m_lookPanel = new DesktopLookPanel(*this, &m_window);
        m_lookPanel->setAttribute(Qt::WA_DeleteOnClose);
    }
    if (window && window->surface.kind != Surface::Kind::desktop) {
        QJsonObject ignored;
        m_styleApp.reset();
        m_restyleError = restyle(QJsonObject{{"op", "get"}, {"target", window->id}}, ignored);
    }
    m_lookPanel->showSection(section);
    m_gapHandles = section == QLatin1String("windows");
    emit changed();
}

QJsonObject DesignController::lookStatus()
{
    QJsonObject status{{"pending", !m_lookEdits.isEmpty()}, {"panel", !m_lookPanel.isNull() && m_lookPanel->isVisible()}};
    if (!m_gapHandles || !m_mode->isOn())
        return status;
    // The gap handles: what the gaps are, with the edit on top, for the overlay to draw and drag.
    const DesktopLook::Look now = DesktopLook::read(DesktopLook::Paths::current(), false);
    status["gapsIn"] = m_lookEdits.value(QLatin1String("gapsIn")).toInt(now.gapsIn);
    status["gapsOut"] = m_lookEdits.value(QLatin1String("gapsOut")).toInt(now.gapsOut);
    status["borderSize"] = m_lookEdits.value(QLatin1String("borderSize")).toInt(now.borderSize);
    status["rounding"] = m_lookEdits.value(QLatin1String("rounding")).toInt(now.rounding);
    status["handles"] = true;
    return status;
}
