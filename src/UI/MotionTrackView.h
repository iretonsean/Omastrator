#pragma once
#include <QRect>
#include <QWidget>

class MotionTimeline;

// The timeline's body: the ruler, one row per track with its bars, the divider and second ruler of the scroll-driven
// rows, and the playhead. It only draws and reports clicks; MotionTimeline owns what they mean.
class MotionTrackView : public QWidget {
    Q_OBJECT
public:
    explicit MotionTrackView(MotionTimeline &owner, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    static constexpr int labelWidth = 176;
    static constexpr int rowHeight = 30;
    static constexpr int rulerHeight = 22;
    static constexpr int gutter = 14;

    // Geometry as painted, for hit tests and for tests.
    QRect rulerRect(bool scroll) const;
    QRect rowRect(const QString &id) const;
    // The row of one element of an open group, and the triangle that opens and closes a group's row.
    QRect childRect(const QString &id, int bar) const;
    QRect expanderRect(const QString &id) const;
    // `inChild`: the bar as drawn in that element's own row of an open group, else on the track's row.
    QRect barRect(const QString &id, int bar, bool inChild = false) const;
    int xForTime(double ms) const;
    double timeAtX(int x) const;
    int xForScroll(double px) const;
    double scrollAtX(int x) const;
    // The height the rows need, so the dock can size itself.
    int contentHeight() const;
    // The top of each region, or -1 when the page has none.
    int timeTop() const;
    int scrollTop() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Layout;
    Layout layout() const;
    double timeSpan() const;
    double scrollSpan() const;
    MotionTimeline &m_owner;
    enum class Drag { none, time, scroll };
    Drag m_drag = Drag::none;
};
