#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/KeyboardShortcuts.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
constexpr double swatchSize = 24;
constexpr double swatchOffset = 12;
// The buttons reach 3 points past the swatches' frame.
constexpr QPoint origin(3, 3);

const VectorObject *styleSource(const EditorSession &session)
{
    if (!session.document())
        return nullptr;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (object && object->hasPaint())
            return object;
    }
    return nullptr;
}

// A 12-point glyph in the secondary ink.
class GlyphButton : public QAbstractButton {
public:
    GlyphButton(std::function<void(QPainter &)> glyph, QWidget *parent) : QAbstractButton(parent), m_glyph(std::move(glyph))
    {
        setFixedSize(12, 12);
        setCursor(Qt::ArrowCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::PlaceholderText);
        painter.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        m_glyph(painter);
    }

private:
    const std::function<void(QPainter &)> m_glyph;
};

// A bent arrow with heads at both ends, as Illustrator's.
void swapGlyph(QPainter &painter)
{
    QPainterPath arrow;
    arrow.moveTo(2, 3.5);
    arrow.quadTo(8.5, 3.5, 8.5, 10);
    arrow.moveTo(4, 1.5);
    arrow.lineTo(2, 3.5);
    arrow.lineTo(4, 5.5);
    arrow.moveTo(6.5, 8);
    arrow.lineTo(8.5, 10);
    arrow.lineTo(10.5, 8);
    painter.drawPath(arrow);
}

// Small black stroke over white fill: the defaults.
void resetGlyph(QPainter &painter)
{
    const QColor ink = painter.pen().color();
    painter.fillRect(QRectF(4.5, 4.5, 6, 6), ink);
    painter.drawRect(QRectF(1.5, 1.5, 6, 6));
    painter.fillRect(QRectF(2, 2, 5, 5), Qt::white);
}
}

Paint ShownStyle::fill(const EditorSession &session)
{
    const VectorObject *source = styleSource(session);
    return source ? source->fill : session.defaultFill();
}

StrokeStyle ShownStyle::stroke(const EditorSession &session)
{
    const VectorObject *source = styleSource(session);
    return source ? source->stroke : session.defaultStroke();
}

namespace {
// Whether any two painted leaves of the selection differ by `key`.
template <typename Key> bool differs(const EditorSession &session, Key key)
{
    if (!session.document())
        return false;
    const VectorObject *first = nullptr;
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = session.document()->find(id);
        if (!object || !object->hasPaint())
            continue;
        if (!first)
            first = object;
        else if (!key(*first, *object))
            return true;
    }
    return false;
}
}

bool ShownStyle::fillMixed(const EditorSession &session)
{
    return differs(session, [](const VectorObject &a, const VectorObject &b) { return a.fill == b.fill; });
}

bool ShownStyle::strokeMixed(const EditorSession &session)
{
    return differs(session, [](const VectorObject &a, const VectorObject &b) { return a.stroke.paint == b.stroke.paint; });
}

bool ShownStyle::strokeWidthMixed(const EditorSession &session)
{
    return differs(session, [](const VectorObject &a, const VectorObject &b) { return std::abs(a.stroke.width - b.stroke.width) < 1e-9; });
}

PaintSwatch::PaintSwatch(std::function<Paint()> paint, bool stroke, QWidget *parent)
    : QAbstractButton(parent), m_paint(std::move(paint)), m_stroke(stroke)
{
}

void PaintSwatch::draw(QPainter &painter, const QRectF &rect, const Paint &paint, bool stroke, bool mixed)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(rect, 3, 3);
    if (stroke) {
        // A ring: the middle shows what lies behind.
        QPainterPath hole;
        const double ring = std::max(4.0, rect.width() / 4);
        hole.addRect(rect.adjusted(ring, ring, -ring, -ring));
        shape = shape.subtracted(hole);
    }
    if (mixed) {
        // Hatching, as Illustrator and Figma show a mixed paint.
        painter.fillPath(shape, QColor(236, 236, 236));
        painter.save();
        painter.setClipPath(shape);
        painter.setPen(QPen(QColor(120, 120, 120), 1));
        for (double x = rect.left() - rect.height(); x < rect.right(); x += 4)
            painter.drawLine(QPointF(x, rect.bottom()), QPointF(x + rect.height(), rect.top()));
        painter.restore();
    } else {
        painter.fillPath(shape, paint.isVisible() ? paint.brush(rect) : QBrush(Qt::white));
    }
    if (!mixed && !paint.isVisible()) {
        painter.setClipPath(shape);
        painter.setPen(QPen(QColor(220, 30, 30), 2));
        painter.drawLine(rect.bottomLeft(), rect.topRight());
        painter.setClipping(false);
    }
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
    painter.restore();
}

