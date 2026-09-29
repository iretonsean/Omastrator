#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// What design mode remembers between runs, in $XDG_CONFIG_HOME/omastrator/anywhere.json:
// the onboarding answers, where each surface's work goes by default, and the Desk's workspace.
namespace AnywhereSettings {
QString path();
QJsonObject read();
// Returns why it failed, or empty.
QString write(const QJsonObject &settings);

// Onboarding (docs/ANYWHERE.md): a few questions about the designer's work, all skippable.
struct Question {
    QString id;
    QString text;
    bool multiple = false;
    // Each option: {id, label}.
    QJsonArray options;
};
const std::vector<Question> &questions();
// Said plainly before the questions.
QString privacyNote();

struct Answers {
    // What they make: web, rice, icons, marketing.
    QStringList makes;
    // Where they come from: figma, illustrator, paper, inkscape, code, new.
    QStringList from;
    // How much AI suggests on its own: quiet, some or lots.
    QString ai = QStringLiteral("some");
    // Answered or skipped: onboarding doesn't open by itself again.
    bool done = false;
    QJsonObject toJson() const;
    static Answers fromJson(const QJsonObject &json);
};
Answers answers();
// Checks each value against the questions; returns why it can't, or empty.
QString setAnswer(const QString &question, const QStringList &values);
QString finish(bool skipped);
// Onboarding opens by itself the first time design mode starts, until answered or skipped.
bool needsOnboarding();

// Where work on a surface goes: overlay, desk, document, source or agent.
const QStringList &destinations();
QString destination(const QString &surfaceKey);
QString setDestination(const QString &surfaceKey, const QString &destination);
// The app's source folder Hand to Agent last used for a surface, offered again next time.
QString handoffFolder(const QString &surfaceKey);
QString setHandoffFolder(const QString &surfaceKey, const QString &folder);

// "Bar follows focus": the floating bar goes wherever the pointer or focus goes, as it did before it began
// sticking to the app design mode started on. Off unless turned on.
bool barFollowsFocus();
QString setBarFollowsFocus(bool follows);

// The Desk's Hyprland workspace: a named special workspace unless changed.
QString deskWorkspace();
}
