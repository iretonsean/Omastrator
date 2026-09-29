#include "Canvas/EditorCanvasState.h"
#include "Document/BrowserInput.h"
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QStyleHints>
#include <QWheelEvent>

// The Browse tool (docs/BROWSER-VIEW.md, section 5) -------------------------------------

namespace {
constexpr qint64 hoverGapMs = 8;
}

std::optional<QUuid> EditorCanvas::State::browseFrameAt(QPointF view) const
{
    if (!browserHost || !session.hasDocument())
        return std::nullopt;
    const VectorDocument &document = *session.document();
    const QPointF point = toDocument(view);
    for (auto it = document.objects.rbegin(); it != document.objects.rend(); ++it) {
        if (it->browser && document.isOnCurrentPage(it->id) && document.isEffectivelyVisible(it->id)
            && it->path.painterPath().boundingRect().contains(point))
            return it->id;
    }
    return std::nullopt;
}

QRectF EditorCanvas::State::browseBox(const QUuid &frame) const
{
    const VectorObject *object = session.hasDocument() ? session.document()->find(frame) : nullptr;
    return object ? object->path.painterPath().boundingRect() : QRectF();
}

void EditorCanvas::State::setBrowseFocus(const std::optional<QUuid> &frame)
{
    if (browseFocus == frame)
        return;
    browseFocus = frame;
    // Typed text may arrive as an input-method commit, so the canvas takes those while a page has the keys.
    canvas.setAttribute(Qt::WA_InputMethodEnabled, frame.has_value());
}

void EditorCanvas::State::browseSend(const QUuid &frame, const QString &type, QPointF view, Qt::MouseButton button, Qt::MouseButtons buttons, int clicks,
                                     Qt::KeyboardModifiers mods)
{
    if (!browserHost)
        return;
    const QPointF css = BrowserInput::cssPoint(toDocument(view), browseBox(frame));
    browserHost->dispatch(frame, QStringLiteral("Input.dispatchMouseEvent"), BrowserInput::mouseParams(type, css, button, buttons, clicks, mods));
}

void EditorCanvas::State::browsePress(QPointF view, Qt::KeyboardModifiers mods, bool doubleClick)
{
    const std::optional<QUuid> frame = browseFrameAt(view);
    if (!frame) {
        // Empty canvas pans, as Figma's hand does, and the pages lose the keys.
        setBrowseFocus(std::nullopt);
        browseClicks = 0;
        beginDrag(DragKind::pan, view);
        return;
    }
    // A press soon after the last, near it, counts one more: the third click of a triple takes the whole paragraph.
    const QStyleHints *hints = QGuiApplication::styleHints();
    const bool again = browseClickClock.isValid() && browseClickClock.elapsed() <= hints->mouseDoubleClickInterval()
        && QLineF(view, browseClickView).length() <= hints->startDragDistance() && browseClicks > 0;
    browseClicks = doubleClick ? std::max(2, again ? browseClicks + 1 : 2) : (again ? browseClicks + 1 : 1);
    browseClickClock.start();
    browseClickView = view;
    if (!browserHost)
        return;
    browseHover = frame;
    const QPointF css = BrowserInput::cssPoint(toDocument(view), browseBox(*frame));
    // The move first, so the page has the pointer where the button goes down.
    browserHost->dispatch(*frame, QStringLiteral("Input.dispatchMouseEvent"),
                          BrowserInput::mouseParams(QStringLiteral("mouseMoved"), css, Qt::NoButton, Qt::NoButton, 0, mods));
    const bool sent = browserHost->dispatch(*frame, QStringLiteral("Input.dispatchMouseEvent"),
                                            BrowserInput::mouseParams(QStringLiteral("mousePressed"), css, Qt::LeftButton, Qt::LeftButton, browseClicks, mods));
    if (!sent) {
        // A page that can't take a click yet, or that a click just woke, has nothing to hold.
        return;
    }
    setBrowseFocus(frame);
    beginDrag(DragKind::browse, view);
    drag->object = *frame;
    drag->started = true;
}

