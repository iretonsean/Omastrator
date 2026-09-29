#include "UI/NewDocumentSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspace.h"
#include <QFileInfo>
#include <algorithm>
#include <QGridLayout>
#include <QInputDialog>
#include <QMenu>
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

NewDocumentSheet::Namer &customNamer()
{
    static NewDocumentSheet::Namer namer;
    return namer;
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
    if (!number || !std::isfinite(value) || value < 1 || value > PresetStore::maximumPoints)
        return std::nullopt;
    return value;
}

NewDocumentSheet::NewDocumentSheet(std::function<void(QSizeF)> onCreate, std::function<void()> onOpen,
                                   std::function<void(const QString &)> onOpenRecent, QWidget *parent, std::function<void()> onCloud)
    : QWidget(parent), m_onCreate(std::move(onCreate)), m_preset(new QComboBox(this)), m_presetMenu(new QToolButton(this)), m_stored(PresetStore::read(PresetStore::documents)), m_width(new QLineEdit(this)), m_height(new QLineEdit(this)),
      m_unit(new QComboBox(this)), m_note(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_create(new QPushButton(QStringLiteral("Create"), this))
{
    setObjectName(QStringLiteral("newDocumentSheet"));
    setFixedWidth(500);
    m_preset->setObjectName(QStringLiteral("presetInput"));
    m_presetMenu->setObjectName(QStringLiteral("presetMenu"));
    m_presetMenu->setText(QStringLiteral("⋯"));
    m_presetMenu->setToolTip(QStringLiteral("Save, rename, delete or hide presets"));
    m_presetMenu->setAccessibleName(QStringLiteral("Preset options"));
    m_presetMenu->setPopupMode(QToolButton::InstantPopup);
    m_presetMenu->setFixedSize(28, 28);
    auto *menu = new QMenu(m_presetMenu);
    const auto action = [&](const QString &label, const QString &name, void (NewDocumentSheet::*run)()) {
        QAction *made = menu->addAction(label);
        made->setObjectName(name);
        connect(made, &QAction::triggered, this, run);
        return made;
    };
    m_save = action(QStringLiteral("Save Preset…"), QStringLiteral("savePreset"), &NewDocumentSheet::savePreset);
    m_rename = action(QStringLiteral("Rename Preset…"), QStringLiteral("renamePreset"), &NewDocumentSheet::renamePreset);
    m_delete = action(QStringLiteral("Delete Preset"), QStringLiteral("deletePreset"), &NewDocumentSheet::deletePreset);
    m_hide = action(QStringLiteral("Hide Preset"), QStringLiteral("hidePreset"), &NewDocumentSheet::hidePreset);
    menu->addSeparator();
    m_showHidden = action(QStringLiteral("Show Hidden Presets"), QStringLiteral("showHiddenPresets"), &NewDocumentSheet::showHiddenPresets);
    m_presetMenu->setMenu(menu);
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
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(4);
    presetRow->addWidget(m_preset, 1);
    presetRow->addWidget(m_presetMenu);
    fields->addLayout(presetRow, 1, 0);
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
    if (onCloud) {
        auto *cloud = new QPushButton(QStringLiteral("Cloud Storage…"), this);
        cloud->setObjectName(QStringLiteral("cloudStorage"));
        connect(cloud, &QPushButton::clicked, this, [onCloud = std::move(onCloud)] { onCloud(); });
        buttons->addWidget(cloud);
    }
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
            auto *button = new QPushButton(ProjectWorkspace::recentIcon(path), ProjectWorkspace::recentLabel(path), list);
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
        connect(field, &QLineEdit::textEdited, this, [this] { m_preset->setCurrentIndex(customIndex()); });
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
    fillPresets(QString());
}

LengthUnit NewDocumentSheet::unit() const
{
    return LengthUnit(m_unit->currentIndex());
}

void NewDocumentSheet::setNamer(Namer namer)
{
    customNamer() = std::move(namer);
}

// Saved presets first, then the built-ins that aren't hidden. Selects `select`, else the first built-in.
void NewDocumentSheet::fillPresets(const QString &select)
{
    m_choices.clear();
    for (const PresetStore::Entry &entry : m_stored.saved)
        m_choices.push_back({entry, false});
    for (const Preset &preset : presets)
        if (!m_stored.hidden.contains(QString::fromUtf8(preset.name)))
            m_choices.push_back({{QString::fromUtf8(preset.name), preset.points, preset.unit}, true});
    m_preset->clear();
    int index = -1, firstBuiltIn = -1;
    for (size_t i = 0; i < m_choices.size(); ++i) {
        m_preset->addItem(m_choices[i].entry.name);
        if (m_choices[i].entry.name == select)
            index = int(i);
        if (m_choices[i].builtIn && firstBuiltIn < 0)
            firstBuiltIn = int(i);
    }
    m_preset->addItem(QStringLiteral("Custom"));
    if (select == QLatin1String("Custom"))
        index = customIndex();
    if (index < 0)
        index = firstBuiltIn >= 0 ? firstBuiltIn : m_choices.empty() ? customIndex() : 0;
    // Every preset gone: leave the size that's there, or start from Letter.
    if (index == customIndex() && m_width->text().isEmpty()) {
        m_unit->setCurrentIndex(int(presets[0].unit));
        m_shownUnit = presets[0].unit;
        m_width->setText(shown(presets[0].points.width(), presets[0].unit));
        m_height->setText(shown(presets[0].points.height(), presets[0].unit));
    }
    choosePreset(index);
}

const NewDocumentSheet::Choice *NewDocumentSheet::chosen() const
{
    const int index = m_preset->currentIndex();
    return index >= 0 && index < customIndex() ? &m_choices[size_t(index)] : nullptr;
}

QString NewDocumentSheet::nameProblem(const QString &name, const QString &ignoring) const
{
    bool reserved = name.compare(QStringLiteral("Custom"), Qt::CaseInsensitive) == 0;
    for (const Preset &preset : presets)
        reserved = reserved || name.compare(QString::fromUtf8(preset.name), Qt::CaseInsensitive) == 0;
    if (reserved)
        return QStringLiteral("“%1” is the name of a built-in preset.").arg(name);
    for (const PresetStore::Entry &entry : m_stored.saved)
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0 && entry.name.compare(ignoring, Qt::CaseInsensitive) != 0)
            return QStringLiteral("There is already a preset called “%1”.").arg(name);
    return {};
}

