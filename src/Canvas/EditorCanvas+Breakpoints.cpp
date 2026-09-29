#include "Canvas/EditorCanvasState.h"
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

// A Browser View's breakpoint buttons (docs/BROWSER-VIEW.md, sections 4, 6 and 7): each holds the frame at a width as a
// preview, which is never a history step.

namespace {
constexpr double buttonGap = 2;
constexpr double buttonHeight = 20;
constexpr double pencilWidth = 24;
// The address keeps at least this much when the buttons crowd it.
constexpr double addressLeast = 120;
constexpr double addressLeastPreviewing = 20;
}

std::optional<int> EditorCanvas::State::previewedWidth(const QUuid &frame) const
{
    if (!session.hasDocument() || !session.isPreviewOnly())
        return std::nullopt;
    const bool holding = held && held->frame == frame;
    const bool dragging = drag && drag->previewFrame == frame;
    if (!holding && !dragging)
        return std::nullopt;
    return int(std::lround(session.document()->bounds(frame).width()));
}

bool EditorCanvas::State::showsWidths(const QUuid &frame) const
{
    if (session.isSelected(frame) || browseFocus == frame || browseHover == frame || editPage == frame)
        return true;
    if (!hover || !session.hasDocument())
        return false;
    const VectorObject *object = session.document()->find(frame);
    if (!object)
        return false;
    const QRectF box = documentToView().mapRect(object->path.painterPath().boundingRect());
    return box.adjusted(0, -36, 0, 0).contains(*hover);
}

double EditorCanvas::State::addWidthButtons(BrowserBarLayout &layout, double right, double left, const QFontMetricsF &metrics) const
{
    if (!showsWidths(layout.frame) || !browserHost)
        return right;
    // The frame's own width is a button too, dotted, so that ending a preview is one click away.
    const int design = int(std::lround(session.designBox(layout.frame).width()));
    QList<int> widths = browserHost->breakpoints(layout.frame);
    if (!widths.contains(design))
        widths.append(design);
    std::sort(widths.begin(), widths.end());
    double total = -buttonGap;
    for (int width : widths)
        total += metrics.horizontalAdvance(QString::number(width)) + 14 + buttonGap;
    // The Edit Page pencil ends the row.
    total += buttonGap + 4 + pencilWidth;
    // A held preview keeps its buttons however narrow it gets, so the way back is always there.
    const bool previewing = held && held->frame == layout.frame && session.isPreviewOnly();
    if (right - left - total < (previewing ? addressLeastPreviewing : addressLeast))
        return right;
    double x = right - total;
    for (int width : widths) {
        const double size = metrics.horizontalAdvance(QString::number(width)) + 14;
        layout.widths.emplace_back(QRectF(x, layout.bar.top() + (layout.bar.height() - buttonHeight) / 2, size, buttonHeight), width);
        x += size + buttonGap;
    }
    layout.designWidth = design;
    const double rowTop = layout.bar.top() + (layout.bar.height() - buttonHeight) / 2;
    layout.editPage = QRectF(right - pencilWidth, rowTop, pencilWidth, buttonHeight);
    return right - total - 4;
}

void EditorCanvas::State::drawWidthButtons(QPainter &painter, const BrowserBarLayout &layout) const
{
    if (layout.widths.empty())
        return;
    const QPalette &palette = canvas.palette();
    const std::optional<int> previewed = previewedWidth(layout.frame);
    for (const auto &[rect, width] : layout.widths) {
        const bool on = previewed && *previewed == width;
        const bool hot = hover && rect.contains(*hover);
        painter.setPen(Qt::NoPen);
        if (on || hot) {
            painter.setBrush(on ? accent() : palette.color(QPalette::Midlight));
            painter.drawRoundedRect(rect, 5, 5);
        }
        painter.setPen(on ? QColor(Qt::white) : palette.color(QPalette::WindowText));
        painter.drawText(rect, Qt::AlignCenter, QString::number(width));
        if (width == layout.designWidth) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(on ? QColor(Qt::white) : accent());
            painter.drawEllipse(QPointF(rect.center().x(), rect.bottom() - 2.5), 1.5, 1.5);
        }
    }
    painter.setBrush(Qt::NoBrush);
    drawEditPageButton(painter, layout);
}

void EditorCanvas::State::drawEditPageButton(QPainter &painter, const BrowserBarLayout &layout) const
{
    if (layout.editPage.isNull())
        return;
    const bool on = editPage == layout.frame;
    const bool hot = hover && layout.editPage.contains(*hover);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    if (on || hot) {
        painter.setBrush(on ? accent() : canvas.palette().color(QPalette::Midlight));
        painter.drawRoundedRect(layout.editPage, 5, 5);
    }
    // A pencil on the diagonal: body, then the tip.
    painter.setPen(QPen(on ? QColor(Qt::white) : canvas.palette().color(QPalette::WindowText), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QPointF c = layout.editPage.center();
    QPolygonF pencil;
    pencil << c + QPointF(3.5, -5.5) << c + QPointF(5.5, -3.5) << c + QPointF(-2.5, 4.5) << c + QPointF(-5.5, 5.5) << c + QPointF(-4.5, 2.5);
    painter.drawPolygon(pencil);
    painter.drawLine(c + QPointF(2, -4), c + QPointF(4, -2));
    painter.restore();
}

void EditorCanvas::State::holdPreview(const QUuid &frame, int width)
{
    endHeldPreview();
    if (!session.hasDocument() || width < 1)
        return;
    if (session.selection() != std::vector<QUuid>{frame})
        session.select({frame});
    session.beginPreview(QStringLiteral("Preview Width"));
    if (!session.isPreviewOnly())
        return;
    const QRectF box = session.designBox(frame);
    session.previewFrameBox(frame, QRectF(box.topLeft(), QSizeF(width, box.height())));
    held = HeldPreview{frame, width};
}

bool EditorCanvas::State::endHeldPreview()
{
    const bool had = held.has_value() && session.isPreviewOnly();
    held.reset();
    if (had)
        session.cancelInteraction();
    return had;
}

void EditorCanvas::State::setDesignWidth(const QUuid &frame, int width)
{
    QRectF box = session.designBox(frame);
    box.setWidth(width);
    endHeldPreview();
    session.setDesignBox(frame, box);
}
