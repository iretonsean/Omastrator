#include "Canvas/EditorCanvasState.h"
#include <QPainter>
#include <algorithm>

// An empty Browser View's three ways to fill it (docs/MOTION.md, section 4), drawn in the frame under "No page yet." at a
// constant screen size. A frame too small for them keeps the plain line.

namespace {
constexpr double buttonHeight = 30;
constexpr double gap = 8;
constexpr double titleHeight = 22;
constexpr double minWidth = 300;
constexpr double minHeight = 130;
}

std::vector<EditorCanvas::State::EmptyLayout> EditorCanvas::State::emptyFrames() const
{
    std::vector<EmptyLayout> layouts;
    if (!browserHost || !session.hasDocument())
        return layouts;
    const VectorDocument &document = *session.document();
    QFont font = canvas.font();
    font.setPixelSize(12);
    const QFontMetricsF metrics(font);
    for (const VectorObject &object : document.objects) {
        if (!object.browser || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        const BrowserViewHost::Empty offer = browserHost->empty(object.id);
        if (!offer.offered)
            continue;
        const QRectF box = documentToView().mapRect(document.bounds(object.id));
        if (box.width() < minWidth || box.height() < minHeight)
            continue;
        EmptyLayout layout;
        layout.frame = object.id;
        layout.buildLine = offer.buildLine;
        std::vector<QRectF *> buttons{&layout.type, &layout.generate};
        std::vector<QString> labels{QStringLiteral("Type an address"), QStringLiteral("Generate a page…")};
        if (offer.build) {
            buttons.push_back(&layout.build);
            labels.push_back(QStringLiteral("Build It from a canvas frame"));
        }
        std::vector<double> widths;
        double row = gap * double(buttons.size() - 1);
        for (const QString &label : labels) {
            widths.push_back(metrics.horizontalAdvance(label) + 28);
            row += widths.back();
        }
        const bool stacked = row > box.width() - 32;
        const double stackedHeight = double(buttons.size()) * buttonHeight + gap * double(buttons.size() - 1);
        const double block = titleHeight + 12 + (stacked ? stackedHeight : buttonHeight) + (offer.buildLine.isEmpty() ? 0 : 10 + 16);
        if (block > box.height() - 16)
            continue;
        const double top = box.center().y() - block / 2;
        layout.title = QRectF(box.left(), top, box.width(), titleHeight);
        double y = top + titleHeight + 12;
        double x = box.center().x() - row / 2;
        for (size_t i = 0; i < buttons.size(); ++i) {
            if (stacked) {
                *buttons[i] = QRectF(box.center().x() - widths[i] / 2, y, widths[i], buttonHeight);
                y += buttonHeight + gap;
            } else {
                *buttons[i] = QRectF(x, y, widths[i], buttonHeight);
                x += widths[i] + gap;
            }
        }
        if (stacked)
            y -= gap;
        else
            y += buttonHeight;
        if (!offer.buildLine.isEmpty()) {
            const double words = std::min(box.width() - 24, metrics.horizontalAdvance(offer.buildLine) + 4);
            layout.hint = QRectF(box.center().x() - words / 2, y + 10, words, 16);
        }
        layouts.push_back(layout);
    }
    return layouts;
}

void EditorCanvas::State::drawEmptyFrames(QPainter &painter) const
{
    const std::vector<EmptyLayout> layouts = emptyFrames();
    if (layouts.empty())
        return;
    QFont font = canvas.font();
    font.setPixelSize(12);
    QFont title = font;
    title.setPixelSize(14);
    const QPalette &palette = canvas.palette();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const EmptyLayout &layout : layouts) {
        // A panel in the app's own colours, so the offer reads whatever the frame is showing.
        QRectF content;
        for (const QRectF &part : {layout.type, layout.generate, layout.build, layout.hint})
            if (!part.isNull())
                content = content.isNull() ? part : content.united(part);
        const double titleWidth = QFontMetricsF(title).horizontalAdvance(QStringLiteral("No page yet."));
        const QRectF words(layout.title.center().x() - titleWidth / 2, layout.title.top(), titleWidth, layout.title.height());
        const QRectF card = content.united(words).adjusted(-16, -12, 16, 14);
        painter.setPen(QPen(palette.color(QPalette::Mid), 1));
        painter.setBrush(palette.color(QPalette::Window));
        painter.drawRoundedRect(card, 10, 10);
        painter.setFont(title);
        painter.setPen(palette.color(QPalette::WindowText));
        painter.drawText(layout.title, Qt::AlignCenter, QStringLiteral("No page yet."));
        painter.setFont(font);
        const auto button = [&](const QRectF &rect, const QString &label, bool primary) {
            if (rect.isNull())
                return;
            const bool hovered = hover && rect.contains(*hover);
            painter.setPen(primary ? Qt::NoPen : QPen(palette.color(QPalette::Mid), 1));
            painter.setBrush(primary ? accent().lighter(hovered ? 115 : 100) : hovered ? palette.color(QPalette::Midlight) : Qt::NoBrush);
            painter.drawRoundedRect(rect, 8, 8);
            painter.setPen(primary ? palette.color(QPalette::HighlightedText) : palette.color(QPalette::WindowText));
            painter.drawText(rect, Qt::AlignCenter, label);
        };
        button(layout.type, QStringLiteral("Type an address"), false);
        button(layout.generate, QStringLiteral("Generate a page…"), true);
        button(layout.build, QStringLiteral("Build It from a canvas frame"), false);
        if (!layout.hint.isNull()) {
            painter.setPen(palette.color(QPalette::PlaceholderText));
            painter.drawText(layout.hint, Qt::AlignCenter, QFontMetricsF(font).elidedText(layout.buildLine, Qt::ElideRight, layout.hint.width()));
        }
    }
    painter.restore();
}

bool EditorCanvas::State::emptyFramePress(QPointF view)
{
    if (!browserHost)
        return false;
    for (const EmptyLayout &layout : emptyFrames()) {
        if (layout.type.contains(view)) {
            session.select({layout.frame});
            openAddressEditor(layout.frame);
        } else if (layout.generate.contains(view)) {
            session.select({layout.frame});
            browserHost->act(layout.frame, BrowserViewHost::Action::generatePage);
        } else if (layout.build.contains(view)) {
            session.select({layout.frame});
            browserHost->act(layout.frame, BrowserViewHost::Action::buildIt);
        } else {
            continue;
        }
        canvas.update();
        return true;
    }
    return false;
}

QString EditorCanvas::State::emptyFrameTip(QPointF view) const
{
    for (const EmptyLayout &layout : emptyFrames()) {
        if (layout.type.contains(view))
            return QStringLiteral("Type a web address for this frame");
        if (layout.generate.contains(view))
            return QStringLiteral("Your agent writes a page into a new project. You confirm every file first.");
        if (layout.build.contains(view))
            return QStringLiteral("Your agent builds the design on this frame into a new or existing project");
    }
    return {};
}

bool EditorCanvas::State::stopGenerating()
{
    if (!browserHost || !session.hasDocument())
        return false;
    for (const VectorObject &object : session.document()->objects) {
        if (object.browser && browserHost->empty(object.id).generating) {
            browserHost->act(object.id, BrowserViewHost::Action::stopBuild);
            canvas.update();
            return true;
        }
    }
    return false;
}