std::optional<QString> NewDocumentSheet::askName(const QString &title, const QString &initial)
{
    if (customNamer())
        return customNamer()(this, title, initial);
    bool accepted = false;
    const QString name = QInputDialog::getText(this, title, QStringLiteral("Name"), QLineEdit::Normal, initial, &accepted);
    return accepted ? std::optional<QString>(name) : std::nullopt;
}

// Every tab's welcome sheet lives on, so an action starts from what is on disk, not from
// what this sheet read when it was built; otherwise it would drop presets saved elsewhere.
PresetStore::Section NewDocumentSheet::reload()
{
    m_stored = PresetStore::read(PresetStore::documents);
    return m_stored;
}

// Keeps the new list for this session even when it can't be written, and says so.
void NewDocumentSheet::store(const PresetStore::Section &section, const QString &select)
{
    m_stored = section;
    const QString problem = PresetStore::write(PresetStore::documents, section);
    fillPresets(select);
    if (!problem.isEmpty()) {
        m_note->setText(problem);
        m_note->setForegroundRole(QPalette::BrightText);
    }
}

void NewDocumentSheet::savePreset()
{
    const std::optional<double> width = dimension(m_width->text(), unit()), height = dimension(m_height->text(), unit());
    if (!width || !height)
        return;
    const std::optional<QString> asked = askName(QStringLiteral("Save Preset"), QString());
    const QString name = asked ? asked->trimmed() : QString();
    if (name.isEmpty())
        return;
    reload();
    if (const QString problem = nameProblem(name, name); !problem.isEmpty()) {
        m_note->setText(problem);
        m_note->setForegroundRole(QPalette::BrightText);
        return;
    }
    PresetStore::Section next = reload();
    std::erase_if(next.saved, [&](const PresetStore::Entry &entry) { return entry.name.compare(name, Qt::CaseInsensitive) == 0; });
    next.saved.insert(next.saved.begin(), {name, QSizeF(*width, *height), unit()});
    store(next, name);
}

void NewDocumentSheet::renamePreset()
{
    const Choice *current = chosen();
    if (!current || current->builtIn)
        return;
    const QString old = current->entry.name;
    const std::optional<QString> asked = askName(QStringLiteral("Rename Preset"), old);
    const QString name = asked ? asked->trimmed() : QString();
    if (name.isEmpty() || name == old)
        return;
    reload();
    if (const QString problem = nameProblem(name, old); !problem.isEmpty()) {
        m_note->setText(problem);
        m_note->setForegroundRole(QPalette::BrightText);
        return;
    }
    PresetStore::Section next = reload();
    for (PresetStore::Entry &entry : next.saved)
        if (entry.name == old)
            entry.name = name;
    store(next, name);
}

void NewDocumentSheet::deletePreset()
{
    const Choice *current = chosen();
    if (!current || current->builtIn)
        return;
    PresetStore::Section next = reload();
    const QString name = current->entry.name;
    std::erase_if(next.saved, [&](const PresetStore::Entry &entry) { return entry.name == name; });
    store(next, QString());
}

void NewDocumentSheet::hidePreset()
{
    const Choice *current = chosen();
    if (!current || !current->builtIn)
        return;
    PresetStore::Section next = reload();
    if (!next.hidden.contains(current->entry.name))
        next.hidden << current->entry.name;
    store(next, QString());
}

void NewDocumentSheet::showHiddenPresets()
{
    PresetStore::Section next = reload();
    next.hidden.clear();
    store(next, m_preset->currentText());
}

void NewDocumentSheet::updatePresetMenu()
{
    const Choice *current = chosen();
    m_save->setEnabled(dimension(m_width->text(), unit()) && dimension(m_height->text(), unit()));
    m_rename->setEnabled(current && !current->builtIn);
    m_delete->setEnabled(current && !current->builtIn);
    m_hide->setEnabled(current && current->builtIn);
    m_showHidden->setEnabled(!m_stored.hidden.isEmpty());
}

void NewDocumentSheet::choosePreset(int index)
{
    m_preset->setCurrentIndex(index);
    if (index >= customIndex()) {
        validate();
        return;
    }
    const PresetStore::Entry &entry = m_choices[size_t(index)].entry;
    m_unit->setCurrentIndex(int(entry.unit));
    m_shownUnit = entry.unit;
    m_width->setText(shown(entry.points.width(), entry.unit));
    m_height->setText(shown(entry.points.height(), entry.unit));
    validate();
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
    updatePresetMenu();
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
    const PresetStore::Section onDisk = PresetStore::read(PresetStore::documents);
    const auto same = [](const PresetStore::Entry &a, const PresetStore::Entry &b) { return a.name == b.name && a.points == b.points && a.unit == b.unit; };
    if (onDisk.hidden != m_stored.hidden || !std::ranges::equal(onDisk.saved, m_stored.saved, same)) {
        m_stored = onDisk;
        fillPresets(m_preset->currentText());
    }
    m_width->setFocus();
}
