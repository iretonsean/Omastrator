#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/LayerAppearanceControls.h"
#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include "UI/ToolHeaderStyle.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setFixedWidth(46);
    return label;
}

const std::array<PaintKind, 4> kinds{PaintKind::none, PaintKind::solid, PaintKind::linearGradient, PaintKind::radialGradient};
}

PaintRow::PaintRow(EditorSession &session, bool stroke, FloatingPanel &picker, QWidget *parent)
    : QWidget(parent), m_session(session), m_stroke(stroke), m_picker(picker), m_kind(new QComboBox(this)),
      m_well(new PaintSwatch([this] { return shown(); }, stroke, this)),
      m_end(new PaintSwatch([this] {
          const Paint paint = shown();
          return Paint::solid(paint.stops.empty() ? paint.color : paint.stops.back().color);
      }, false, this))
{
    const QString prefix = stroke ? QStringLiteral("stroke") : QStringLiteral("fill");
    setObjectName(prefix + QStringLiteral("Row"));
    m_kind->setObjectName(prefix + QStringLiteral("Kind"));
    m_kind->setAccessibleName(stroke ? QStringLiteral("Stroke kind") : QStringLiteral("Fill kind"));
    m_kind->addItems({QStringLiteral("None"), QStringLiteral("Solid"), QStringLiteral("Linear"), QStringLiteral("Radial")});
    m_well->setObjectName(prefix + QStringLiteral("Well"));
    m_well->setFixedSize(36, 22);
    m_well->setToolTip(stroke ? QStringLiteral("Stroke color") : QStringLiteral("Fill color"));
    m_end->setObjectName(prefix + QStringLiteral("End"));
    m_end->setFixedSize(22, 22);
    m_end->setToolTip(QStringLiteral("Gradient end color"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    row->addWidget(caption(stroke ? QStringLiteral("Stroke") : QStringLiteral("Fill"), this));
    row->addWidget(m_well);
    row->addWidget(m_end);
    row->addWidget(m_kind, 1);
    connect(m_kind, &QComboBox::activated, this, [this](int index) { apply(converted(shown(), kinds.at(size_t(index)))); });
    connect(m_well, &QAbstractButton::clicked, this, [this] { pickStop(false); });
    connect(m_end, &QAbstractButton::clicked, this, [this] { pickStop(true); });
}

Paint PaintRow::shown() const
{
    return m_stroke ? ShownStyle::stroke(m_session).paint : ShownStyle::fill(m_session);
}

Paint PaintRow::converted(const Paint &paint, PaintKind kind)
{
    if (kind == paint.kind)
        return paint;
    // Colours carry over between solids and gradients.
    const QColor first = paint.kind == PaintKind::none ? QColor(Qt::black) : paint.swatch();
    const QColor last = paint.stops.size() >= 2 ? paint.stops.back().color : QColor(Qt::white);
    switch (kind) {
    case PaintKind::none: return Paint::none();
    case PaintKind::solid: return Paint::solid(first);
    case PaintKind::linearGradient: return Paint::linear(first, last);
    case PaintKind::radialGradient: return Paint::radial(first, last);
    }
    return paint;
}

void PaintRow::apply(const Paint &paint)
{
    if (!m_stroke) {
        m_session.setFillOfSelection(paint);
        return;
    }
    StrokeStyle stroke = ShownStyle::stroke(m_session);
    stroke.paint = paint;
    m_session.setStrokeOfSelection(stroke);
}

// The well picks a solid or a gradient's first stop.
void PaintRow::pickStop(bool last)
{
    const Paint paint = shown();
    const bool gradient = paint.stops.size() >= 2;
    const QColor start = last && gradient ? paint.stops.back().color : paint.isVisible() ? paint.swatch() : QColor(Qt::black);
    const QString title = (m_stroke ? QStringLiteral("Stroke") : QStringLiteral("Fill")) + (gradient ? QStringLiteral(" Gradient") : QStringLiteral(" Color"));
    ColorPickerSheet::showIn(m_picker, title, start, [this, last](const QColor &color) {
        Paint next = shown();
        if (next.stops.size() >= 2)
            (last ? next.stops.back() : next.stops.front()).color = color;
        else
            next = Paint::solid(color);
        apply(next);
    });
}

void PaintRow::synchronize()
{
    const Paint paint = shown();
    const bool mixed = m_stroke ? ShownStyle::strokeMixed(m_session) : ShownStyle::fillMixed(m_session);
    m_well->setMixed(mixed);
    m_kind->setPlaceholderText(QStringLiteral("Mixed"));
    m_kind->setCurrentIndex(mixed ? -1 : int(std::find(kinds.begin(), kinds.end(), paint.kind) - kinds.begin()));
    m_well->setToolTip((m_stroke ? QStringLiteral("Stroke color") : QStringLiteral("Fill color")) + (mixed ? QStringLiteral(": mixed") : QString()));
    m_end->setVisible(!mixed && paint.stops.size() >= 2);
    m_well->update();
    m_end->update();
}

PanelSection *PropertiesPanel::appearanceSection()
{
    m_appearance = new PanelSection(QStringLiteral("Appearance"), QStringLiteral("appearance"), this);
    PanelSection *block = m_appearance;
    QVBoxLayout *body = block->body;
    auto *swap = new QToolButton(block);
    swap->setObjectName(QStringLiteral("swapFillStrokeButton"));
    swap->setText(QStringLiteral("⇄"));
    swap->setAutoRaise(true);
    swap->setToolTip(QStringLiteral("Swap fill and stroke (X)"));
    swap->setAccessibleName(QStringLiteral("Swap fill and stroke"));
    swap->setFixedSize(24, 22);
    connect(swap, &QToolButton::clicked, this, [this] { m_session.swapFillAndStroke(); });
    auto *reset = new QToolButton(block);
    reset->setObjectName(QStringLiteral("defaultFillStrokeButton"));
    reset->setText(QStringLiteral("Default"));
    reset->setAutoRaise(true);
    reset->setToolTip(QStringLiteral("Default fill and stroke (D)"));
    reset->setFixedHeight(22);
    for (QToolButton *button : {swap, reset})
        button->setFont(ToolHeaderStyle::controlFont());
    connect(reset, &QToolButton::clicked, this, [this] { m_session.resetDefaultColors(); });
    block->trailing->addWidget(swap);
    block->trailing->addWidget(reset);
    m_fill = new PaintRow(m_session, false, m_picker, block);
    m_strokePaint = new PaintRow(m_session, true, m_picker, block);
    body->addWidget(m_fill);
    body->addWidget(m_strokePaint);
    body->addWidget(new LayerAppearanceControls(m_session, block));
    return block;
}

PanelSection *PropertiesPanel::strokeSection()
{
    m_stroke = new PanelSection(QStringLiteral("Stroke"), QStringLiteral("stroke"), this);
    PanelSection *block = m_stroke;
    QVBoxLayout *body = block->body;
    m_strokeWidth = new NumberField(QString(), QStringLiteral("pt"), [this](double width) { setStrokeWidth(width); }, block);
    m_strokeWidth->field->setObjectName(QStringLiteral("strokeWidth"));
    m_strokeWidth->step = 0.5;
    m_strokeWidth->minimum = 0;
    m_strokeWidth->setToolTip(QStringLiteral("Stroke weight"));
    m_strokeWidth->changeEach = [this](const std::function<double(double)> &change) {
        m_session.beginEdit(QStringLiteral("Stroke"));
        for (const QUuid &id : m_session.selectedLeaves()) {
            const VectorObject *object = m_session.document()->find(id);
            if (!object || !object->hasPaint() || m_session.document()->isEffectivelyLocked(id))
                continue;
            VectorObject changed = *object;
            changed.stroke.width = std::max(0.0, change(object->stroke.width));
            m_session.updateObject(changed, QStringLiteral("Stroke"));
        }
        m_session.endEdit();
    };
    m_strokeWidth->gesture = [this](bool starting) {
        if (starting)
            m_session.beginEdit(QStringLiteral("Stroke"));
        else
            m_session.endEdit();
    };
    m_cap = new QComboBox(block);
    m_cap->setObjectName(QStringLiteral("strokeCap"));
    m_cap->addItems({QStringLiteral("Butt"), QStringLiteral("Round"), QStringLiteral("Projecting")});
    m_join = new QComboBox(block);
    m_join->setObjectName(QStringLiteral("strokeJoin"));
    m_join->addItems({QStringLiteral("Miter"), QStringLiteral("Round"), QStringLiteral("Bevel")});
    m_dashes = new QLineEdit(block);
    m_dashes->setObjectName(QStringLiteral("strokeDashes"));
    m_dashes->setPlaceholderText(QStringLiteral("Solid"));
    m_dashes->setToolTip(QStringLiteral("Dash and gap lengths in points, such as 12 6"));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    grid->addWidget(caption(QStringLiteral("Weight"), block), 0, 0);
    grid->addWidget(m_strokeWidth, 0, 1);
    grid->addWidget(caption(QStringLiteral("Cap"), block), 1, 0);
    grid->addWidget(m_cap, 1, 1);
    grid->addWidget(caption(QStringLiteral("Corner"), block), 2, 0);
    grid->addWidget(m_join, 2, 1);
    grid->addWidget(caption(QStringLiteral("Dashes"), block), 3, 0);
    grid->addWidget(m_dashes, 3, 1);
    grid->setColumnStretch(1, 1);
    body->addLayout(grid);
    connect(m_cap, &QComboBox::activated, this, [this](int index) {
        StrokeStyle stroke = ShownStyle::stroke(m_session);
        stroke.cap = std::array{Qt::FlatCap, Qt::RoundCap, Qt::SquareCap}.at(size_t(index));
        m_session.setStrokeOfSelection(stroke);
    });
    connect(m_join, &QComboBox::activated, this, [this](int index) {
        StrokeStyle stroke = ShownStyle::stroke(m_session);
        stroke.join = std::array{Qt::MiterJoin, Qt::RoundJoin, Qt::BevelJoin}.at(size_t(index));
        m_session.setStrokeOfSelection(stroke);
    });
    connect(m_dashes, &QLineEdit::editingFinished, this, &PropertiesPanel::applyDashes);
    return block;
}

void PropertiesPanel::setStrokeWidth(double width)
{
    StrokeStyle stroke = ShownStyle::stroke(m_session);
    stroke.width = std::max(0.0, width);
    m_session.setStrokeOfSelection(stroke);
}

// Numbers apart by spaces or commas; anything else is refused.
void PropertiesPanel::applyDashes()
{
    std::vector<double> dashes;
    for (const QString &part : m_dashes->text().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        bool number = false;
        const double length = part.toDouble(&number);
        if (!number || !(length >= 0)) {
            synchronize();
            return;
        }
        dashes.push_back(length);
    }
    StrokeStyle stroke = ShownStyle::stroke(m_session);
    if (stroke.dashes == dashes)
        return;
    stroke.dashes = dashes;
    m_session.setStrokeOfSelection(stroke);
}
