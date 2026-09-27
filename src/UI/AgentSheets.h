#pragma once
#include <QDialog>
#include <QString>

class AgentBridge;

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
// Help ▸ Connect an Agent…
QDialog *connectAgent(AgentBridge &bridge, QWidget *window);
// The text Connect an Agent shows.
QString connectText(const AgentBridge &bridge);
}
