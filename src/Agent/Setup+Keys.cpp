#include "Agent/Hyprland.h"
#include "Agent/Setup.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <map>
#include <vector>

namespace {
constexpr int shiftBit = 1, ctrlBit = 4, altBit = 8, superBit = 64;

// Hyprland's own reading of a modifier string: any of these words in it.
int modsOf(const QString &text)
{
    const QString upper = text.toUpper();
    int mods = 0;
    if (upper.contains(QLatin1String("SHIFT")))
        mods |= shiftBit;
    if (upper.contains(QLatin1String("CTRL")) || upper.contains(QLatin1String("CONTROL")))
        mods |= ctrlBit;
    if (upper.contains(QLatin1String("ALT")) || upper.contains(QLatin1String("MOD1")))
        mods |= altBit;
    if (upper.contains(QLatin1String("SUPER")) || upper.contains(QLatin1String("WIN")) || upper.contains(QLatin1String("LOGO"))
        || upper.contains(QLatin1String("MOD4")) || upper.contains(QLatin1String("META")))
        mods |= superBit;
    return mods;
}

QString comboOf(int mods, const QString &key)
{
    QString text = key.trimmed().toUpper();
    if (text == QLatin1String("ESC"))
        text = QStringLiteral("ESCAPE");
    // A key given by code has no name to compare with.
    if (text.isEmpty() || text.startsWith(QLatin1String("CODE:")))
        return {};
    QStringList parts;
    if (mods & superBit)
        parts << QStringLiteral("SUPER");
    if (mods & ctrlBit)
        parts << QStringLiteral("CTRL");
    if (mods & altBit)
        parts << QStringLiteral("ALT");
    if (mods & shiftBit)
        parts << QStringLiteral("SHIFT");
    parts << text;
    return parts.join(QLatin1Char('+'));
}

bool isOurs(const QString &description, const QString &arg)
{
    return description.startsWith(QLatin1String("Omastrator"), Qt::CaseInsensitive) || arg.contains(QLatin1String("omastrator"), Qt::CaseInsensitive);
}

// A bind Hyprland has now, in the default submap or another.
struct LiveBind {
    QString combo;
    bool release = false;
    bool mouse = false;
    bool defaultSubmap = true;
    // The one rule for "whose is this bind" (taken keys, the lost-keys check, the own-keys check): its own text says Omastrator,
    // or any bind on the same combo in the default submap does. Lua Hyprland reports every bind as "__lua" with a number, so a
    // release bind, or a hatch's second bind, has no text of its own; and key files older than this rule wrote some without any.
    bool ours = false;
};

std::optional<std::vector<LiveBind>> liveBinds()
{
    const QJsonValue binds = Hyprland::query(QStringLiteral("binds"));
    if (!binds.isArray())
        return std::nullopt;
    std::vector<LiveBind> all;
    QSet<QString> ourCombos;
    for (const QJsonValue &value : binds.toArray()) {
        const QJsonObject bind = value.toObject();
        LiveBind live;
        live.combo = comboOf(bind["modmask"].toInt() & (shiftBit | ctrlBit | altBit | superBit), bind["key"].toString());
        live.release = bind["release"].toBool();
        live.mouse = bind["mouse"].toBool();
        live.defaultSubmap = bind["submap"].toString().isEmpty();
        live.ours = isOurs(bind["description"].toString(), bind["arg"].toString());
        // Our own submaps bind plain keys (Escape, T): only the default submap's combos can vouch for a bind.
        if (live.ours && live.defaultSubmap && !live.mouse)
            ourCombos.insert(live.combo);
        all.push_back(live);
    }
    for (LiveBind &live : all)
        live.ours = live.ours || (live.defaultSubmap && !live.mouse && ourCombos.contains(live.combo));
    return all;
}

// What the user has bound in the default submap, "Super+1" or "Super+Alt+V (release)".
QSet<QString> userKeys(const std::vector<LiveBind> &binds)
{
    QSet<QString> keys;
    for (const LiveBind &bind : binds) {
        if (bind.defaultSubmap && !bind.ours && !bind.combo.isEmpty())
            keys.insert(Setup::displayCombo(bind.combo) + (bind.release ? QStringLiteral(" (release)") : QString()));
    }
    return keys;
}

void addLive(QSet<QString> *taken)
{
    const auto binds = liveBinds();
    for (const LiveBind &bind : binds.value_or(std::vector<LiveBind>())) {
        // Keys inside the user's own submaps and mouse binds can't be the ones setup takes.
        if (bind.defaultSubmap && !bind.mouse && !bind.ours && !bind.combo.isEmpty())
            taken->insert(bind.combo);
    }
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QStringList configFiles(const QString &folder)
{
    QStringList files;
    QDirIterator it(folder, {QStringLiteral("*.conf"), QStringLiteral("*.lua")}, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext())
        files << it.next();
    files.sort();
    return files;
}

void addLuaFile(const QByteArray &text, QSet<QString> *taken)
{
    static const QRegularExpression bind(QStringLiteral("(?<!\\w)(?:\\w+\\.)?bind\\w*\\(\\s*[\"']([^\"']*)[\"']"));
    static const QRegularExpression submap(QStringLiteral("^(\\s*).*define_submap\\("));
    // The user's own submaps run to the `end)` at the indentation they began at.
    QString closing;
    bool inSubmap = false;
    for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("--")))
            continue;
        if (inSubmap) {
            if (trimmed.startsWith(QLatin1String("end)")) && line.size() - line.trimmed().size() == closing.size())
                inSubmap = false;
            continue;
        }
        if (const auto opened = submap.match(line); opened.hasMatch() && !line.contains(QLatin1String("end)"))) {
            inSubmap = true;
            closing = opened.captured(1);
            continue;
        }
        if (line.contains(QLatin1String("omastrator"), Qt::CaseInsensitive))
            continue;
        const auto found = bind.match(line);
        if (!found.hasMatch())
            continue;
        const QString keys = found.captured(1);
        const qsizetype plus = keys.lastIndexOf(QLatin1Char('+'));
        const QString combo = plus < 0 ? comboOf(0, keys) : comboOf(modsOf(keys.left(plus)), keys.mid(plus + 1));
        if (!combo.isEmpty())
            taken->insert(combo);
    }
}

