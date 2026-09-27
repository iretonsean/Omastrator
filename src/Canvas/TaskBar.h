#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QPointer>
#include <functional>
#include <optional>

class EditorCanvas;

// Illustrator's Contextual Task Bar, lighter: a small bar under the selection with its likeliest next actions.
// It places, hides and fades itself; what it holds comes from the filler, rebuilt when the selection's kind changes.
class TaskBar : public QFrame {
    Q_OBJECT
public:
    explicit TaskBar(EditorCanvas &canvas);

    // View ▸ Contextual Task Bar, remembered across launches.
    static bool isTurnedOn();
    static void setTurnedOn(bool on);
    // Where a moved bar sits relative to its own spot under the selection, remembered.
    static QPoint savedOffset();
    static void setSavedOffset(QPoint offset);
    // Pinned, the bar stays at one place in the view instead of following the selection.
    static bool isPinned();
    static void setPinned(bool pinned, QPoint at = QPoint());
    static QPoint pinnedAt();

    // `kind` names the selection's kind ("" hides the bar); `fill` adds its contents to the row after the grip.
    void setFiller(std::function<QString()> kind, std::function<void(QHBoxLayout &row)> fill);
    // Something outside the canvas holds the bar back, such as an agent's proposal.
    void setBlocked(std::function<bool()> blocked);
    // Shows, hides, refills and places the bar for the canvas as it is now.
    void refresh();
    QString shownKind() const { return m_kind; }
    // Near a handle the bar fades and lets clicks through.
    bool isFaded() const { return m_faded; }

    // The bar's place for a selection box in the view: centred under it, above when there's no room below,
    // over the bottom of the view when neither fits; then moved by `offset` and kept inside the view.
    static QRect place(const QRectF &selection, QSize bar, QSize view, QPoint offset = QPoint());
    // How far below or above the selection the bar keeps, clear of the rotate zone.
    static constexpr int gap = 26;
    static constexpr int margin = 8;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    friend class TaskBarGrip;
    void refill();
    void reposition();
    void setFaded(bool faded);
    void fadeFor(QPointF pointer);
    // The grip: dragging moves the bar and remembers where.
    void dragBy(QPoint delta, bool finished);

    EditorCanvas &m_canvas;
    QHBoxLayout *const m_row;
    QWidget *const m_grip;
    std::function<QString()> m_kindOf;
    std::function<void(QHBoxLayout &)> m_fill;
    std::function<bool()> m_blocked;
    QString m_kind;
    bool m_faded = false;
    // While the grip drags: the offset (or pinned place) the drag started from.
    std::optional<QPoint> m_dragStart;
};
