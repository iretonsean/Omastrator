#pragma once
#include "Document/EditorSession.h"
#include "UI/PanelSection.h"
#include <QComboBox>
#include <functional>
#include <vector>

class NumberField;

// Properties' Paragraph section, shown for area type: indents, first-line
// indent, space before and after, and where a justified paragraph's last line sits.
// It edits the selected texts' paragraphs, or those the selected characters touch.
class ParagraphSection : public PanelSection {
    Q_OBJECT
public:
    ParagraphSection(EditorSession &session, QWidget *parent);
    void synchronize();

private:
    NumberField *number(const QString &label, const QString &name, const QString &undo, double ParagraphFormat::*member, const QString &tip);

    EditorSession &m_session;
    QComboBox *const m_lastLine;
    QWidget *const m_lastLineRow;
    std::vector<std::pair<NumberField *, double ParagraphFormat::*>> m_numbers;
};
