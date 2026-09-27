#include "UI/NewDocumentSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspace.h"
#include <QFileInfo>
#include <QGridLayout>
#include <QShortcut>
#include <QVBoxLayout>
#include <cmath>

const std::array<NewDocumentSheet::Preset, 5> NewDocumentSheet::presets{{
    {"Letter", QSizeF(612, 792), LengthUnit::in},
    {"A4", QSizeF(595.2756, 841.8898), LengthUnit::mm},
    {"A3", QSizeF(841.8898, 1190.5512), LengthUnit::mm},
    {"1920 × 1080", QSizeF(1920, 1080), LengthUnit::px},
    {"1080 × 1080", QSizeF(1080, 1080), LengthUnit::px},
}};

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}

QString shown(double points, LengthUnit unit)
{
    const double value = points / NewDocumentSheet::pointsPer(unit);
    return QString::number(std::round(value * 100) / 100, 'g', 8);
}
}

double NewDocumentSheet::pointsPer(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::pt:
    case LengthUnit::px: return 1;
    case LengthUnit::in: return 72;
    case LengthUnit::mm: return 72 / 25.4;
    }
    return 1;
}

std::optional<double> NewDocumentSheet::dimension(const QString &text, LengthUnit unit)
{
    bool number = false;
    const double value = text.trimmed().toDouble(&number) * pointsPer(unit);
    if (!number || !std::isfinite(value) || value < 1 || value > maximumPoints)
        return std::nullopt;
    return value;
}

NewDocumentSheet::NewDocumentSheet(std::function<void(QSizeF)> onCreate, std::function<void()> onOpen,
                                   std::function<void(const QString &)> onOpenRecent, QWidget *parent)
    : QWidget(parent), m_onCreate(std::move(onCreate)), m_preset(new QComboBox(this)), m_width(new QLineEdit(this)), m_height(new QLineEdit(this)),
      m_unit(new QComboBox(this)), m_note(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_create(new QPushButton(QStringLiteral("Create"), this))
{
    setObjectName(QStringLiteral("newDocumentSheet"));
    setFixedWidth(500);
    m_preset->setObjectName(QStringLiteral("presetInput"));
    for (const Preset &preset : presets)
        m_preset->addItem(QString::fromUtf8(preset.name));
    m_preset->addItem(QStringLiteral("Custom"));
    m_width->setObjectName(QStringLiteral("widthInput"));
    m_height->setObjectName(QStringLiteral("heightInput"));
    m_unit->setObjectName(QStringLiteral("unitInput"));
    m_unit->addItems({QStringLiteral("pt"), QStringLiteral("px"), QStringLiteral("in"), QStringLiteral("mm")});
    m_note->setObjectName(QStringLiteral("documentNote"));
    m_create->setObjectName(QStringLiteral("createDocument"));
    m_create->setDefault(true);
    NativeShortcut::bind(*this, m_create, nullptr);
    auto *open = new QPushButton(QStringLiteral("Open…"), this);
    open->setObjectName(QStringLiteral("openDocument"));

    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(8);
    fields->addWidget(text(QStringLiteral("Preset"), 12, QFont::Medium, QPalette::WindowText, this), 0, 0);
    fields->addWidget(m_preset, 1, 0);
    fields->addWidget(text(QStringLiteral("Width"), 12, QFont::Medium, QPalette::WindowText, this), 0, 1);
    fields->addWidget(m_width, 1, 1);
    fields->addWidget(text(QStringLiteral("×"), 14, QFont::Normal, QPalette::PlaceholderText, this), 1, 2);
    fields->addWidget(text(QStringLiteral("Height"), 12, QFont::Medium, QPalette::WindowText, this), 0, 3);
    fields->addWidget(m_height, 1, 3);
    fields->addWidget(text(QStringLiteral("Units"), 12, QFont::Medium, QPalette::WindowText, this), 0, 4);
    fields->addWidget(m_unit, 1, 4);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(open);
    buttons->addStretch(1);
    buttons->addWidget(m_create);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(28, 28, 28, 28);
    column->setSpacing(20);
    auto *heading = new QVBoxLayout;
    heading->setSpacing(6);
    heading->addWidget(text(QStringLiteral("New document"), 17, QFont::DemiBold, QPalette::WindowText, this));
    heading->addWidget(text(QStringLiteral("An empty artboard for your next illustration."), 13, QFont::Normal, QPalette::PlaceholderText, this));
    column->addLayout(heading);
    column->addLayout(fields);
    column->addWidget(m_note);
    column->addLayout(buttons);
    // Recent files, newest first, when there are any.
    const QStringList recent = ProjectWorkspace::recentFiles();
    if (!recent.isEmpty()) {
        auto *list = new QWidget(this);
        list->setObjectName(QStringLiteral("recentFiles"));
        auto *rows = new QVBoxLayout(list);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(2);
        rows->addWidget(text(QStringLiteral("Recent"), 12, QFont::Medium, QPalette::WindowText, list));
        for (const QString &path : recent.mid(0, 6)) {
            auto *button = new QPushButton(QFileInfo(path).fileName(), list);
            button->setFlat(true);
            button->setToolTip(path);
            button->setStyleSheet(QStringLiteral("text-align: left"));
            connect(button, &QPushButton::clicked, this, [onOpenRecent, path] { onOpenRecent(path); });
            rows->addWidget(button);
        }
        column->addWidget(list);
    }

    connect(m_preset, &QComboBox::activated, this, &NewDocumentSheet::choosePreset);
    connect(m_unit, &QComboBox::activated, this, &NewDocumentSheet::changeUnit);
    for (QLineEdit *field : {m_width, m_height}) {
        connect(field, &QLineEdit::textEdited, this, [this] { m_preset->setCurrentIndex(int(presets.size())); });
        connect(field, &QLineEdit::textChanged, this, &NewDocumentSheet::validate);
    }
    // Return anywhere on the sheet creates, unless remapped.
    std::vector<QShortcut *> returns;
    for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Enter})
        returns.push_back(new QShortcut(QKeySequence(key), this, this, &NewDocumentSheet::create, Qt::WidgetWithChildrenShortcut));
    const auto follow = [returns] {
        for (QShortcut *shortcut : returns)
            shortcut->setEnabled(ShortcutSettings::shared().native(ShortcutChord(QStringLiteral("\r"))) == ShortcutChord(QStringLiteral("\r")));
    };
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, follow);
    follow();
    connect(m_create, &QPushButton::clicked, this, &NewDocumentSheet::create);
    connect(open, &QPushButton::clicked, this, [onOpen = std::move(onOpen)] { onOpen(); });
    choosePreset(0);
}

