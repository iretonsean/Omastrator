#pragma once
#include "Document/EditorSession.h"
#include "UI/PanelSection.h"
#include <QCheckBox>
#include <QComboBox>
#include <functional>
#include <vector>

class NumberField;

// Properties' Paragraph section, shown for area type: indents, first-line
// indent, space before and after, where a justified paragraph's last line sits,
// and automatic hyphenation (P2-5), its margins behind a disclosure.
// It edits the selected texts' paragraphs, or those the selected characters touch.
class ParagraphSection : public PanelSection {
    Q_OBJECT
public:
    ParagraphSection(EditorSession &session, QWidget *parent);
    void synchronize();

private:
    NumberField *number(const QString &label, const QString &name, const QString &undo, double ParagraphFormat::*member, const QString &tip);
    NumberField *count(const QString &label, const QString &name, const QString &undo, int ParagraphFormat::*member, const QString &tip);

    EditorSession &m_session;
    QComboBox *const m_lastLine;
    QWidget *const m_lastLineRow;
    QCheckBox *const m_hyphenate;
    QWidget *const m_hyphenateOptions;
    std::vector<std::pair<NumberField *, double ParagraphFormat::*>> m_numbers;
    std::vector<std::pair<NumberField *, int ParagraphFormat::*>> m_counts;
};
