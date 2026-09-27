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

signals:
    // The pointer's document position, for the status bar; nullopt off the artboard.
    void pointerMoved(std::optional<QPointF> documentPoint);
    void textEditingChanged(bool editing);

private:
    EditorSession &m_session;
    struct State;
    std::unique_ptr<State> m_state;
};
