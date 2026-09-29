#include "Canvas/EditorCanvas.h"
#include "Canvas/EditorCanvasState.h"
#include "Canvas/Rulers.h"
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QStyleHints>
#include <cmath>

EditorCanvas::State::State(EditorCanvas &canvas, EditorSession &session) : canvas(canvas), session(session), shownTool(session.tool())
{
}

EditorCanvas::EditorCanvas(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_state(std::make_unique<State>(*this, session))
{
    setObjectName(QStringLiteral("editorCanvas"));
    setAccessibleName(QStringLiteral("Canvas"));
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    // Hover highlights and the pen's rubber band follow the pointer.
    setMouseTracking(true);
    setMinimumSize(64, 64);
    m_state->caretBlink.setInterval(530);
    connect(&m_state->caretBlink, &QTimer::timeout, this, [this] {
        m_state->caretShown = !m_state->caretShown;
        update();
    });
    m_state->opacityCommit.setSingleShot(true);
    connect(&m_state->opacityCommit, &QTimer::timeout, this, [this] { m_state->finishOpacity(); });
    connect(&m_session, &EditorSession::documentChanged, this, [this] {
        m_state->documentChanged();
        update();
    });
    // Typing ends on the page it began on, before the switch or the undo step that follows.
    connect(&m_session, &EditorSession::aboutToChangePage, this, [this] { m_state->finishText(); });
    // A page switch is no document change, but everything the canvas built belongs to the old page.
    connect(&m_session, &EditorSession::currentPageChanged, this, [this] {
        m_state->finishText();
        m_state->drag.reset();
        m_state->pen.reset();
        m_state->documentChanged();
        m_state->updateCursor();
        update();
    });
    connect(&m_session, &EditorSession::changed, this, [this] {
        m_state->toolChanged();
        m_state->syncRulers();
        update();
        noteGesture();
    });
    m_state->rulers = new Rulers(session, this);
    m_state->syncRulers();
    m_state->updateCursor();
}

void EditorCanvas::State::syncRulers()
{
    if (!rulers)
        return;
    rulers->setGeometry(canvas.rect());
    rulers->setVisible(session.showsRulers && session.hasDocument());
    rulers->update();
}

EditorCanvas::~EditorCanvas() = default;

bool EditorCanvas::isEditingText() const
{
    return m_state->text != nullptr;
}

bool EditorCanvas::kernAtCaret(double amount)
{
    const auto &text = m_state->text;
    if (!text || !text->inDocument || text->caret != text->anchor || text->caret <= 0 || text->caret >= text->text().size())
        return false;
    // A kern sits between two characters of one line.
    if (text->text().at(text->caret - 1) == QLatin1Char('\n') || text->text().at(text->caret) == QLatin1Char('\n'))
        return false;
    m_session.kernText(text->object.id, text->caret, amount);
    return true;
}

void EditorCanvas::finishTextEditing()
{
    m_state->finishText();
}

void EditorCanvas::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    if (paused)
        m_state->cancelDrag();
    m_state->updateCursor();
    update();
    noteGesture();
    emit gestureChanged();
}

std::optional<QRectF> EditorCanvas::selectionViewRect() const
{
    if (!m_session.hasDocument() || !m_session.hasSelection() || m_state->text)
        return std::nullopt;
    return m_state->documentToView().mapRect(m_session.selectionBounds(true));
}

std::vector<QPointF> EditorCanvas::selectionHandles() const
{
    std::vector<QPointF> points;
    if (const std::optional<QRectF> box = m_state->selectionBox()) {
        for (int index = 0; index < 8; ++index) {
            if (m_state->handleShown(*box, index))
                points.push_back(m_state->handlePoint(*box, index));
        }
    }
    return points;
}

bool EditorCanvas::isGesturing() const
{
    return (m_state->drag && m_state->drag->started && m_state->drag->kind != State::DragKind::pan) || m_state->pen.has_value();
}

void EditorCanvas::noteGesture()
{
    if (isGesturing() == m_gesturing)
        return;
    m_gesturing = !m_gesturing;
    emit gestureChanged();
}

// Geometry ---------------------------------------------------------------------

QSizeF EditorCanvas::State::documentSize() const
{
    return session.document() ? session.document()->viewSize() : QSizeF(1, 1);
}

double EditorCanvas::State::scale() const
{
    return session.viewport.pointsPerPixel();
}

QPointF EditorCanvas::State::toDocument(QPointF view) const
{
    return session.viewport.documentPoint(view, documentSize());
}

QPointF EditorCanvas::State::toView(QPointF document) const
{
    return session.viewport.viewPoint(document, documentSize());
}

QTransform EditorCanvas::State::documentToView() const
{
    const QPointF origin = session.viewport.documentRect(documentSize()).topLeft();
    return QTransform::fromScale(scale(), scale()) * QTransform::fromTranslate(origin.x(), origin.y());
}

void EditorCanvas::State::syncViewport()
{
    session.resizeView(QSizeF(canvas.size()), canvas.devicePixelRatioF());
}

EditorCanvas::State::Drag &EditorCanvas::State::beginDrag(DragKind kind, QPointF view)
{
    drag.emplace();
    drag->kind = kind;
    drag->pressView = drag->lastView = view;
    drag->pressDocument = toDocument(view);
    return *drag;
}

bool EditorCanvas::State::pastDragDistance(QPointF view) const
{
    return drag && (view - drag->pressView).manhattanLength() >= std::max(3, QGuiApplication::styleHints()->startDragDistance() / 3);
}

