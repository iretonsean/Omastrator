#pragma once
#include <QWidget>
#include <array>

// An easing as a curve with two handles (docs/MOTION.md, section 3): dragging a handle previews the value as it moves, and
// letting go commits it. The value is shown as text by the field beside it: "cubic-bezier(0.16, 1, 0.3, 1)".
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {150, 130}; }
    // The two control points as x1, y1, x2, y2. x stays between 0 and 1; y may go past both, up to overshoot.
    void setCurve(const std::array<double, 4> &points);
    std::array<double, 4> curve() const { return m_points; }
    // Where a handle is drawn (1 or 2), for hit tests and for tests.
    QPointF handle(int which) const;
    QPointF pointOf(double x, double y) const;

signals:
    // A handle moved; the text is cubic-bezier(…).
    void previewed(const QString &css);
    // The handle was let go.
    void committed(const QString &css);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF plot() const;
    std::array<double, 4> m_points{0.25, 0.1, 0.25, 1};
    int m_dragging = 0;
    static constexpr double lowest = -0.5;
    static constexpr double highest = 1.5;
};
