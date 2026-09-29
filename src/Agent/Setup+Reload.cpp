#include "Agent/Hyprland.h"
#include "Agent/Setup.h"
#include <QDir>

// The check around a setup that loads Omastrator's keys into Hyprland: reload once before writing for a fair baseline,
// reload after, and compare. It never says a key is back or working without looking (docs/OS-SUITE.md, "Reload check").
namespace {
QString hyprlandConfigPath(const Setup::Environment &environment)
{
    const bool lua = Setup::hyprFormat(environment) == Setup::HyprFormat::lua;
    return QDir(environment.hyprDirectory()).filePath(lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf"));
}

QString listed(const QStringList &keys)
{
    return keys.mid(0, 8).join(QStringLiteral(", ")) + (keys.size() > 8 ? QStringLiteral(" and %1 more").arg(keys.size() - 8) : QString());
}
}

namespace Setup {
bool hasSourceLine(const QByteArray &config, HyprFormat format)
{
    if (format == HyprFormat::lua)
        return config.contains("dofile") && config.contains("omastrator/hyprland.lua\")");
    return config.contains("source = ~/.config/omastrator/hyprland.conf");
}

KeyCheck startKeyCheck(const Environment &environment, bool keysAccepted, bool sourceAccepted, const QStringList &skippedKeys, QTextStream &out, QTextStream &err)
{
    KeyCheck check;
    if (!keysAccepted && !sourceAccepted)
        return check;
    // Reloading gains nothing while Hyprland doesn't load our file.
    QFile config(hyprlandConfigPath(environment));
    const QByteArray text = config.open(QIODevice::ReadOnly) ? config.readAll() : QByteArray();
    if (!sourceAccepted && !hasSourceLine(text, hyprFormat(environment)))
        return check;
    // Not running (a TTY, a chroot): the keys load at the next start.
    if (!Hyprland::query(QStringLiteral("binds")).isArray())
        return check;

    // A reload loads the whole config again: an unloaded error in it would swap the user's keys for Hyprland's emergency ones before setup wrote a thing.
    const QString configPath = hyprlandConfigPath(environment);
    const Hyprland::ConfigCheck verdict = Hyprland::verifyConfig(configPath);
    if (!verdict.available) {
        out << "\nHyprland's own program isn't here to check your config with, so setup won't reload Hyprland or check your keys.\n"
               "Omastrator's keys load at the next reload (`hyprctl reload`) or login.\n";
        return check;
    }
    if (!verdict.ok) {
        err << "\nHyprland says your config has errors it hasn't loaded yet (" << configPath << "):\n" << verdict.errors << "\n\n"
            << "Nothing was changed, and Hyprland wasn't reloaded: a reload would load those errors, and your keys with them. Fix them and run setup again,\n"
            << "or run `omastrator setup --no-keys` to install without keys. `hyprctl configerrors` lists what the running Hyprland already couldn't load.\n";
        check.stopped = true;
        return check;
    }
    check.active = true;

    QStringList skipped;
    for (const QString &key : skippedKeys)
        skipped << normalizeCombo(key);
    for (const QString &combo : omastratorKeys(DesignKeys::from(environment))) {
        if (!combo.isEmpty() && !skipped.contains(combo))
            check.ownKeys << combo;
    }

    out << "\nReloading Hyprland's config once before anything is written, to see which keys it has when nothing of Omastrator's has changed.\n";
    out.flush();
    const QString error = Hyprland::reload();
    const std::optional<QSet<QString>> before = error.isEmpty() ? liveUserBinds() : std::nullopt;
    if (!before) {
        err << "\n" << (error.isEmpty() ? QStringLiteral("Hyprland reloaded but didn't answer afterwards") : QStringLiteral("Hyprland didn't reload its config (%1)").arg(error))
            << ", so setup can't tell which keys are yours before it changes anything.\n"
            << "Nothing was changed. `hyprctl configerrors` may say why; fix that and run setup again, or run `omastrator setup --no-keys` to install without keys.\n";
        check.active = false;
        check.stopped = true;
        return check;
    }
    check.before = *before;
    return check;
}

KeyOutcome finishKeyCheck(const KeyCheck &check, const Environment &environment, const QString &backupName, QTextStream &in, QTextStream &out, QTextStream &err,
                          QString *summary)
{
    if (!check.active)
        return KeyOutcome::fine;
    const QString folder = QDir(environment.backups()).filePath(backupName);
    if (const QString error = Hyprland::reload(); !error.isEmpty()) {
        err << "\nHyprland didn't reload its config (" << error << ").\n"
            << "The files are written, but Hyprland isn't using Omastrator's keys until it reloads: run `hyprctl reload`, or log out and in.\n"
            << "Setup couldn't check your keys. To put every file back: omastrator setup --restore " << backupName << '\n';
        *summary = QStringLiteral("Hyprland was not reloaded, so the new keys are not active yet");
        return KeyOutcome::problem;
    }
    const BindCheck now = checkBinds(check.before, check.ownKeys);
    if (!now.answered) {
        err << "\nHyprland reloaded but then didn't answer, so setup can't tell whether your keys are all there. Nothing was put back.\n"
            << "Look with `hyprctl binds`; if keys are missing, run: omastrator setup --restore " << backupName << '\n';
        *summary = QStringLiteral("Hyprland didn't answer after the reload, so the keys were not checked");
        return KeyOutcome::problem;
    }
    if (!now.lost.isEmpty()) {
        err << "\nAfter the change, Hyprland no longer has these keys: " << listed(now.lost) << ".\n"
            << "Setup is putting every file back as it was. Anything Omastrator's key file logged is in " << environment.setupLog() << ".\n";
        err.flush();
        if (runRestore(environment, backupName, true, false, in, out, err) != 0) {
            out << "The restore didn't finish: run `omastrator setup --restore " << backupName << "`.\n";
            return KeyOutcome::restored;
        }
        // Hyprland doesn't reread the files a Lua config loads, so the reload after a restore can bring back other trouble too: look again.
        const QString error = Hyprland::reload();
        const BindCheck again = error.isEmpty() ? checkBinds(check.before, {}) : BindCheck();
        if (!error.isEmpty())
            err << "Setup put its files back, but Hyprland didn't reload (" << error << "). Run `hyprctl reload`, then check your keys.\n";
        else if (!again.answered)
            err << "Setup put its files back, but Hyprland didn't answer after reloading, so it can't say whether your keys are back. Run `hyprctl binds` to look.\n";
        else if (!again.lost.isEmpty())
            err << "Still missing with every file put back as it was: " << listed(again.lost) << ".\n"
                << "So Omastrator's files aren't the cause; something else in your Hyprland config is (an edit that hadn't been reloaded yet?).\n"
                << "`hyprctl configerrors` lists what Hyprland couldn't load. The backup stays in " << folder << ".\n";
        else
            out << "Your keys are back. Omastrator's keys were not installed.\n";
        return KeyOutcome::restored;
    }
    if (!now.missing.isEmpty()) {
        err << "\nHyprland didn't bind these of Omastrator's keys: " << listed(now.missing) << ".\n"
            << "Your own keys are all still there. What went wrong is in " << environment.setupLog() << " (if Hyprland got that far) and in `hyprctl configerrors`.\n"
            << "To put every file back: omastrator setup --restore " << backupName << '\n';
        *summary = QStringLiteral("Hyprland didn't bind %1 of Omastrator's keys").arg(now.missing.size());
        return KeyOutcome::problem;
    }
    return KeyOutcome::fine;
}
}
