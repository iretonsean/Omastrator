#pragma once
#include <QPointer>
#include <QWidget>

class MotionTimeline;
class QVBoxLayout;

// The Motion inspector (docs/MOTION.md, section 3): what the selected timeline row does, as designer controls: what starts
// it, its easing, duration and stagger, the motion tokens it uses, the reduced-motion rule and the keyframes. It follows
// the timeline's selection.
class MotionInspector : public QWidget {
    Q_OBJECT
public:
    explicit MotionInspector(MotionTimeline &timeline, QWidget *parent = nullptr);

private:
    void rebuild();
    MotionTimeline &m_timeline;
    QVBoxLayout *const m_outer;
    QPointer<QWidget> m_body;
};
