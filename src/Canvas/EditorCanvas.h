#pragma once
#include "Document/EditorSession.h"
#include <QLineF>
#include <QList>
#include <QWidget>
#include <memory>
#include <optional>

class BrowserViewHost;
class QMenu;

// The artboard view: draws the document through VectorRenderer, pans and zooms
// through the session's CanvasViewport, and runs every tool in `Tool`.
class EditorCanvas : public QWidget {
    Q_OBJECT
public:
    explicit EditorCanvas(EditorSession &session, QWidget *parent = nullptr);
    ~EditorCanvas() override;
    EditorSession &session() const { return m_session; }
    // True while type is edited in place; menus leave the keys to it.
    bool isEditingText() const;
    // Ends in-place type editing, keeping what was typed.
    void finishTextEditing();
    // Alt+Left and Alt+Right at a caret with nothing selected: kerns the pair around it.
    // False when there's no such caret, so the keys track the whole text instead.
    bool kernAtCaret(double amount);
    // Paused, clicks and keys start nothing: an agent's proposal waits for Enter or Esc.
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }
    // Isolation: clicks pick inside this group until Esc or a click outside it.
    std::optional<QUuid> isolatedGroup() const;
    void isolateGroup(const QUuid &group);
    void exitIsolation();
    // Alt-hover distances from the selection to what's under the pointer, in document coordinates.
    std::vector<QLineF> measurements() const;
    // The Δx/Δy, W × H or angle label beside a drag; empty when none shows.
    QString dragReadout() const;
    // The selection's bounds in view coordinates; nullopt with nothing selected or while type is edited.
    std::optional<QRectF> selectionViewRect() const;
    // The Selection tool's handles, in view coordinates, where they show.
    std::vector<QPointF> selectionHandles() const;
    // A drag past the click distance, or a pen path, is under way.
    bool isGesturing() const;
    // In-place type's right-click menu: clipboard, case and special characters.
    QMenu *textEditingMenu(QWidget *parent);
    // Where a Browser View's live picture and messages come from; the canvas owns neither.
    void setBrowserViewHost(BrowserViewHost *host);
    BrowserViewHost *browserViewHost() const;
    // The Browser View's address field opens over its bar; Enter changes the URL, Escape leaves it.
    void openAddressEditor(const QUuid &frame);
    bool isEditingAddress() const;
    // Document points to view pixels, as the canvas draws them now.
    QTransform documentToView() const;
    // Arrow keys move this many points, ten times as far with Shift.
    static double keyboardIncrement();
    static void setKeyboardIncrement(double points);

signals:
    // A line for the status bar, as a URL the Browser View refuses.
    void notice(const QString &text);
    // The pointer's document position, for the status bar; nullopt off the artboard.
    void pointerMoved(std::optional<QPointF> documentPoint);
    void textEditingChanged(bool editing);
    // A right-click, once it picked its target: the leaves under the pointer, topmost first.
    void contextMenuRequested(QPoint globalPosition, const QList<QUuid> &underPointer);
    // A gesture began or ended, or the canvas paused or resumed.
    void gestureChanged();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    // Says so when a press, move, release or key started or ended a gesture.
    void noteGesture();

    EditorSession &m_session;
    struct State;
    std::unique_ptr<State> m_state;
    bool m_paused = false;
    bool m_gesturing = false;
};
