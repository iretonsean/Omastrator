#include "UI/LayerAppearanceControls.h"
#include "UI/BlendModePicker.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <cmath>

namespace {
QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

LayerAppearanceControls::LayerAppearanceControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_picker(new BlendModePicker(session, this)), m_slider(new QSlider(Qt::Horizontal, this)),
      m_percentage(new QLineEdit(this))
{
    setObjectName(QStringLiteral("layerAppearance"));
    // One row: the blend mode, then opacity (docs/PANELS.md). The menu names itself ("Normal").
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto *opacity = new QHBoxLayout;
    opacity->setSpacing(6);
    m_picker->setToolTip(QStringLiteral("Blend mode"));
    opacity->addWidget(m_picker, 1);
    // A thousandth a step; a drag is one undo step.
    m_slider->setRange(0, 1000);
    m_slider->setObjectName(QStringLiteral("opacitySlider"));
    m_slider->setAccessibleName(QStringLiteral("Opacity"));
    m_slider->setToolTip(QStringLiteral("Opacity"));
    opacity->addWidget(m_slider, 1);
    m_percentage->setObjectName(QStringLiteral("opacityPercent"));
    m_percentage->setAccessibleName(QStringLiteral("Opacity percent"));
    m_percentage->setFixedWidth(40);
    m_percentage->installEventFilter(this);
    opacity->addWidget(m_percentage);
    opacity->addWidget(caption(QStringLiteral("%"), this));
    column->addLayout(opacity);
    // A release in a popup is watched application-wide.
    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        m_dragging = true;
        m_session.beginEdit(QStringLiteral("Opacity"));
        qApp->installEventFilter(this);
    });
    connect(m_slider, &QSlider::sliderReleased, this, &LayerAppearanceControls::endDrag);
    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_syncing)
            m_session.setOpacityOfSelection(value / 1000.0);
    });
    connect(&m_session, &EditorSession::changed, this, &LayerAppearanceControls::synchronize);
    synchronize();
}

// A drag never outlives its controls.
LayerAppearanceControls::~LayerAppearanceControls()
{
    endDrag();
}

void LayerAppearanceControls::endDrag()
{
    if (!m_dragging)
        return;
    m_dragging = false;
    qApp->removeEventFilter(this);
    m_session.endEdit();
}

double LayerAppearanceControls::shownOpacity() const
{
    if (!m_session.hasSelection())
        return 1;
    const VectorObject *first = m_session.document()->find(m_session.selection().front());
    return first ? first->opacity : 1;
}

void LayerAppearanceControls::synchronize()
{
    setEnabled(m_session.hasSelection());
    m_syncing = true;
    m_slider->setValue(int(std::lround(shownOpacity() * 1000)));
    m_syncing = false;
    if (!m_percentage->hasFocus())
        m_percentage->setText(QString::number(std::lround(shownOpacity() * 100)));
}

void LayerAppearanceControls::applyPercentage()
{
    bool number = false;
    const double value = m_percentage->text().toDouble(&number);
    if (number && std::isfinite(value) && std::abs(value / 100 - shownOpacity()) > 1e-9)
        m_session.setOpacityOfSelection(value / 100);
    m_percentage->setText(QString::number(std::lround(shownOpacity() * 100)));
}

bool LayerAppearanceControls::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_percentage) {
        const bool release = event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        if (release && m_slider->isSliderDown())
            m_slider->setSliderDown(false);
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        // A menu borrows focus and gives it back: no leaving.
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        if (reason != Qt::MenuBarFocusReason && reason != Qt::PopupFocusReason)
            applyPercentage();
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        applyPercentage();
        return true;
    }
    if (key->key() != Qt::Key_Up && key->key() != Qt::Key_Down)
        return QWidget::eventFilter(watched, event);
    // Up and Down nudge one percent, ten with Shift.
    const double amount = (key->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (key->key() == Qt::Key_Up ? 1 : -1);
    m_session.setOpacityOfSelection((std::round(shownOpacity() * 100) + amount) / 100);
    m_percentage->setText(QString::number(std::lround(shownOpacity() * 100)));
    return true;
}