// hyprlang variables are global: one file defines $mainMod, another binds with it.
using Variables = std::map<QString, QString>;

void addConfVariables(const QByteArray &text, Variables *variables)
{
    static const QRegularExpression variable(QStringLiteral("^\\s*\\$(\\w+)\\s*=\\s*(.*?)\\s*$"));
    for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(QLatin1Char('#')))
            continue;
        if (const auto declared = variable.match(line); declared.hasMatch())
            (*variables)[declared.captured(1)] = declared.captured(2);
    }
}

// A whole name at a time, so $mod never eats the start of $modAlt; values may use other variables.
QString withVariables(QString text, const Variables &variables)
{
    static const QRegularExpression name(QStringLiteral("\\$(\\w+)"));
    for (int pass = 0; pass < 4; ++pass) {
        QString replaced;
        qsizetype from = 0;
        bool changed = false;
        for (auto match = name.globalMatch(text); match.hasNext();) {
            const auto found = match.next();
            const auto value = variables.find(found.captured(1));
            replaced += text.mid(from, found.capturedStart() - from);
            if (value == variables.end()) {
                replaced += found.captured();
            } else {
                replaced += value->second;
                changed = true;
            }
            from = found.capturedEnd();
        }
        replaced += text.mid(from);
        text = replaced;
        if (!changed)
            break;
    }
    return text;
}