LengthUnit NewDocumentSheet::unit() const
{
    return LengthUnit(m_unit->currentIndex());
}

void NewDocumentSheet::choosePreset(int index)
{
    m_preset->setCurrentIndex(index);
    if (index >= int(presets.size())) {
        validate();
        return;
    }
    const Preset &preset = presets.at(size_t(index));
    m_unit->setCurrentIndex(int(preset.unit));
    m_shownUnit = preset.unit;
    m_width->setText(shown(preset.points.width(), preset.unit));
    m_height->setText(shown(preset.points.height(), preset.unit));
}

// The same lengths, shown in the new unit.
void NewDocumentSheet::changeUnit(int index)
{
    const LengthUnit next = LengthUnit(index);
    for (QLineEdit *field : {m_width, m_height}) {
        if (const std::optional<double> points = dimension(field->text(), m_shownUnit))
            field->setText(shown(*points, next));
    }
    m_shownUnit = next;
    validate();
}

void NewDocumentSheet::validate()
{
    const std::optional<double> width = dimension(m_width->text(), unit()), height = dimension(m_height->text(), unit());
    const bool valid = width && height;
    m_note->setText(valid ? QStringLiteral("%1 × %2 pt · White artboard · sRGB").arg(shown(*width, LengthUnit::pt), shown(*height, LengthUnit::pt))
                          : QStringLiteral("Enter sizes from 1 to 16,384 points."));
    m_note->setForegroundRole(valid ? QPalette::PlaceholderText : QPalette::BrightText);
    m_create->setEnabled(valid);
}

void NewDocumentSheet::create()
{
    const std::optional<double> width = dimension(m_width->text(), unit()), height = dimension(m_height->text(), unit());
    if (width && height)
        m_onCreate(QSizeF(*width, *height));
}

void NewDocumentSheet::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_width->setFocus();
}
