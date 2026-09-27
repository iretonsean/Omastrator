#include "UI/ColorPickerSheet.h"
#include "UI/KeyboardShortcuts.h"
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr double fieldSize = 256;

// A pointer drag reports each point it passes.
class Picking : public QWidget {
public:
    Picking(const ColorPickerSheet &sheet, std::function<void(QPointF)> pick, QWidget *parent)
        : QWidget(parent), m_sheet(sheet), m_pick(std::move(pick))
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_pick(event->position());
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons().testFlag(Qt::LeftButton))
            m_pick(event->position());
    }
    const ColorPickerSheet &m_sheet;

private:
    const std::function<void(QPointF)> m_pick;
};

// The saturation and brightness field, its marker ringed.
class SaturationBrightness : public Picking {
public:
    using Picking::Picking;

protected:
    void paintEvent(QPaintEvent *) override
    {
        const PickerHSB &hsb = m_sheet.hsb();
        QPainter painter(this);
        const QRectF rect(0, 0, fieldSize, fieldSize);
        QLinearGradient across(rect.topLeft(), rect.topRight());
        across.setColorAt(0, Qt::white);
        across.setColorAt(1, PickerHSB{hsb.hue, 1, 1}.color());
        painter.fillRect(rect, across);
        QLinearGradient down(rect.topLeft(), rect.bottomLeft());
        down.setColorAt(0, Qt::transparent);
        down.setColorAt(1, Qt::black);
        painter.fillRect(rect, down);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(Qt::NoBrush);
        const QPointF centre(hsb.saturation * (fieldSize - 1), (1 - hsb.brightness) * (fieldSize - 1));
        painter.setPen(QPen(Qt::black, 0.75));
        painter.drawEllipse(centre, 6.375, 6.375);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(centre, 5.25, 5.25);
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    }
};

// The hue strip, 360 on top; the sheet draws arrows.
class HueStrip : public Picking {
public:
    using Picking::Picking;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRectF strip(7, 0, 20, fieldSize);
        QLinearGradient down(strip.topLeft(), strip.bottomLeft());
        for (int step = 0; step <= 6; ++step)
            down.setColorAt(step / 6.0, PickerHSB{360.0 - step * 60, 1, 1}.color());
        painter.fillRect(strip, down);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(strip.adjusted(0.5, 0.5, -0.5, -0.5));
    }
};

// New colour on top, the one it replaces below.
class ColorPreview : public QWidget {
public:
    ColorPreview(const ColorPickerSheet &sheet, QColor original, QWidget *parent) : QWidget(parent), m_sheet(sheet), m_original(original) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()), 5, 5);
        painter.save();
        painter.setClipPath(shape);
        painter.fillRect(QRectF(0, 0, width(), height() / 2.0), m_sheet.color());
        painter.fillRect(QRectF(0, height() / 2.0, width(), height() / 2.0), m_original);
        painter.restore();
        painter.setPen(QPen(QColor(0, 0, 0, 153), 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4.5, 4.5);
    }

private:
    const ColorPickerSheet &m_sheet;
    const QColor m_original;
};

QLabel *label(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFixedWidth(14);
    return label;
}
}

PickerHSB PickerHSB::from(const QColor &color)
{
    const QColor hsv = color.toHsv();
    // Grey has no hue: Qt says −1, the picker 0.
    const double hue = hsv.hsvHueF() < 0 ? 0 : std::round(double(hsv.hsvHueF()) * 36000) / 100;
    return {hue, hsv.hsvSaturationF(), hsv.valueF()};
}

QColor PickerHSB::color() const
{
    // Eight bits a channel, as the fields and hex show.
    const QColor rgb = QColor::fromHsvF(float(std::fmod(hue, 360.0) / 360), float(saturation), float(brightness)).toRgb();
    return QColor(rgb.red(), rgb.green(), rgb.blue());
}

PickerField::PickerField(std::function<void()> commit, std::function<void(int)> step, QWidget *parent)
    : QLineEdit(parent), m_commit(std::move(commit)), m_step(std::move(step))
{
}