// Events -----------------------------------------------------------------------

bool EditorCanvas::event(QEvent *event)
{
    // Open type takes its editing keys ahead of the menus.
    if (event->type() == QEvent::ShortcutOverride && m_state->text) {
        // Typed characters are text, never a menu's plain-key alias (Shift+1, Shift+2).
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool typed = !key->text().isEmpty() && key->text().at(0).isPrint() && !(key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
        if (typed || InlineTextEditor::claims(*key)) {
            event->accept();
            return true;
        }
    }
    // Tab would move focus away mid-drawing.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab && m_state->text)
        return true;
    // Figma's Tab walks the selection along its siblings; with none it moves focus as usual.
    if (event->type() == QEvent::KeyPress && !m_paused && !m_state->text && !m_state->drag && m_session.hasSelection()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool plain = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (plain && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)) {
            m_session.selectSibling(key->key() == Qt::Key_Tab);
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::NativeGesture) {
        const auto *gesture = static_cast<QNativeGestureEvent *>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture && !m_state->drag && m_session.hasDocument()) {
            m_state->zoomAt(m_session.viewport.zoom() * (1 + gesture->value()), gesture->position());
            event->accept();
            return true;
        }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange)
        m_state->syncViewport();
#endif
    return QWidget::event(event);
}

void EditorCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_state->syncViewport();
    m_state->syncRulers();
}

void EditorCanvas::paintEvent(QPaintEvent *)
{
    // Another screen may scale differently.
    if (m_session.viewport.backingScale != std::max(1.0, devicePixelRatioF()) || m_session.viewport.viewSize != QSizeF(size()))
        m_state->syncViewport();
    QPainter painter(this);
    m_state->paint(painter);
}

void EditorCanvas::mousePressEvent(QMouseEvent *event)
{
    m_state->modifiers = event->modifiers();
    m_state->hover = event->position();
    if (!m_session.hasDocument())
        return;
    // The middle button pans with any tool.
    if (event->button() == Qt::MiddleButton && !m_state->drag) {
        m_state->beginDrag(State::DragKind::pan, event->position());
        m_state->updateCursor();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    if (m_paused)
        return;
    // A press starts afresh: Qt can lose a release.
    if (m_state->drag)
        m_state->release(event->position(), event->modifiers());
    m_state->press(event->position(), event->modifiers());
    m_state->updateCursor();
    update();
    noteGesture();
}

void EditorCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_state->modifiers = event->modifiers();
    m_state->hover = event->position();
    const bool held = event->buttons() & (Qt::LeftButton | Qt::MiddleButton);
    m_state->rulers->setMarker(event->position());
    m_state->move(event->position(), event->modifiers(), held);
    m_state->updateCursor();
    noteGesture();
    if (m_session.hasDocument())
        emit pointerMoved(QRectF(QPointF(0, 0), m_state->documentSize()).contains(m_state->toDocument(event->position()))
                              ? std::optional(m_state->toDocument(event->position()))
                              : std::nullopt);
}

void EditorCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    m_state->modifiers = event->modifiers();
    if (event->button() != Qt::LeftButton && !(event->button() == Qt::MiddleButton && m_state->drag && m_state->drag->kind == State::DragKind::pan)) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_state->release(event->position(), event->modifiers());
    m_state->updateCursor();
    update();
    noteGesture();
}

void EditorCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_session.hasDocument() || m_paused) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    m_state->doubleClick(event->position(), event->modifiers());
    update();
}

void EditorCanvas::keyPressEvent(QKeyEvent *event)
{
    if (m_paused) {
        QWidget::keyPressEvent(event);
        return;
    }
    m_state->modifiers = event->modifiers();
    if (!m_state->keyPress(event))
        QWidget::keyPressEvent(event);
    m_state->updateCursor();
    noteGesture();
}

void EditorCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (m_paused) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    m_state->modifiers = event->modifiers();
    if (!m_state->keyRelease(event))
        QWidget::keyReleaseEvent(event);
    m_state->updateCursor();
}

void EditorCanvas::focusOutEvent(QFocusEvent *event)
{
    // A popup keeps the drag: the pointer comes back to finish it.
    if (event->reason() != Qt::PopupFocusReason) {
        m_state->cancelDrag();
        m_state->spaceHeld = false;
    }
    m_state->caretShown = false;
    m_state->updateCursor();
    update();
    noteGesture();
    QWidget::focusOutEvent(event);
}

void EditorCanvas::leaveEvent(QEvent *event)
{
    m_state->hover.reset();
    m_state->rulers->setMarker(std::nullopt);
    m_state->updateHoverGuides(std::nullopt);
    m_state->updateBuilderHover(std::nullopt);
    if (m_state->hovered) {
        m_state->hovered.reset();
        update();
    }
    emit pointerMoved(std::nullopt);
    QWidget::leaveEvent(event);
}

void EditorCanvas::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_state->text) {
        QWidget::inputMethodEvent(event);
        return;
    }
    if (m_state->text->inputMethod(*event) == InlineTextEditor::Result::edited)
        m_state->applyText();
    m_state->restartCaret();
    event->accept();
    update();
}

QVariant EditorCanvas::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (!m_state->text)
        return QWidget::inputMethodQuery(query);
    return m_state->text->inputMethodQuery(query, m_state->documentToView());
}
