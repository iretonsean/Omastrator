#include "UI/ParagraphSection.h"
#include "UI/NumberField.h"
#include "UI/ToolHeaderStyle.h"
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <algorithm>
#include <array>

namespace {
QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(ToolHeaderStyle::controlFont());
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// Justify's last line: left, centred or right, by its place in the menu.
constexpr std::array lastLines{TextAlignment::justify, TextAlignment::justifyCenter, TextAlignment::justifyRight};
}

ParagraphSection::ParagraphSection(EditorSession &session, QWidget *parent)
    : PanelSection(QStringLiteral("Paragraph"), QStringLiteral("paragraph"), parent), m_session(session), m_lastLine(new QComboBox(this)),
      m_lastLineRow(new QWidget(this))
{
    NumberField *left = number(QStringLiteral("Left"), QStringLiteral("paragraphLeftIndent"), QStringLiteral("Left Indent"), &ParagraphFormat::leftIndent,
                               QStringLiteral("Left indent"));
    NumberField *right = number(QStringLiteral("Right"), QStringLiteral("paragraphRightIndent"), QStringLiteral("Right Indent"),
                                &ParagraphFormat::rightIndent, QStringLiteral("Right indent"));
    NumberField *first = number(QStringLiteral("First"), QStringLiteral("paragraphFirstLineIndent"), QStringLiteral("First-Line Indent"),
                                &ParagraphFormat::firstLineIndent, QStringLiteral("First-line indent: below 0 hangs the first line"));
    first->minimum = -1296;
    NumberField *before = number(QStringLiteral("Before"), QStringLiteral("paragraphSpaceBefore"), QStringLiteral("Space Before"),
                                 &ParagraphFormat::spaceBefore, QStringLiteral("Space before the paragraph"));
    NumberField *after = number(QStringLiteral("After"), QStringLiteral("paragraphSpaceAfter"), QStringLiteral("Space After"),
                                &ParagraphFormat::spaceAfter, QStringLiteral("Space after the paragraph"));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addWidget(caption(QStringLiteral("Indent"), this), 0, 0);
    grid->addWidget(left, 0, 1);
    grid->addWidget(right, 0, 2);
    grid->addWidget(first, 1, 1);
    grid->addWidget(caption(QStringLiteral("Space"), this), 2, 0);
    grid->addWidget(before, 2, 1);
    grid->addWidget(after, 2, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    body->addLayout(grid);

    // Only a justified paragraph has a last line to place.
    auto *lastRow = new QHBoxLayout(m_lastLineRow);
    lastRow->setContentsMargins(0, 0, 0, 0);
    lastRow->setSpacing(8);
    m_lastLine->setObjectName(QStringLiteral("paragraphLastLine"));
    m_lastLine->setAccessibleName(QStringLiteral("Last line"));
    m_lastLine->setToolTip(QStringLiteral("Where a justified paragraph's last line sits"));
    m_lastLine->setFont(ToolHeaderStyle::controlFont());
    m_lastLine->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    connect(m_lastLine, &QComboBox::activated, this, [this](int index) {
        const TextAlignment alignment = lastLines.at(size_t(std::clamp(index, 0, 2)));
        m_session.updateText([alignment](TextContent &text) {
            if (text.alignment != TextAlignment::justifyAll && isJustified(text.alignment))
                text.alignment = alignment;
        }, QStringLiteral("Alignment"));
    });
    lastRow->addWidget(caption(QStringLiteral("Last line"), m_lastLineRow));
    lastRow->addWidget(m_lastLine, 1);
    body->addWidget(m_lastLineRow);
}

NumberField *ParagraphSection::number(const QString &label, const QString &name, const QString &undo, double ParagraphFormat::*member, const QString &tip)
{
    auto *made = new NumberField(label, QStringLiteral("pt"), [this, member, undo](double value) {
        m_session.updateText([member, value](TextContent &text) { text.paragraph().*member = value; }, undo);
    }, this);
    made->setObjectName(name);
    made->field->setObjectName(name + QStringLiteral("Field"));
    made->field->setAccessibleName(undo);
    made->setToolTip(tip);
    made->lengths = true;
    made->minimum = 0;
    made->maximum = 5000;
    made->changeEach = [this, member, undo](const std::function<double(double)> &each) {
        m_session.updateText([&](TextContent &text) { text.paragraph().*member = each(text.paragraph().*member); }, undo);
    };
    made->gesture = [this, undo](bool starting) {
        if (starting)
            m_session.beginEdit(undo);
        else
            m_session.endEdit();
    };
    m_numbers.push_back({made, member});
    return made;
}

void ParagraphSection::synchronize()
{
    const std::vector<TextContent> texts = m_session.shownTexts();
    for (const auto &[field, member] : m_numbers) {
        const double first = texts.front().paragraph().*member;
        const bool mixed = std::any_of(texts.begin(), texts.end(), [&](const TextContent &text) { return text.paragraph().*member != first; });
        if (mixed)
            field->syncMixed();
        else
            field->sync(first);
    }
    const bool lastLine = std::all_of(texts.begin(), texts.end(), [](const TextContent &text) {
        return isJustified(text.alignment) && text.alignment != TextAlignment::justifyAll;
    });
    m_lastLineRow->setVisible(lastLine);
    const QSignalBlocker quiet(m_lastLine);
    const TextAlignment shown = texts.front().alignment;
    const bool same = std::all_of(texts.begin(), texts.end(), [&](const TextContent &text) { return text.alignment == shown; });
    const auto at = std::find(lastLines.begin(), lastLines.end(), shown);
    m_lastLine->setPlaceholderText(QStringLiteral("Mixed"));
    m_lastLine->setCurrentIndex(same && at != lastLines.end() ? int(at - lastLines.begin()) : -1);
}
