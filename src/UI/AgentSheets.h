#pragma once
#include <QDialog>
#include <QString>
#include <functional>

class AgentBridge;
class QBoxLayout;

// The sheets the AI entries open. A launch failure shows in the sheet, which stays open.
namespace AgentSheets {
// Object ▸ Generate…: a prompt, 1 to 6 variations, and Fit to selection when there is one.
QDialog *generate(AgentBridge &bridge, QWidget *window, const QString &prompt = QString());
// Object ▸ Edit with Instruction…
QDialog *editWithInstruction(AgentBridge &bridge, QWidget *window);
// Object ▸ Image Trace ▸ Vectorize with AI…
QDialog *vectorize(AgentBridge &bridge, QWidget *window);
// The island's Live mode: a page, and which folder its code is in (suggested, or none for a mock-up).
QDialog *live(AgentBridge &bridge, QWidget *window);
// Live's Deploy (or Save) when there's something to answer first: production the first time, and a GitHub repository.
QDialog *deploy(AgentBridge &bridge, QWidget *window, const QString &folder = QString(), bool deploying = true, bool fromFrame = false);
// Details: a deploy's log, redacted. With a bridge and the project's folder, a failed deploy's window also offers Fix with <agent>
// (AgentSheets+DeployFix.cpp) and, once that has finished, Deploy again.
QDialog *deployLog(QWidget *window, const QString &path, AgentBridge *bridge = nullptr, const QString &folder = QString());
// File ▸ Hand to Agent…: the document in front as a mockup, for an app whose source is in a folder.
QDialog *handoff(AgentBridge &bridge, QWidget *window);
// Hand to Agent… from a surface (docs/ANYWHERE.md): `what` names it; `folder` is offered first; `run` starts it with the
// folder and the notes, and returns why it couldn't.
QDialog *handoffFrom(QWidget *window, const QString &what, const QString &folder,
                     const std::function<QString(const QString &folder, const QString &notes)> &run);
// Help ▸ Connect an Agent…
QDialog *connectAgent(AgentBridge &bridge, QWidget *window);
// Deploy Details' fix row: Fix with <agent>, its state and Stop, and Deploy again. Hidden with the agent's hint when there is no default agent.
void addDeployFix(QDialog *dialog, QBoxLayout *column, AgentBridge &bridge, const QString &folder);
// The text Connect an Agent shows.
QString connectText(const AgentBridge &bridge);
}
