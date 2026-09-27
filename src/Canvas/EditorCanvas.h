#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <memory>
#include <optional>

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
    // Paused, clicks and keys start nothing: an agent's proposal waits for Enter or Esc.
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

signals:
    // The pointer's document position, for the status bar; nullopt off the artboard.
    void pointerMoved(std::optional<QPointF> documentPoint);
    void textEditingChanged(bool editing);

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
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    EditorSession &m_session;
    struct State;
    std::unique_ptr<State> m_state;
    bool m_paused = false;
};
