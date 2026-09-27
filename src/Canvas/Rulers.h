#pragma once
#include "Document/EditorSession.h"
#include <QWidget>
#include <optional>

// View ▸ Rulers: a top and a left strip over the canvas, in points, with the
// pointer marked on each. Clicks pass through to the canvas, which draws guides out of them.
class Rulers : public QWidget {
    Q_OBJECT
public:
    static constexpr int thickness = 18;
    Rulers(EditorSession &session, QWidget *canvas);
    // Where the pointer is, in view coordinates; nullopt hides the marks.
    void setMarker(std::optional<QPointF> view);
    std::optional<QPointF> marker() const { return m_marker; }
    // The labelled step in points: 1, 2 or 5 × 10ⁿ, at least 50 view points apart at `pointsPerUnit`.
    static double labelStep(double pointsPerUnit);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    EditorSession &m_session;
    std::optional<QPointF> m_marker;
};
