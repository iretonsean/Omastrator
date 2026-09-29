#include "Canvas/EditorCanvasState.h"
#include "Document/BrowserInput.h"
#include <QPainter>

// Edit Page (docs/LIVE-IN-FRAME.md, section 3): a Browser View's page takes the pointer as element picks, while the keys
// stay the canvas's. The pointer reaches the page through Browse's mapping; Live's overlay there hovers and selects.

bool EditorCanvas::enterEditPage(const QUuid &frame)
{
    return m_state->enterEditPage(frame);
}

void EditorCanvas::leaveEditPage()
{
    m_state->leaveEditPage();
}

std::optional<QUuid> EditorCanvas::editPageFrame() const
{
    return m_state->editPage;
}

bool EditorCanvas::State::enterEditPage(const QUuid &frame)
{
    if (editPage == frame)
        return true;
    if (!browserHost || !session.hasDocument())
        return false;
    const VectorObject *object = session.document()->find(frame);
    if (!object || !object->browser || !session.document()->isOnCurrentPage(frame))
        return false;
    finishText();
    // The Selection tool is the mode's tool; any other leaves it.
    if (session.tool() != Tool::select)
        session.selectTool(Tool::select);
    if (editPage)
        leaveEditPage();
    const QString failure = browserHost->beginEditPage(frame);
    if (!failure.isEmpty()) {
        emit canvas.notice(failure);
        return false;
    }
    // The frame's own handles would sit over the page.
    session.deselectAll();
    editPage = frame;
    canvas.update();
    emit canvas.editPageChanged();
    return true;
}

void EditorCanvas::State::leaveEditPage()
{
    if (!editPage)
        return;
    const QUuid frame = *editPage;
    editPage.reset();
    // The page's button comes up and it forgets the pointer, as when Browse ends.
    if (drag && drag->kind == DragKind::browse) {
        browseLeave();
    } else if (browserHost) {
        browserHost->dispatch(frame, QStringLiteral("Input.dispatchMouseEvent"),
                              BrowserInput::mouseParams(QStringLiteral("mouseMoved"), QPointF(-1, -1), Qt::NoButton, Qt::NoButton, 0, {}));
    }
    browseHover.reset();
    if (browserHost)
        browserHost->endEditPage(frame);
    canvas.update();
    emit canvas.editPageChanged();
}

void EditorCanvas::State::checkEditPage()
{
    if (!editPage)
        return;
    const VectorObject *object = session.hasDocument() ? session.document()->find(*editPage) : nullptr;
    if (!object || !object->browser || !session.document()->isOnCurrentPage(*editPage) || session.tool() != Tool::select)
        leaveEditPage();
}

bool EditorCanvas::State::editPagePress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!editPage || !browserHost)
        return false;
    if (!browseBox(*editPage).contains(toDocument(view))) {
        leaveEditPage();
        return false;
    }
    browseHover = editPage;
    browseClicks = 1;
    const QPointF css = BrowserInput::cssPoint(toDocument(view), browseBox(*editPage));
    // The move first, so the page's overlay has the element under the pointer when the button goes down.
    browserHost->dispatch(*editPage, QStringLiteral("Input.dispatchMouseEvent"),
                          BrowserInput::mouseParams(QStringLiteral("mouseMoved"), css, Qt::NoButton, Qt::NoButton, 0, modifiers));
    if (browserHost->dispatch(*editPage, QStringLiteral("Input.dispatchMouseEvent"),
                              BrowserInput::mouseParams(QStringLiteral("mousePressed"), css, Qt::LeftButton, Qt::LeftButton, 1, modifiers))) {
        beginDrag(DragKind::browse, view);
        drag->object = *editPage;
        drag->started = true;
    }
    return true;
}

std::optional<QUuid> EditorCanvas::State::editPageTargetAt(QPointF view) const
{
    if (!browserHost || !session.hasDocument())
        return std::nullopt;
    const std::optional<QUuid> frame = browseFrameAt(view);
    if (!frame)
        return std::nullopt;
    // A design object over the page (its child, or anything above it) is the design's to double-click.
    const std::optional<QUuid> leaf = hitLeaf(toDocument(view));
    if (leaf && *leaf != *frame)
        return std::nullopt;
    return frame;
}

void EditorCanvas::State::dimEditPageChildren(std::optional<VectorDocument> &shown) const
{
    if (!editPage || !session.hasDocument() || !session.document()->find(*editPage))
        return;
    if (!shown)
        shown = *session.document();
    for (const QUuid &child : shown->children(*editPage)) {
        if (VectorObject *object = shown->find(child))
            object->opacity *= 0.25;
    }
}

void EditorCanvas::State::drawEditPage(QPainter &painter) const
{
    if (!editPage || !browserHost)
        return;
    const QRectF box = browseBox(*editPage);
    if (box.isEmpty())
        return;
    const QTransform toView = documentToView();
    const QRectF frame = toView.mapRect(box);
    const BrowserViewHost::EditBoxes boxes = browserHost->editBoxes(*editPage);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(frame.adjusted(-2, -2, 2, 2));
    const QColor color = accent();
    // The mode's own edge, so a page that takes clicks as picks doesn't look like Browse.
    QColor edge = color;
    edge.setAlphaF(0.55);
    painter.setPen(QPen(edge, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame.adjusted(0.5, 0.5, -0.5, -0.5));
    const auto inView = [&](const QRectF &css) { return toView.mapRect(css.translated(box.topLeft())); };
    if (boxes.hover) {
        painter.setPen(QPen(color, 1));
        painter.drawRect(inView(boxes.hover->rect));
    }
    QFont font = canvas.font();
    font.setPixelSize(10);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    for (const BrowserViewHost::EditBox &each : boxes.selection) {
        const QRectF rect = inView(each.rect);
        painter.setPen(QPen(color, 2));
        painter.drawRect(rect);
        // The label sits under the box, or inside its bottom edge where the frame ends.
        const QString label = each.label;
        QRectF pill(0, 0, metrics.horizontalAdvance(label) + 10, metrics.height() + 4);
        pill.moveTopLeft(rect.bottomLeft() + QPointF(0, 3));
        if (pill.bottom() > frame.bottom())
            pill.moveBottom(rect.bottom() - 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(pill, 3, 3);
        painter.setPen(Qt::white);
        painter.drawText(pill, Qt::AlignCenter, label);
    }
    painter.restore();
}
