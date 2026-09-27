#include "UI/NumberField.h"
#include "UI/ToolHeaders.h"
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <cmath>

namespace {
const std::array<TextAlignment, 3> alignments{TextAlignment::left, TextAlignment::center, TextAlignment::right};

// Four lines against one side, as text-align glyphs are.
QPixmap alignmentGlyph(TextAlignment alignment, const QColor &ink, double ratio)
{
    QPixmap pixmap(QSize(18, 18) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
    for (int row = 0; row < 4; ++row) {
        const double length = row % 2 == 0 ? 14 : 9, y = 4 + row * 3.4;
        const double left = alignment == TextAlignment::left ? 2 : alignment == TextAlignment::center ? 9 - length / 2 : 16 - length;
        painter.drawLine(QPointF(left, y), QPointF(left + length, y));
    }
    return pixmap;
}

std::vector<const VectorObject *> selectedTexts(const EditorSession &session)
{
    std::vector<const VectorObject *> texts;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (object && object->kind == ObjectKind::text)
            texts.push_back(object);
    }
    return texts;
}
}

TypeControls::TypeControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Type"), parent), m_session(session), m_family(new QFontComboBox(this)),
      m_size(new NumberField(QString(), QStringLiteral("pt"), [this](double size) {
          change([size](TextContent &text) { text.size = std::clamp(size, 1.0, 1296.0); });
      }, this)),
      m_bold(toggle(QStringLiteral("typeBold"), QStringLiteral("B"), QStringLiteral("Bold"))),
      m_italic(toggle(QStringLiteral("typeItalic"), QStringLiteral("I"), QStringLiteral("Italic"))),
      m_alignments{toggle(QStringLiteral("typeAlignLeft"), QString(), QStringLiteral("Align left")),
                   toggle(QStringLiteral("typeAlignCenter"), QString(), QStringLiteral("Align center")),
                   toggle(QStringLiteral("typeAlignRight"), QString(), QStringLiteral("Align right"))}
{
    setObjectName(QStringLiteral("typeControls"));
    m_family->setObjectName(QStringLiteral("typeFamily"));
    m_family->setFixedWidth(200);
    m_family->setToolTip(QStringLiteral("Font family"));
    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        const QString family = font.family();
        if (family != shownText().family)
            change([&family](TextContent &text) { text.family = family; });
    });
    m_size->setObjectName(QStringLiteral("typeSize"));
    m_size->field->setObjectName(QStringLiteral("typeSizeField"));
    m_size->field->setFixedWidth(52);
    QFont bold = m_bold->font();
    bold.setBold(true);
    m_bold->setFont(bold);
    QFont italic = m_italic->font();
    italic.setItalic(true);
    m_italic->setFont(italic);
    connect(m_bold, &QToolButton::clicked, this, [this](bool on) { change([on](TextContent &text) { text.bold = on; }); });
    connect(m_italic, &QToolButton::clicked, this, [this](bool on) { change([on](TextContent &text) { text.italic = on; }); });
    for (size_t index = 0; index < alignments.size(); ++index) {
        const TextAlignment alignment = alignments.at(index);
        connect(m_alignments.at(index), &QToolButton::clicked, this, [this, alignment] {
            change([alignment](TextContent &text) { text.alignment = alignment; });
        });
    }
    int at = 1;
    for (QWidget *widget : std::initializer_list<QWidget *>{m_family, m_size, m_bold, m_italic, m_alignments[0], m_alignments[1], m_alignments[2]})
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
    const std::vector<const VectorObject *> texts = m_session.document() ? selectedTexts(m_session) : std::vector<const VectorObject *>();
    return texts.empty() ? m_session.defaultText : texts.front()->text;
}

void TypeControls::change(const std::function<void(TextContent &)> &edit)
{
    edit(m_session.defaultText);
    const std::vector<const VectorObject *> texts = m_session.document() ? selectedTexts(m_session) : std::vector<const VectorObject *>();
    if (texts.empty()) {
        synchronize();
        return;
    }
    // Copied first: each update replaces the document.
    std::vector<VectorObject> updated;
    for (const VectorObject *text : texts) {
        updated.push_back(*text);
        edit(updated.back().text);
    }
    m_session.beginEdit(QStringLiteral("Character"));
    for (const VectorObject &object : updated)
        m_session.updateObject(object, QStringLiteral("Character"));
    m_session.endEdit();
}

void TypeControls::applyGlyphs()
{
    for (size_t index = 0; index < alignments.size(); ++index)
        m_alignments.at(index)->setIcon(alignmentGlyph(alignments.at(index), palette().color(QPalette::WindowText), devicePixelRatio()));
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
    m_size->sync(text.size);
    m_bold->setChecked(text.bold);
    m_italic->setChecked(text.italic);
    for (size_t index = 0; index < alignments.size(); ++index)
        m_alignments.at(index)->setChecked(text.alignment == alignments.at(index));
}