void EditorCanvas::State::browseMove(QPointF view, Qt::KeyboardModifiers mods, bool held)
{
    if (drag && drag->kind == DragKind::browse) {
        if (held)
            browseSend(drag->object, QStringLiteral("mouseMoved"), view, Qt::LeftButton, Qt::LeftButton, browseClicks, mods);
        return;
    }
    // Hover: the page under the pointer sees it move, and the one it left sees it go.
    const std::optional<QUuid> frame = browseFrameAt(view);
    if (browseHover && browseHover != frame && browserHost) {
        browserHost->dispatch(*browseHover, QStringLiteral("Input.dispatchMouseEvent"),
                              BrowserInput::mouseParams(QStringLiteral("mouseMoved"), QPointF(-1, -1), Qt::NoButton, Qt::NoButton, 0, mods));
    }
    browseHover = frame;
    if (!frame || (browseMoveClock.isValid() && browseMoveClock.elapsed() < hoverGapMs))
        return;
    browseMoveClock.start();
    browseSend(*frame, QStringLiteral("mouseMoved"), view, Qt::NoButton, Qt::NoButton, 0, mods);
}

void EditorCanvas::State::browseRelease(QPointF view, Qt::KeyboardModifiers mods)
{
    if (!drag || drag->kind != DragKind::browse)
        return;
    browseSend(drag->object, QStringLiteral("mouseReleased"), view, Qt::LeftButton, Qt::NoButton, browseClicks, mods);
}

void EditorCanvas::State::browseLeave()
{
    if (drag && drag->kind == DragKind::browse) {
        browseSend(drag->object, QStringLiteral("mouseReleased"), drag->lastView, Qt::LeftButton, Qt::NoButton, browseClicks, modifiers);
        drag.reset();
    }
    if (browseHover && browserHost) {
        browserHost->dispatch(*browseHover, QStringLiteral("Input.dispatchMouseEvent"),
                              BrowserInput::mouseParams(QStringLiteral("mouseMoved"), QPointF(-1, -1), Qt::NoButton, Qt::NoButton, 0, {}));
    }
    browseHover.reset();
    browseClicks = 0;
    setBrowseFocus(std::nullopt);
}

bool EditorCanvas::State::browseWheel(QWheelEvent *event)
{
    if (session.tool() != Tool::browse || !browserHost || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
        return false;
    const std::optional<QUuid> frame = browseFrameAt(event->position());
    if (!frame)
        return false;
    const QPointF css = BrowserInput::cssPoint(toDocument(event->position()), browseBox(*frame));
    browserHost->dispatch(*frame, QStringLiteral("Input.dispatchMouseEvent"),
                          BrowserInput::wheelParams(css, event->angleDelta(), event->pixelDelta(), event->modifiers()));
    return true;
}

bool EditorCanvas::State::browseKey(QKeyEvent *event, bool down)
{
    if (session.tool() != Tool::browse)
        return false;
    // Esc always leaves, and Ctrl+K stays the palette's, whatever the page is doing.
    if (event->key() == Qt::Key_Escape) {
        if (down)
            session.selectTool(Tool::select);
        return down;
    }
    if (!browseFocus || BrowserInput::reserved(event->key(), event->modifiers()) || !browserHost)
        return false;
    // A release that repeats a held key is Qt's echo, not the user's.
    if (!down && event->isAutoRepeat())
        return true;
    const std::optional<QJsonObject> params = BrowserInput::keyParams(down, event->key(), event->text(), event->modifiers(), event->isAutoRepeat());
    if (!params)
        return false;
    browserHost->dispatch(*browseFocus, QStringLiteral("Input.dispatchKeyEvent"), *params);
    return true;
}

bool EditorCanvas::State::browseInput(QInputMethodEvent *event)
{
    if (!browseFocused() || !browserHost)
        return false;
    if (!event->commitString().isEmpty())
        browserHost->dispatch(*browseFocus, QStringLiteral("Input.insertText"), {{QStringLiteral("text"), event->commitString()}});
    return true;
}
