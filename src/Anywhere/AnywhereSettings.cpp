#include "Anywhere/AnywhereSettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
QJsonArray options(std::initializer_list<std::pair<const char *, const char *>> list)
{
    QJsonArray array;
    for (const auto &[id, label] : list)
        array.append(QJsonObject{{"id", QLatin1String(id)}, {"label", QString::fromUtf8(label)}});
    return array;
}

QStringList optionIds(const AnywhereSettings::Question &question)
{
    QStringList ids;
    for (const QJsonValue &option : question.options)
        ids << option.toObject()["id"].toString();
    return ids;
}
}

namespace AnywhereSettings {
QString path()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : given;
    return QDir(config).filePath(QStringLiteral("omastrator/anywhere.json"));
}

QJsonObject read()
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString write(const QJsonObject &settings)
{
    const QString target = path();
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        return QStringLiteral("Could not create %1.").arg(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    const QByteArray bytes = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(target, file.errorString());
    return {};
}

const std::vector<Question> &questions()
{
    static const std::vector<Question> all{
        {QStringLiteral("makes"), QStringLiteral("What do you make most?"), true,
         options({{"web", "UI for web apps"}, {"rice", "Omarchy themes and rices"}, {"icons", "Icons and illustration"},
                  {"marketing", "Marketing and social"}})},
        {QStringLiteral("from"), QStringLiteral("Which tools are you coming from?"), true,
         options({{"figma", "Figma"}, {"illustrator", "Illustrator"}, {"paper", "Paper"}, {"inkscape", "Inkscape"}, {"code", "Code first"},
                  {"new", "New to design tools"}})},
        {QStringLiteral("ai"), QStringLiteral("How much should AI suggest on its own?"), false,
         options({{"quiet", "Only when I ask"}, {"some", "A few suggestions"}, {"lots", "Suggest freely"}})},
    };
    return all;
}

QString privacyNote()
{
    return QStringLiteral("Captures of other apps stay on this machine. Nothing goes to an agent unless you ask.");
}

QJsonObject Answers::toJson() const
{
    return {{"makes", QJsonArray::fromStringList(makes)}, {"from", QJsonArray::fromStringList(from)}, {"ai", ai}, {"done", done}};
}

Answers Answers::fromJson(const QJsonObject &json)
{
    Answers answers;
    for (const QJsonValue &value : json["makes"].toArray())
        answers.makes << value.toString();
    for (const QJsonValue &value : json["from"].toArray())
        answers.from << value.toString();
    const QString ai = json["ai"].toString();
    if (ai == QLatin1String("quiet") || ai == QLatin1String("some") || ai == QLatin1String("lots"))
        answers.ai = ai;
    answers.done = json["done"].toBool();
    return answers;
}

Answers answers()
{
    return Answers::fromJson(read()["onboarding"].toObject());
}

QString setAnswer(const QString &question, const QStringList &values)
{
    const auto found = std::find_if(questions().begin(), questions().end(), [&](const Question &each) { return each.id == question; });
    if (found == questions().end())
        return QStringLiteral("There is no onboarding question “%1”.").arg(question);
    const QStringList allowed = optionIds(*found);
    for (const QString &value : values) {
        if (!allowed.contains(value))
            return QStringLiteral("“%1” isn't an answer to “%2”. Choose from: %3.").arg(value, found->text, allowed.join(QStringLiteral(", ")));
    }
    if (!found->multiple && values.size() != 1)
        return QStringLiteral("Choose one answer to “%1”.").arg(found->text);
    QJsonObject settings = read();
    QJsonObject onboarding = settings["onboarding"].toObject();
    onboarding[question] = found->multiple ? QJsonValue(QJsonArray::fromStringList(values)) : QJsonValue(values.first());
    settings["onboarding"] = onboarding;
    return write(settings);
}

QString finish(bool skipped)
{
    QJsonObject settings = read();
    QJsonObject onboarding = settings["onboarding"].toObject();
    onboarding["done"] = true;
    onboarding["skipped"] = skipped;
    settings["onboarding"] = onboarding;
    return write(settings);
}

bool needsOnboarding()
{
    return !answers().done;
}

const QStringList &destinations()
{
    static const QStringList all{QStringLiteral("overlay"), QStringLiteral("desk"), QStringLiteral("document"), QStringLiteral("source"),
                                 QStringLiteral("agent")};
    return all;
}

QString destination(const QString &surfaceKey)
{
    const QString chosen = read()["destinations"].toObject()[surfaceKey].toString();
    return destinations().contains(chosen) ? chosen : QStringLiteral("overlay");
}

QString setDestination(const QString &surfaceKey, const QString &destination)
{
    if (!destinations().contains(destination))
        return QStringLiteral("Send work to one of: %1.").arg(destinations().join(QStringLiteral(", ")));
    QJsonObject settings = read();
    QJsonObject chosen = settings["destinations"].toObject();
    if (chosen[surfaceKey].toString() == destination)
        return {};
    chosen[surfaceKey] = destination;
    settings["destinations"] = chosen;
    return write(settings);
}

QString handoffFolder(const QString &surfaceKey)
{
    return read()["handoff"].toObject()[surfaceKey].toString();
}

QString setHandoffFolder(const QString &surfaceKey, const QString &folder)
{
    QJsonObject settings = read();
    QJsonObject folders = settings["handoff"].toObject();
    if (folders[surfaceKey].toString() == folder)
        return {};
    folders[surfaceKey] = folder;
    settings["handoff"] = folders;
    return write(settings);
}

bool barFollowsFocus()
{
    return read()["barFollowsFocus"].toBool(false);
}

QString setBarFollowsFocus(bool follows)
{
    QJsonObject settings = read();
    if (settings["barFollowsFocus"].toBool(false) == follows)
        return {};
    settings["barFollowsFocus"] = follows;
    return write(settings);
}

QString deskWorkspace()
{
    const QString chosen = read()["deskWorkspace"].toString().trimmed();
    return chosen.isEmpty() ? QStringLiteral("special:omastrator-desk") : chosen;
}
}
