#include "Live/EditSets.h"
#include "Live/LiveSession.h"
#include "Live/Registry.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <map>

namespace {
QJsonObject load()
{
    QFile file(EditSets::path());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QString save(const QJsonObject &all)
{
    const QString path = EditSets::path();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(all).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Couldn't save the edit set to %1: %2").arg(path, file.errorString());
    return {};
}

EditSets::Set setFrom(const QJsonObject &json)
{
    EditSets::Set set;
    set.name = json["name"].toString();
    set.enabled = json["enabled"].toBool(true);
    set.updated = QDateTime::fromString(json["updated"].toString(), Qt::ISODate);
    for (const QJsonValue &edit : json["edits"].toArray())
        set.edits.push_back(EditSets::Edit::fromJson(edit.toObject()));
    return set;
}

QJsonObject setJson(const EditSets::Set &set)
{
    return {{"name", set.name}, {"enabled", set.enabled}, {"updated", set.updated.toString(Qt::ISODate)}, {"edits", EditSets::toJson(set.edits)}};
}

QString writeAll(const QString &origin, const std::vector<EditSets::Set> &sets)
{
    QJsonObject all = load();
    QJsonObject origins = all["origins"].toObject();
    QJsonArray list;
    for (const EditSets::Set &set : sets)
        list.append(setJson(set));
    if (list.isEmpty())
        origins.remove(origin);
    else
        origins[origin] = QJsonObject{{"sets", list}};
    all["version"] = 1;
    all["origins"] = origins;
    return save(all);
}

// A value that snapped to one of the page's custom properties is written as that property, so it follows the site.
QString cssValue(const EditSets::Edit &edit)
{
    return edit.token.startsWith(QLatin1String("--")) ? QStringLiteral("var(%1)").arg(edit.token) : edit.value;
}

QString quoted(QString text)
{
    text.replace(QLatin1String("*/"), QLatin1String("* /"));
    return QLatin1Char('"') + text.simplified().left(200) + QLatin1Char('"');
}

// Each page's rules, selectors in the order they were first changed.
QString rules(const std::vector<EditSets::Edit> &edits, const QString &indent, QStringList *texts)
{
    std::vector<std::pair<QString, QStringList>> blocks;
    for (const EditSets::Edit &edit : edits) {
        if (edit.property == QLatin1String("text")) {
            texts->append(QStringLiteral("%1: %2 → %3").arg(edit.selector, quoted(edit.before), quoted(edit.value)));
            continue;
        }
        auto block = std::find_if(blocks.begin(), blocks.end(), [&](const auto &each) { return each.first == edit.selector; });
        if (block == blocks.end())
            block = blocks.insert(blocks.end(), {edit.selector, {}});
        block->second << QStringLiteral("%1  %2: %3 !important;").arg(indent, edit.property, cssValue(edit));
    }
    QString out;
    for (const auto &[selector, lines] : blocks)
        out += QStringLiteral("%1%2 {\n%3\n%1}\n").arg(indent, selector, lines.join(QLatin1Char('\n')));
    return out;
}
}

namespace EditSets {
QJsonObject Edit::toJson() const
{
    QJsonObject json{{"path", path}, {"selector", selector}, {"property", property}, {"value", value}, {"before", before}};
    if (!token.isEmpty())
        json["token"] = token;
    if (!addClass.isEmpty())
        json["addClass"] = addClass;
    if (!removeClass.isEmpty())
        json["removeClass"] = removeClass;
    return json;
}

Edit Edit::fromJson(const QJsonObject &json)
{
    return {json["path"].toString(QStringLiteral("/")), json["selector"].toString(), json["property"].toString(), json["value"].toString(),
            json["before"].toString(), json["token"].toString(), json["addClass"].toString(), json["removeClass"].toString()};
}

Edit Edit::fromLive(const LiveEdit &edit)
{
    return {edit.path.isEmpty() ? QStringLiteral("/") : edit.path, edit.selector, edit.property, edit.after, edit.before, edit.token,
            edit.addClass, edit.removeClass};
}

QJsonObject Set::summary() const
{
    return {{"name", name}, {"enabled", enabled}, {"edits", int(edits.size())}, {"updated", updated.toString(Qt::ISODate)}};
}

QString path()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    const QString home = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
    return QDir(home).filePath(QStringLiteral("omastrator/edit-sets.json"));
}

QString originOf(const QUrl &url)
{
    if (url.isLocalFile())
        return QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath()).toString();
    return ProjectRegistry::originOf(url);
}

QString pathOf(const QUrl &url)
{
    if (url.isLocalFile())
        return QFileInfo(url.toLocalFile()).fileName();
    return url.path().isEmpty() ? QStringLiteral("/") : url.path();
}

std::vector<Set> read(const QString &origin)
{
    std::vector<Set> sets;
    for (const QJsonValue &set : load()["origins"].toObject()[origin].toObject()["sets"].toArray())
        sets.push_back(setFrom(set.toObject()));
    return sets;
}

