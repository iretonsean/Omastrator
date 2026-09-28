#include "UI/OmarchyStyle.h"
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>
#include <algorithm>

namespace {
// A framed list or text box drawn as a group: its viewport lets the rounded group tone through.
const char *groupProperty = "omarchyGroup";

QColor mixed(const QColor &over, const QColor &under, double weight)
{
    return QColor::fromRgbF(float(over.redF() * weight + under.redF() * (1 - weight)), float(over.greenF() * weight + under.greenF() * (1 - weight)),
                            float(over.blueF() * weight + under.blueF() * (1 - weight)));
}

bool isChosenButton(const QStyleOption *option)
{
    if (option->state & QStyle::State_On)
        return true;
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    return button && (button->features & QStyleOptionButton::DefaultButton);
}
}

OmarchyStyle::OmarchyStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))), m_colors(OmarchyColors::builtInDark())
{
    setObjectName(QStringLiteral("omarchy"));
}

QColor OmarchyStyle::controlFill(const QStyleOption *option, bool chosen) const
{
    const QStyle::State state = option->state;
    if (!(state & State_Enabled))
        return chosen ? mixed(m_colors.accentSoft, m_colors.surface1, 0.45) : mixed(m_colors.surface2, m_colors.surface1, 0.6);
    if (chosen)
        return state & State_Sunken ? m_colors.accentSoft.darker(115) : m_colors.accentSoft;
    if (state & State_Sunken)
        return mixed(m_colors.text2, m_colors.lift, 0.08);
    if (state & State_MouseOver)
        return m_colors.lift;
    return m_colors.surface2;
}

void OmarchyStyle::fillRounded(QPainter *painter, const QRectF &rect, const QColor &fill, qreal radius) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, radius, radius);
    painter->restore();
}

void OmarchyStyle::drawChevron(QPainter *painter, const QRectF &rect, bool down, const QColor &colour) const
{
    const QPointF centre = rect.center();
    const qreal half = 3.5, rise = down ? 1.75 : -1.75;
    QPainterPath path;
    path.moveTo(centre.x() - half, centre.y() - rise);
    path.lineTo(centre.x(), centre.y() + rise);
    path.lineTo(centre.x() + half, centre.y() - rise);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->restore();
}

void OmarchyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel:
        fillRounded(painter, QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), controlFill(option, isChosenButton(option)), rowRadius);
        return;
    case PE_PanelButtonTool: {
        const bool raised = option->state & (State_MouseOver | State_Sunken | State_On);
        if (!raised && (option->state & State_AutoRaise))
            return;
        // A chosen tool lifts and stays lifted; the accent would drown its icon.
        const QColor fill = option->state & State_On ? (option->state & State_MouseOver ? mixed(m_colors.text2, m_colors.lift, 0.06) : m_colors.lift)
                                                     : controlFill(option, false);
        fillRounded(painter, QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), fill, rowRadius);
        return;
    }
    case PE_FrameDefaultButton:
    case PE_FrameButtonTool:
    case PE_FrameButtonBevel:
    case PE_FrameLineEdit:
    case PE_FrameFocusRect:
        return;
    case PE_PanelLineEdit: {
        const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        if (frame && frame->lineWidth <= 0)
            return;
        const QColor fill = !(option->state & State_Enabled) ? mixed(m_colors.surface2, m_colors.surface1, 0.6)
                            : option->state & State_HasFocus ? m_colors.lift
                            : option->state & State_MouseOver ? mixed(m_colors.lift, m_colors.surface2, 0.5)
                                                              : m_colors.surface2;
        fillRounded(painter, QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), fill, fieldRadius);
        return;
    }
    case PE_IndicatorCheckBox: {
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool on = option->state & (State_On | State_NoChange);
        const bool enabled = option->state & State_Enabled;
        QColor fill = on ? m_colors.accent : option->state & State_MouseOver ? m_colors.lift : m_colors.surface3;
        if (!enabled)
            fill = mixed(fill, m_colors.surface1, 0.45);
        fillRounded(painter, box, fill, 4);
        if (!on)
            return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(enabled ? QColor(Qt::white) : m_colors.text3, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal w = box.width(), h = box.height();
        if (option->state & State_NoChange) {
            painter->drawLine(QPointF(box.left() + w * 0.28, box.center().y()), QPointF(box.right() - w * 0.28, box.center().y()));
        } else {
            QPainterPath tick;
            tick.moveTo(box.left() + w * 0.25, box.top() + h * 0.52);
            tick.lineTo(box.left() + w * 0.43, box.top() + h * 0.70);
            tick.lineTo(box.left() + w * 0.76, box.top() + h * 0.32);
            painter->drawPath(tick);
        }
        painter->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        const QRectF circle = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool on = option->state & State_On;
        QColor fill = on ? m_colors.accent : option->state & State_MouseOver ? m_colors.lift : m_colors.surface3;
        if (!(option->state & State_Enabled))
            fill = mixed(fill, m_colors.surface1, 0.45);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawEllipse(circle);
        if (on) {
            painter->setBrush(Qt::white);
            const qreal dot = circle.width() * 0.36;
            painter->drawEllipse(circle.center(), dot / 2, dot / 2);
        }
        painter->restore();
        return;
    }
    case PE_PanelItemViewItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option)) {
            // A chosen row takes the soft accent, a row under the pointer lifts; both rounded, inset from the edges.
            const bool chosen = item->state & State_Selected;
            if (!chosen && !(item->state & State_MouseOver))
                return QProxyStyle::drawPrimitive(element, option, painter, widget);
            QRectF row = QRectF(item->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            if (item->viewItemPosition == QStyleOptionViewItem::OnlyOne || item->viewItemPosition == QStyleOptionViewItem::Invalid)
                row.adjust(4, 1, -4, -1);
            fillRounded(painter, row, chosen ? m_colors.accentSoft : m_colors.lift, rowRadius);
            return;
        }
        break;
    case PE_PanelTipLabel:
        painter->fillRect(option->rect, option->palette.toolTipBase());
        return;
    case PE_FrameMenu:
        // Menus lie over panels of their own tone: a hairline in the lift tone keeps them apart.
        painter->save();
        painter->setPen(m_colors.lift);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRect(option->rect).adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
}

void OmarchyStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case CE_PushButtonLabel:
        if (isChosenButton(option) && (option->state & State_Enabled)) {
            if (const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option)) {
                QStyleOptionButton chosen = *button;
                chosen.palette.setColor(QPalette::ButtonText, m_colors.onAccent);
                QProxyStyle::drawControl(element, &chosen, painter, widget);
                return;
            }
        }
        break;
    case CE_ItemViewItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option); item && (item->state & State_Selected)) {
            // Dark words on the soft accent, whichever group the view is in.
            QStyleOptionViewItem chosen = *item;
            for (const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive})
                chosen.palette.setColor(group, QPalette::HighlightedText, m_colors.onAccent);
            QProxyStyle::drawControl(element, &chosen, painter, widget);
            return;
        }
        break;
    case CE_ShapedFrame:
        if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option)) {
            if (widget && widget->property(groupProperty).toBool()) {
                fillRounded(painter, QRectF(frame->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_colors.surface2, 8);
                return;
            }
            // No outlines: tone separates surfaces. Divider lines stay.
            if (frame->frameShape == QFrame::StyledPanel || frame->frameShape == QFrame::Panel || frame->frameShape == QFrame::WinPanel
                || frame->frameShape == QFrame::Box)
                return;
        }
        break;
    case CE_MenuItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
            item && (item->state & State_Selected) && (item->state & State_Enabled) && item->menuItemType != QStyleOptionMenuItem::Separator) {
            fillRounded(painter, QRectF(item->rect).adjusted(4.5, 0.5, -4.5, -0.5), m_colors.lift, rowRadius);
            QStyleOptionMenuItem plain = *item;
            plain.state &= ~State_Selected;
            plain.palette.setColor(QPalette::WindowText, m_colors.text1);
            plain.palette.setColor(QPalette::ButtonText, m_colors.text1);
            plain.palette.setColor(QPalette::Text, m_colors.text1);
            QProxyStyle::drawControl(element, &plain, painter, widget);
            return;
        }
        break;
    case CE_MenuBarItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            painter->fillRect(item->rect, item->palette.window());
            const bool lit = (item->state & State_Selected) && (item->state & (State_Sunken | State_MouseOver | State_HasFocus));
            if (lit)
                fillRounded(painter, QRectF(item->rect).adjusted(0.5, 3.5, -0.5, -3.5), m_colors.lift, rowRadius);
            QPalette palette = item->palette;
            palette.setColor(QPalette::WindowText, lit ? m_colors.text1 : item->palette.color(QPalette::WindowText));
            proxy()->drawItemText(painter, item->rect, Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip | Qt::TextSingleLine, palette,
                                  item->state & State_Enabled, item->text, QPalette::WindowText);
            return;
        }
        break;
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void OmarchyStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    switch (control) {
    case CC_ComboBox:
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            if (combo->frame || (combo->state & State_MouseOver)) {
                QStyleOption state = *combo;
                if (combo->editable && (combo->state & State_HasFocus))
                    state.state |= State_MouseOver;
                fillRounded(painter, QRectF(combo->rect).adjusted(0.5, 0.5, -0.5, -0.5), controlFill(&state, false), fieldRadius);
            }
            if (combo->subControls & SC_ComboBoxArrow) {
                const QRect arrow = proxy()->subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
                drawChevron(painter, arrow, true, combo->state & State_Enabled ? m_colors.text3 : m_colors.text4);
            }
            return;
        }
        break;
    case CC_SpinBox:
        if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
            if (spin->frame) {
                QStyleOptionFrame field;
                field.QStyleOption::operator=(*spin);
                field.lineWidth = 1;
                drawPrimitive(PE_PanelLineEdit, &field, painter, widget);
            }
            if (spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                const QRect up = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
                const QRect down = proxy()->subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);
                const auto tone = [this, spin](QAbstractSpinBox::StepEnabledFlag flag, SubControl sub) {
                    if (!(spin->state & State_Enabled) || !(spin->stepEnabled & flag))
                        return m_colors.text4;
                    return spin->activeSubControls == sub && (spin->state & State_MouseOver) ? m_colors.text1 : m_colors.text3;
                };
                drawChevron(painter, QRectF(up).translated(0, 1), false, tone(QAbstractSpinBox::StepUpEnabled, SC_SpinBoxUp));
                drawChevron(painter, QRectF(down).translated(0, -1), true, tone(QAbstractSpinBox::StepDownEnabled, SC_SpinBoxDown));
            }
            return;
        }
        break;
    case CC_Slider:
        if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            const QRect groove = proxy()->subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
            const QRect handle = proxy()->subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
            const bool horizontal = slider->orientation == Qt::Horizontal;
            const bool enabled = slider->state & State_Enabled;
            // A 4 px track, the accent up to the knob.
            const QRectF track = horizontal ? QRectF(groove.left(), groove.center().y() - 1.5, groove.width(), 4)
                                            : QRectF(groove.center().x() - 1.5, groove.top(), 4, groove.height());
            fillRounded(painter, track, m_colors.surface3, 2);
            const QPointF knob = QRectF(handle).center();
            QRectF filled = track;
            if (horizontal)
                (slider->upsideDown ? filled.setLeft(knob.x()) : filled.setRight(knob.x()));
            else
                (slider->upsideDown ? filled.setBottom(knob.y()) : filled.setTop(knob.y()));
            fillRounded(painter, filled, enabled ? m_colors.accent : m_colors.text4, 2);
            const qreal size = std::min<qreal>(14, std::min(handle.width(), handle.height()));
            const QRectF knobRect(knob.x() - size / 2, knob.y() - size / 2, size, size);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 90));
            painter->drawEllipse(knobRect.translated(0, 1));
            painter->setBrush(enabled ? (slider->state & State_Sunken ? m_colors.text2 : m_colors.text1) : m_colors.text4);
            painter->drawEllipse(knobRect);
            painter->restore();
            return;
        }
        break;
    case CC_ScrollBar:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            // No arrows, no groove: a rounded thumb that brightens under the pointer.
            const QRect thumb = proxy()->subControlRect(CC_ScrollBar, bar, SC_ScrollBarSlider, widget);
            const bool hot = bar->activeSubControls & SC_ScrollBarSlider;
            const QColor fill = bar->state & State_Sunken && hot ? m_colors.text3 : hot ? mixed(m_colors.text3, m_colors.text4, 0.5) : m_colors.text4;
            const QRectF inset = QRectF(thumb).adjusted(2.5, 2.5, -2.5, -2.5);
            fillRounded(painter, inset, fill, std::min(inset.width(), inset.height()) / 2);
            return;
        }
        break;
    default:
        break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect OmarchyStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sub, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            const QRect whole = bar->rect;
            const bool horizontal = bar->orientation == Qt::Horizontal;
            const int length = horizontal ? whole.width() : whole.height();
            const int span = bar->maximum - bar->minimum;
            const int least = proxy()->pixelMetric(PM_ScrollBarSliderMin, bar, widget);
            const int thumb = span <= 0 ? length
                                        : std::clamp(int(qint64(length) * bar->pageStep / (qint64(span) + bar->pageStep)), std::min(least, length), length);
            const int at = sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition, length - thumb, bar->upsideDown);
            const auto along = [&](int start, int size) {
                return horizontal ? QRect(whole.left() + start, whole.top(), size, whole.height())
                                  : QRect(whole.left(), whole.top() + start, whole.width(), size);
            };
            switch (sub) {
            case SC_ScrollBarSlider:
                return along(at, thumb);
            case SC_ScrollBarSubPage:
                return along(0, at);
            case SC_ScrollBarAddPage:
                return along(at + thumb, length - at - thumb);
            case SC_ScrollBarGroove:
                return whole;
            case SC_ScrollBarAddLine:
            case SC_ScrollBarSubLine:
            case SC_ScrollBarFirst:
            case SC_ScrollBarLast:
                return {};
            default:
                break;
            }
        }
    }
    return QProxyStyle::subControlRect(control, option, sub, widget);
}

int OmarchyStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_ScrollBarExtent:
        return 11;
    case PM_ScrollBarSliderMin:
        return 28;
    case PM_FocusFrameHMargin:
        // Also the words' margin in list rows, clear of the rounded chosen fill.
        return 7;
    case PM_ButtonShiftHorizontal:
    case PM_ButtonShiftVertical:
        return 0;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize OmarchyStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
    // List rows as tall as the theme's data rows.
    if (type == CT_ItemViewItem)
        result.setHeight(std::max(result.height() + 6, 24));
    return result;
}

void OmarchyStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    auto *area = qobject_cast<QAbstractScrollArea *>(widget);
    // Lists and text boxes with a frame, painting their own Base; not the popups of combo boxes.
    if (!area || area->frameShape() == QFrame::NoFrame || widget->inherits("QComboBoxListView") || !area->viewport()->autoFillBackground()
        || area->viewport()->backgroundRole() != QPalette::Base)
        return;
    area->viewport()->setAutoFillBackground(false);
    // The group's padding.
    area->setContentsMargins(4, 4, 4, 4);
    widget->setProperty(groupProperty, true);
}

void OmarchyStyle::unpolish(QWidget *widget)
{
    if (widget->property(groupProperty).toBool()) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(widget)) {
            area->viewport()->setAutoFillBackground(true);
            area->setContentsMargins(0, 0, 0, 0);
        }
        widget->setProperty(groupProperty, QVariant());
    }
    QProxyStyle::unpolish(widget);
}
