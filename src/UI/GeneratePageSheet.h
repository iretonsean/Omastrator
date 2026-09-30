#pragma once
#include "Live/PageTemplates.h"
#include <QString>
#include <functional>
#include <optional>

class QWidget;

// The sheet Generate a page opens from an empty Browser View (docs/MOTION.md, section 4): what the page should be, the stack
// to build it with, and the folder for the new project. Build It from an empty frame opens it without the description.
namespace GeneratePageSheet {
struct Answer {
    QString description;
    PageTemplates::Stack stack = PageTemplates::Stack::viteTailwind;
    QString folder;
};
// `describe` false: the Build It sheet, which asks for the stack and the folder only. `run` gets the answer and returns why
// it can't go on; the sheet shows that and stays open. A test's responder answers in its place, and then the return value
// is what `run` said.
QString open(QWidget *window, bool describe, const QString &frameName, const std::function<QString(const Answer &)> &run);
// Tests answer the sheet: return nothing to cancel. An empty function puts the dialog back.
void setResponder(std::function<std::optional<Answer>(bool describe)> responder);
// A folder for a new project, from the words that describe it: under ~/Projects (else home), not one that has files in it.
QString suggestedFolder(const QString &description);
}
