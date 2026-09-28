#pragma once
#include "UI/OmarchyTheme.h"
#include <QProxyStyle>

// Fusion, drawn the way the desktop's theme draws its shell: no outlines
// (tone separates things), rounded controls, the lift under the pointer and
// the soft accent for what is chosen. Every colour comes from the theme, so a
// theme without component tokens still gets the same shapes in its colours.
class OmarchyStyle : public QProxyStyle {
    Q_OBJECT
public:
    OmarchyStyle();

    void setColors(const OmarchyColors &colors) { m_colors = colors; }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget = nullptr) const override;
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    using QProxyStyle::polish;
    using QProxyStyle::unpolish;

    static constexpr int rowRadius = 6;
    static constexpr int fieldRadius = 5;

private:
    // A control's fill: resting, lifted under the pointer, pressed, or chosen.
    QColor controlFill(const QStyleOption *option, bool chosen) const;
    void fillRounded(QPainter *painter, const QRectF &rect, const QColor &fill, qreal radius) const;
    void drawChevron(QPainter *painter, const QRectF &rect, bool down, const QColor &colour) const;

    OmarchyColors m_colors;
};
