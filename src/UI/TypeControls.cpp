#include "UI/CharacterSection.h"
#include "UI/NumberField.h"
#include "UI/ToolHeaders.h"
#include <QEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <cmath>

namespace {
const std::array<TextAlignment, 4> alignments{TextAlignment::left, TextAlignment::center, TextAlignment::right, TextAlignment::justify};
}

TypeControls::TypeControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Type"), parent), m_session(session), m_family(new QFontComboBox(this)),
      m_size(new NumberField(QString(), QStringLiteral("pt"), [this](double size) {
          change([size](TextContent &text) { text.size = std::clamp(size, 0.1, 1296.0); }, QStringLiteral("Font Size"));
      }, this)),
      m_style(new QComboBox(this)),
      m_alignments{toggle(QStringLiteral("typeAlignLeft"), QString(), TypeAlignment::name(TextAlignment::left)),
                   toggle(QStringLiteral("typeAlignCenter"), QString(), TypeAlignment::name(TextAlignment::center)),
                   toggle(QStringLiteral("typeAlignRight"), QString(), TypeAlignment::name(TextAlignment::right)),
                   toggle(QStringLiteral("typeAlignJustify"), QString(), TypeAlignment::name(TextAlignment::justify))}
{
    setObjectName(QStringLiteral("typeControls"));
    m_family->setObjectName(QStringLiteral("typeFamily"));
    m_family->setFixedWidth(200);
    m_family->setToolTip(QStringLiteral("Font family"));
    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        const QString family = font.family();
        if (family == shownText().family)
            return;
        // A new family keeps the nearest face to the current one.
        change([&family](TextContent &text) {
            const int weight = text.isBold() ? 700 : 400;
            const bool italic = text.isItalic();
            text.family = family;
            text.style = TextContent::styleFor(family, weight, italic);
        }, QStringLiteral("Font"));
    });
    m_style->setObjectName(QStringLiteral("typeStyle"));
    m_style->setToolTip(QStringLiteral("Font style"));
    m_style->setAccessibleName(QStringLiteral("Font style"));
    m_style->setMinimumWidth(110);
    connect(m_style, &QComboBox::activated, this, [this](int index) {
        const QString style = m_style->itemText(index);
        change([&style](TextContent &text) { text.style = style; }, QStringLiteral("Font Style"));
    });
    m_size->setObjectName(QStringLiteral("typeSize"));
    m_size->field->setObjectName(QStringLiteral("typeSizeField"));
    m_size->field->setFixedWidth(52);
    for (size_t index = 0; index < alignments.size(); ++index) {
        const TextAlignment alignment = alignments.at(index);
        connect(m_alignments.at(index), &QToolButton::clicked, this, [this, alignment] {
            change([alignment](TextContent &text) { text.alignment = alignment; }, QStringLiteral("Alignment"));
        });
    }
    int at = 1;
    for (QWidget *widget : std::initializer_list<QWidget *>{m_family, m_style, m_size, m_alignments[0], m_alignments[1], m_alignments[2], m_alignments[3]})
        row->insertWidget(at++, widget);
    applyGlyphs();
    connect(&m_session, &EditorSession::changed, this, &TypeControls::synchronize);
    synchronize();
}

QToolButton *TypeControls::toggle(const QString &name, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(28, 26);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    return button;
}

TextContent TypeControls::shownText() const
{
    return m_session.shownText();
}

void TypeControls::change(const std::function<void(TextContent &)> &edit, const QString &name)
{
    m_session.updateText(edit, name);
}

void TypeControls::applyGlyphs()
{
    for (size_t index = 0; index < alignments.size(); ++index)
        m_alignments.at(index)->setIcon(TypeAlignment::glyph(alignments.at(index), palette().color(QPalette::WindowText), devicePixelRatio()));
}

// The theme's ink reaches the glyphs.
void TypeControls::changeEvent(QEvent *event)
{
    ToolHeaderBar::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyGlyphs();
}

void TypeControls::synchronize()
{
    const TextContent text = shownText();
    {
        const QSignalBlocker quiet(m_family);
        m_family->setCurrentFont(QFont(text.family));
    }
    {
        const QSignalBlocker quiet(m_style);
        QStringList styles = QFontDatabase::styles(text.family);
        if (!styles.contains(text.style))
            styles.prepend(text.style);
        if (styles != [this] {
                QStringList shown;
                for (int index = 0; index < m_style->count(); ++index)
                    shown << m_style->itemText(index);
                return shown;
            }()) {
            m_style->clear();
            m_style->addItems(styles);
        }
        m_style->setCurrentText(text.style);
    }
    m_size->sync(text.size);
    for (size_t index = 0; index < alignments.size(); ++index)
        m_alignments.at(index)->setChecked(text.alignment == alignments.at(index)
                                           || (alignments.at(index) == TextAlignment::justify && text.alignment == TextAlignment::justifyAll));
}