QString keep(const QString &origin, const QString &name, const std::vector<Edit> &edits)
{
    if (origin.isEmpty())
        return QStringLiteral("This page has no address to keep edits for.");
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("Give the edit set a name.");
    if (edits.empty())
        return QStringLiteral("There are no edits on this page to keep.");
    std::vector<Set> sets = read(origin);
    auto set = std::find_if(sets.begin(), sets.end(), [&](const Set &each) { return each.name == trimmed; });
    if (set == sets.end())
        set = sets.insert(sets.end(), Set{trimmed, true, {}, {}});
    for (const Edit &edit : edits) {
        auto same = std::find_if(set->edits.begin(), set->edits.end(), [&](const Edit &each) {
            return each.path == edit.path && each.selector == edit.selector && each.property == edit.property;
        });
        if (same == set->edits.end()) {
            set->edits.push_back(edit);
        } else {
            // The page's own value stays the one from before the first change.
            Edit merged = edit;
            merged.before = same->before;
            if (!same->removeClass.isEmpty() && merged.removeClass == same->addClass)
                merged.removeClass = same->removeClass;
            *same = merged;
        }
    }
    set->enabled = true;
    set->updated = QDateTime::currentDateTime();
    return writeAll(origin, sets);
}

QString setEnabled(const QString &origin, const QString &name, bool enabled)
{
    std::vector<Set> sets = read(origin);
    auto set = std::find_if(sets.begin(), sets.end(), [&](const Set &each) { return each.name == name; });
    if (set == sets.end())
        return QStringLiteral("There's no edit set called “%1” for this site.").arg(name);
    set->enabled = enabled;
    return writeAll(origin, sets);
}

QString remove(const QString &origin, const QString &name)
{
    std::vector<Set> sets = read(origin);
    const auto before = sets.size();
    std::erase_if(sets, [&](const Set &each) { return each.name == name; });
    if (sets.size() == before)
        return QStringLiteral("There's no edit set called “%1” for this site.").arg(name);
    return writeAll(origin, sets);
}

QString suggestedName(const QString &origin)
{
    const std::vector<Set> sets = read(origin);
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("Edits %1").arg(n);
        if (std::none_of(sets.begin(), sets.end(), [&](const Set &set) { return set.name == name; }))
            return name;
    }
}

std::vector<Edit> active(const QString &origin, const QString &path)
{
    std::vector<Edit> edits;
    for (const Set &set : read(origin)) {
        if (!set.enabled)
            continue;
        for (const Edit &edit : set.edits)
            if (edit.path == path)
                edits.push_back(edit);
    }
    return edits;
}

QString css(const QString &origin, const QString &name, const std::vector<Edit> &edits, bool userstyle)
{
    // Pages in the order they were first edited.
    std::vector<std::pair<QString, std::vector<Edit>>> pages;
    for (const Edit &edit : edits) {
        auto page = std::find_if(pages.begin(), pages.end(), [&](const auto &each) { return each.first == edit.path; });
        if (page == pages.end())
            page = pages.insert(pages.end(), {edit.path, {}});
        page->second.push_back(edit);
    }
    const QString title = name.isEmpty() ? QStringLiteral("Edits") : name;
    QString out;
    if (userstyle) {
        out += QStringLiteral("/* ==UserStyle==\n@name           %1: %2\n@namespace      omastrator\n@version        1.0.0\n"
                              "@description    Kept in Omastrator. Not your site: these changes stay on this machine.\n==/UserStyle== */\n\n")
                   .arg(QUrl(origin).host().isEmpty() ? origin : QUrl(origin).host(), title);
    } else {
        out += QStringLiteral("/* %1 for %2, from Omastrator.\n   Not your site: these changes stay on this machine. */\n\n").arg(title, origin);
    }
    QStringList texts;
    for (const auto &[page, list] : pages) {
        const QString address = origin.startsWith(QLatin1String("file:")) ? origin + QLatin1Char('/') + page : origin + page;
        if (userstyle) {
            const QString body = rules(list, QStringLiteral("  "), &texts);
            if (!body.isEmpty())
                out += QStringLiteral("@-moz-document url-prefix(\"%1\") {\n%2}\n\n").arg(address, body);
        } else {
            const QString body = rules(list, QString(), &texts);
            if (!body.isEmpty())
                out += QStringLiteral("/* %1 */\n%2\n").arg(address, body);
        }
    }
    if (!texts.isEmpty())
        out += QStringLiteral("/* Text changes can't be made with CSS:\n   %1\n*/\n").arg(texts.join(QStringLiteral("\n   ")));
    return out;
}

QString diff(const std::vector<Edit> &edits)
{
    QStringList lines;
    for (const Edit &edit : edits) {
        if (edit.property == QLatin1String("text"))
            lines << QStringLiteral("%1 %2 { text: %3 → %4 }").arg(edit.path, edit.selector, quoted(edit.before), quoted(edit.value));
        else
            lines << QStringLiteral("%1 %2 { %3: %4 → %5%6 }")
                         .arg(edit.path, edit.selector, edit.property, edit.before.isEmpty() ? QStringLiteral("(unset)") : edit.before, edit.value,
                              edit.token.isEmpty() ? QString() : QStringLiteral(" (the page's %1)").arg(edit.token));
    }
    return lines.join(QLatin1Char('\n'));
}

QJsonArray toJson(const std::vector<Edit> &edits)
{
    QJsonArray list;
    for (const Edit &edit : edits)
        list.append(edit.toJson());
    return list;
}
}
