#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include <QAbstractButton>
#include <QWidget>
#include <functional>

// The first selected object's style, else the defaults.
namespace ShownStyle {
Paint fill(const EditorSession &session);
StrokeStyle stroke(const EditorSession &session);
}

// A fill or stroke well; none shows a red slash.
class PaintSwatch : public QAbstractButton {
    Q_OBJECT
public:
    PaintSwatch(std::function<Paint()> paint, bool stroke, QWidget *parent);
    // A stroke well is a thick ring.
    static void draw(QPainter &painter, const QRectF &rect, const Paint &paint, bool stroke);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const std::function<Paint()> m_paint;
    const bool m_stroke;
};

// The rail's fill and stroke wells, swap, defaults and picker.
class ColorPaletteControls : public QWidget {
    Q_OBJECT
public:
    explicit ColorPaletteControls(EditorSession &session, QWidget *parent = nullptr);
    void pickFill();
    void pickStroke();

private:
    void synchronize();

    EditorSession &m_session;
    PaintSwatch *const m_stroke;
    PaintSwatch *const m_fill;
    QAbstractButton *const m_swap;
    QAbstractButton *const m_reset;
    FloatingPanel m_picker{QStringLiteral("colorPickerPanel"), *this};
};