void addConfFile(const QByteArray &text, const Variables &variables, QSet<QString> *taken)
{
    static const QRegularExpression bind(QStringLiteral("^\\s*bind([a-z]*)\\s*=\\s*(.*)$"));
    bool inSubmap = false;
    for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (trimmed.startsWith(QLatin1Char('$')))
            continue;
        if (trimmed.startsWith(QLatin1String("submap"))) {
            const qsizetype equals = trimmed.indexOf(QLatin1Char('='));
            if (equals > 0)
                inSubmap = trimmed.mid(equals + 1).trimmed() != QLatin1String("reset");
            continue;
        }
        if (inSubmap || line.contains(QLatin1String("omastrator"), Qt::CaseInsensitive))
            continue;
        const auto found = bind.match(line);
        // bindm binds the mouse.
        if (!found.hasMatch() || found.captured(1).contains(QLatin1Char('m')))
            continue;
        const QStringList fields = withVariables(found.captured(2), variables).split(QLatin1Char(','));
        if (fields.size() < 2)
            continue;
        const QString combo = comboOf(modsOf(fields[0]), fields[1]);
        if (!combo.isEmpty())
            taken->insert(combo);
    }
}
}

namespace Setup {
QString normalizeCombo(const QString &lua)
{
    const qsizetype plus = lua.lastIndexOf(QLatin1Char('+'));
    return plus < 0 ? comboOf(0, lua) : comboOf(modsOf(lua.left(plus)), lua.mid(plus + 1));
}

QString displayCombo(const QString &normalized)
{
    QStringList parts = normalized.split(QLatin1Char('+'));
    for (QString &part : parts)
        part = part.left(1) + part.mid(1).toLower();
    return parts.join(QLatin1Char('+'));
}

QStringList omastratorKeys(const DesignKeys &keys)
{
    QStringList combos;
    for (const char *key : {"V", "Escape"})
        combos << normalizeCombo(QStringLiteral("SUPER + ALT + ") + QLatin1String(key));
    combos << normalizeCombo(keys.design) << normalizeCombo(keys.desk);
    combos.removeDuplicates();
    return combos;
}

QSet<QString> takenKeys(const Environment &environment)
{
    QSet<QString> taken;
    addLive(&taken);
    QStringList files = configFiles(environment.hyprDirectory());
    files << configFiles(QDir(environment.omarchyPath).filePath(QStringLiteral("default/hypr")));
    std::map<QString, QByteArray> texts;
    Variables variables;
    for (const QString &path : std::as_const(files)) {
        texts[path] = fileBytes(path);
        if (!path.endsWith(QLatin1String(".lua")))
            addConfVariables(texts[path], &variables);
    }
    for (const QString &path : std::as_const(files)) {
        if (path.endsWith(QLatin1String(".lua")))
            addLuaFile(texts[path], &taken);
        else
            addConfFile(texts[path], variables, &taken);
    }
    return taken;
}

KeyChoice chooseKeys(const Environment &environment)
{
    KeyChoice choice;
    const QSet<QString> taken = takenKeys(environment);
    for (const QString &combo : omastratorKeys(DesignKeys::from(environment))) {
        if (!taken.contains(combo))
            continue;
        choice.skip << combo;
        choice.skipped << displayCombo(combo);
    }
    return choice;
}

std::optional<QSet<QString>> liveUserBinds()
{
    const auto binds = liveBinds();
    if (!binds)
        return std::nullopt;
    return userKeys(*binds);
}

BindCheck checkBinds(const QSet<QString> &before, const QStringList &ownKeys, int waitMs)
{
    BindCheck check;
    // Hyprland answers the reload before every bind is back, so look again for a moment before calling one lost.
    for (int waited = 0;; waited += 200) {
        check = {};
        const auto now = liveBinds();
        if (!now)
            return check;
        check.answered = true;
        const QSet<QString> user = userKeys(*now);
        QSet<QString> bound;
        for (const LiveBind &bind : *now)
            bound.insert(bind.combo);
        for (const QString &key : before) {
            if (!user.contains(key))
                check.lost << key;
        }
        for (const QString &key : ownKeys) {
            if (!bound.contains(key))
                check.missing << displayCombo(key);
        }
        if ((check.lost.isEmpty() && check.missing.isEmpty()) || waited >= waitMs)
            break;
        QThread::msleep(200);
    }
    check.lost.sort();
    check.missing.sort();
    return check;
}
}