void PickerField::keyPressEvent(QKeyEvent *event)
{
    // One a press, ten with Shift.
    if (m_step && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        m_step((event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (event->key() == Qt::Key_Up ? 1 : -1));
        return;
    }
    // Return commits, then goes on to OK, the default.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        m_commit();
    QLineEdit::keyPressEvent(event);
}

void PickerField::focusOutEvent(QFocusEvent *event)
{
    // A menu or popup borrows the focus: the typing stays.
    if (event->reason() != Qt::MenuBarFocusReason && event->reason() != Qt::PopupFocusReason)
        m_commit();
    QLineEdit::focusOutEvent(event);
}

ColorPickerSheet::ColorPickerSheet(const QColor &initial, std::function<void(std::optional<QColor>)> finish, QWidget *parent)
    : QWidget(parent), m_original(initial), m_hsb(PickerHSB::from(initial)),
      m_field(new SaturationBrightness(*this, [this](QPointF point) {
          pick([point](PickerHSB &hsb) {
              hsb.saturation = std::clamp(point.x() / (fieldSize - 1), 0.0, 1.0);
              hsb.brightness = 1 - std::clamp(point.y() / (fieldSize - 1), 0.0, 1.0);
          });
      }, this)),
      m_hue(new HueStrip(*this, [this](QPointF point) {
          pick([point](PickerHSB &hsb) { hsb.hue = (1 - std::clamp(point.y() / (fieldSize - 1), 0.0, 1.0)) * 360; });
      }, this)),
      m_preview(new ColorPreview(*this, initial, this)), m_channels{channel(0), channel(1), channel(2)},
      m_hex(new PickerField([this] { commitHex(); }, nullptr, this)), m_ok(new QPushButton(QStringLiteral("OK"), this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
{
    setObjectName(QStringLiteral("colorPickerSheet"));
    m_field->setObjectName(QStringLiteral("saturationBrightness"));
    m_field->setFixedSize(int(fieldSize), int(fieldSize));
    m_field->setAccessibleName(QStringLiteral("Saturation and brightness"));
    m_hue->setObjectName(QStringLiteral("hueStrip"));
    m_hue->setFixedSize(34, int(fieldSize));
    m_hue->setAccessibleName(QStringLiteral("Hue"));
    m_preview->setObjectName(QStringLiteral("newColor"));
    m_preview->setFixedSize(64, 64);
    m_preview->setAccessibleName(QStringLiteral("New color"));
    m_ok->setObjectName(QStringLiteral("pickerOK"));
    m_ok->setDefault(true);
    NativeShortcut::bind(*this, m_ok, m_cancel);
    m_ok->setFixedWidth(90);
    m_cancel->setObjectName(QStringLiteral("pickerCancel"));
    m_cancel->setAutoDefault(false);
    m_cancel->setFixedWidth(90);
    connect(m_ok, &QPushButton::clicked, this, [this, finish] { finish(color()); });
    connect(m_cancel, &QPushButton::clicked, this, [finish] { finish(std::nullopt); });
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_ok);
    buttons->addWidget(m_cancel);
    buttons->addStretch();
    auto *top = new QHBoxLayout;
    top->setSpacing(16);
    top->addWidget(m_preview, 0, Qt::AlignTop);
    top->addLayout(buttons);
    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(6);
    for (int index = 0; index < 3; ++index) {
        fields->addWidget(label(QString(QStringLiteral("RGB").at(index)), this), index, 0);
        fields->addWidget(m_channels[size_t(index)], index, 1, Qt::AlignLeft);
    }
    m_hex->setObjectName(QStringLiteral("hex"));
    m_hex->setPlaceholderText(QStringLiteral("Hex"));
    m_hex->setAccessibleName(QStringLiteral("Hex color"));
    m_hex->setFixedWidth(84);
    m_hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    fields->addWidget(label(QStringLiteral("#"), this), 3, 0);
    fields->addWidget(m_hex, 3, 1, Qt::AlignLeft);
    auto *column = new QVBoxLayout;
    column->setSpacing(0);
    column->setContentsMargins(0, 0, 0, 0);
    column->addLayout(top);
    column->addStretch();
    column->addSpacing(12);
    column->addLayout(fields);
    auto *side = new QWidget(this);
    side->setFixedSize(180, int(fieldSize));
    side->setLayout(column);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(20, 20, 20, 20);
    row->setSpacing(14);
    row->addWidget(m_field, 0, Qt::AlignTop);
    row->addWidget(m_hue, 0, Qt::AlignTop);
    row->addWidget(side, 0, Qt::AlignTop);
    synchronize();
}

void ColorPickerSheet::showIn(FloatingPanel &panel, const QString &title, const QColor &initial, std::function<void(QColor)> apply)
{
    panel.onClose = [&panel] { panel.close(); };
    panel.show(title, new ColorPickerSheet(initial, [&panel, apply = std::move(apply)](std::optional<QColor> chosen) {
        panel.close();
        if (chosen)
            apply(*chosen);
    }));
}

PickerField *ColorPickerSheet::channel(int index)
{
    auto *field = new PickerField([this, index] {
        // Typed numbers clamp; overflows are 255.
        PickerField &edited = *m_channels[size_t(index)];
        if (edited.isModified() && !edited.text().isEmpty()) {
            bool fits = false;
            const qlonglong typed = edited.text().toLongLong(&fits);
            setChannel(index, fits ? double(typed) : 255);
        }
        synchronize();
    }, [this, index](int step) {
        const QColor rgb = color();
        setChannel(index, (index == 0 ? rgb.red() : index == 1 ? rgb.green() : rgb.blue()) + step);
        m_channels[size_t(index)]->setModified(false);
        synchronize();
    }, this);
    field->setObjectName(QString(QStringLiteral("rgb").at(index)));
    field->setPlaceholderText(QString(QStringLiteral("RGB").at(index)));
    field->setAccessibleName(QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}.at(index));
    field->setFixedWidth(52);
    field->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), field));
    return field;
}