void PaintSwatch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    draw(painter, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_paint(), m_stroke, m_mixed);
}

void PaintSwatch::setMixed(bool mixed)
{
    if (mixed == m_mixed)
        return;
    m_mixed = mixed;
    update();
}

ColorPaletteControls::ColorPaletteControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session),
      m_stroke(new PaintSwatch([&session] { return ShownStyle::stroke(session).paint; }, true, this)),
      m_fill(new PaintSwatch([&session] { return ShownStyle::fill(session); }, false, this)),
      m_swap(new GlyphButton(swapGlyph, this)), m_reset(new GlyphButton(resetGlyph, this))
{
    setObjectName(QStringLiteral("colorPalette"));
    setFixedSize(int(swatchSize + swatchOffset) + 2 * origin.x(), int(swatchSize + swatchOffset) + 2 * origin.y());
    // The fill sits in front, the stroke behind, as Illustrator's.
    m_stroke->setObjectName(QStringLiteral("strokeSwatch"));
    m_stroke->setGeometry(QRect(origin + QPoint(int(swatchOffset), int(swatchOffset)), QSize(int(swatchSize), int(swatchSize))));
    m_stroke->setToolTip(QStringLiteral("Stroke color"));
    m_stroke->setAccessibleName(QStringLiteral("Stroke color"));
    m_fill->setObjectName(QStringLiteral("fillSwatch"));
    m_fill->setGeometry(QRect(origin, QSize(int(swatchSize), int(swatchSize))));
    m_fill->setToolTip(QStringLiteral("Fill color"));
    m_fill->setAccessibleName(QStringLiteral("Fill color"));
    m_fill->raise();
    m_swap->setObjectName(QStringLiteral("swapFillStroke"));
    m_swap->move(origin + QPoint(int(swatchSize) + 2, -3));
    m_swap->setToolTip(ShortcutSettings::shared().tip(QStringLiteral("Swap fill and stroke"), QStringLiteral("Swap fill and stroke")));
    m_swap->setAccessibleName(QStringLiteral("Swap fill and stroke"));
    m_reset->setObjectName(QStringLiteral("defaultFillStroke"));
    m_reset->move(origin + QPoint(-1, int(swatchSize) + 3));
    m_reset->setToolTip(ShortcutSettings::shared().tip(QStringLiteral("Default fill and stroke"), QStringLiteral("Default fill and stroke")));
    m_reset->setAccessibleName(QStringLiteral("Default fill and stroke"));
    connect(m_fill, &QAbstractButton::clicked, this, &ColorPaletteControls::pickFill);
    connect(m_stroke, &QAbstractButton::clicked, this, &ColorPaletteControls::pickStroke);
    connect(m_swap, &QAbstractButton::clicked, this, [this] { m_session.swapFillAndStroke(); });
    connect(m_reset, &QAbstractButton::clicked, this, [this] { m_session.resetDefaultColors(); });
    connect(&m_session, &EditorSession::changed, this, &ColorPaletteControls::synchronize);
    synchronize();
}

void ColorPaletteControls::pickFill()
{
    const Paint shown = ShownStyle::fill(m_session);
    ColorPickerSheet::showIn(m_picker, QStringLiteral("Fill Color"), shown.isVisible() ? shown.swatch() : QColor(Qt::white),
                           [this](const QColor &color) { m_session.setFillOfSelection(Paint::solid(color)); });
}

void ColorPaletteControls::pickStroke()
{
    const StrokeStyle shown = ShownStyle::stroke(m_session);
    ColorPickerSheet::showIn(m_picker, QStringLiteral("Stroke Color"), shown.paint.isVisible() ? shown.paint.swatch() : QColor(Qt::black),
                           [this](const QColor &color) {
                               StrokeStyle stroke = ShownStyle::stroke(m_session);
                               stroke.paint = Paint::solid(color);
                               m_session.setStrokeOfSelection(stroke);
                           });
}

void ColorPaletteControls::synchronize()
{
    m_fill->setMixed(ShownStyle::fillMixed(m_session));
    m_stroke->setMixed(ShownStyle::strokeMixed(m_session));
    m_fill->update();
    m_stroke->update();
}
