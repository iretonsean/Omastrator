#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <functional>

class EditorCanvas;

// The bar under the picked page elements in Edit Page (docs/LIVE-IN-FRAME.md, section 3): a fixed height at every zoom,
// 8 px from the selection. What it holds comes from the filler; the controls are rebuilt only when the picked kind
// changes, so a scrub in progress is never destroyed by the value it changes.
class ElementBar : public QFrame {
    Q_OBJECT
public:
    explicit ElementBar(EditorCanvas &canvas);

    // `signature` names what the controls are for ("" hides the bar); `fill` builds them; `sync` sets their values.
    void setFiller(std::function<QString()> signature, std::function<void(QHBoxLayout &row)> fill, std::function<void()> sync);
    // Shows, hides, refills or syncs, and places the bar for the canvas as it is now.
    void refresh();
    QString shownSignature() const { return m_signature; }

    // The bar's place for a selection: centred under it, above when there's no room below, then kept inside `visible`.
    static QRect place(const QRectF &selection, QSize bar, const QRectF &visible, int gap = defaultGap);
    static constexpr int defaultGap = 8;
    static constexpr int barHeight = 28;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void refill();
    void reposition();

    EditorCanvas &m_canvas;
    QHBoxLayout *const m_row;
    std::function<QString()> m_signatureOf;
    std::function<void(QHBoxLayout &)> m_fill;
    std::function<void()> m_sync;
    QString m_signature;
};