void ColorPickerSheet::setHSB(const PickerHSB &hsb)
{
    if (hsb == m_hsb)
        return;
    m_hsb = hsb;
    synchronize();
    emit colorChanged(color());
}

void ColorPickerSheet::pick(const std::function<void(PickerHSB &)> &edit)
{
    PickerHSB next = m_hsb;
    edit(next);
    for (PickerField *channel : m_channels)
        channel->setModified(false);
    setHSB(next);
}

void ColorPickerSheet::setChannel(int channel, double value)
{
    QColor rgb = color();
    const int clamped = int(std::clamp(value, 0.0, 255.0));
    if (channel == 0)
        rgb.setRed(clamped);
    else if (channel == 1)
        rgb.setGreen(clamped);
    else
        rgb.setBlue(clamped);
    // A grey keeps the hue the strip showed.
    PickerHSB next = PickerHSB::from(rgb);
    if (next.saturation == 0)
        next.hue = m_hsb.hue;
    setHSB(next);
}

void ColorPickerSheet::commitHex()
{
    QString typed = m_hex->text().trimmed();
    if (!typed.startsWith(QLatin1Char('#')))
        typed.prepend(QLatin1Char('#'));
    const QColor parsed = QColor::fromString(typed);
    if (parsed.isValid() && typed.size() == 7)
        setHSB(PickerHSB::from(parsed));
    m_hex->setText(color().name().mid(1).toUpper());
}

void ColorPickerSheet::synchronize()
{
    const QColor rgb = color();
    const std::array<int, 3> values{rgb.red(), rgb.green(), rgb.blue()};
    // An entry being typed in keeps its typing.
    for (size_t index = 0; index < 3; ++index) {
        if (!m_channels[index]->hasFocus() || !m_channels[index]->isModified())
            m_channels[index]->setText(QString::number(values[index]));
    }
    if (!m_hex->hasFocus())
        m_hex->setText(rgb.name().mid(1).toUpper());
    m_hue->setAccessibleDescription(QStringLiteral("%1 degrees").arg(std::lround(m_hsb.hue)));
    m_field->update();
    // The arrows reach five points past the strip.
    update(m_hue->geometry().adjusted(0, -5, 0, 5));
    m_preview->update();
}

// The strip's arrows overflow it; widgets clip.
void ColorPickerSheet::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF strip(m_hue->geometry());
    const double y = strip.top() + (1 - m_hsb.hue / 360) * (fieldSize - 1);
    QPainterPath arrows;
    arrows.addPolygon(QPolygonF({QPointF(strip.left(), y - 5), QPointF(strip.left() + 7, y), QPointF(strip.left(), y + 5)}));
    arrows.addPolygon(QPolygonF({QPointF(strip.right(), y - 5), QPointF(strip.right() - 7, y), QPointF(strip.right(), y + 5)}));
    painter.fillPath(arrows, palette().color(QPalette::WindowText));
}
