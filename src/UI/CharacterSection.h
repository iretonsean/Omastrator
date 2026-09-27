#pragma once
#include "Document/EditorSession.h"
#include "UI/PanelSection.h"
#include <QComboBox>
#include <QFontComboBox>
#include <QPushButton>
#include <QMenu>
#include <array>

class NumberField;

// Text alignment's glyphs and names, shared with the Type tool's bar.
namespace TypeAlignment {
inline constexpr std::array<TextAlignment, 5> all{TextAlignment::left, TextAlignment::center, TextAlignment::right, TextAlignment::justify,
                                                  TextAlignment::justifyAll};
QPixmap glyph(TextAlignment alignment, const QColor &ink, double ratio);
QString name(TextAlignment alignment);
}

// Properties' Character section: the essentials, and the rest behind Show more.
// It edits every selected text, or the next text when none is selected.
class CharacterSection : public PanelSection {
    Q_OBJECT
public:
    CharacterSection(EditorSession &session, QWidget *parent);
    void synchronize();
    // Whether Show more is open; remembered across launches.
    bool showsMore() const;
    void setShowsMore(bool shown);

protected:
    void changeEvent(QEvent *event) override;

private:
    // The text style button in the heading, and its menu.
    void buildStyles();
    void synchronizeStyles();
    QMenu *stylesMenu();
    NumberField *number(const QString &label, const QString &suffix, const QString &name, const QString &undo,
                        const std::function<void(TextContent &, double)> &set, const std::function<double(const TextContent &)> &get);
    QToolButton *toggleButton(const QString &name, const QString &text, const QString &tip);
    void change(const std::function<void(TextContent &)> &edit, const QString &name);
    void applyGlyphs();

    EditorSession &m_session;
    QFontComboBox *const m_family;
    QComboBox *const m_style;
    NumberField *m_size = nullptr;
    NumberField *m_leading = nullptr;
    NumberField *m_tracking = nullptr;
    std::array<QToolButton *, 5> m_alignments{};
    QPushButton *const m_more;
    QWidget *const m_extra;
    QComboBox *const m_kerning;
    NumberField *m_baseline = nullptr;
    NumberField *m_horizontal = nullptr;
    NumberField *m_vertical = nullptr;
    QComboBox *const m_case;
    QToolButton *m_underline = nullptr;
    QToolButton *m_strikethrough = nullptr;
    QComboBox *const m_kind;
    QToolButton *m_styles = nullptr;
    // OpenType features, behind their own button; hidden where Qt can't apply them.
    QToolButton *m_openType = nullptr;
    // Fields a number is read from, to sync them together.
    std::vector<std::pair<NumberField *, std::function<double(const TextContent &)>>> m_numbers;
};
