#include "UI/FramePresets.h"
#include "UI/NumberField.h"
#include "UI/ToolHeaderStyle.h"
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QApplication>
#include <QPainter>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyledItemDelegate>
#include <algorithm>
#include <cmath>

namespace {
constexpr int nameRole = Qt::UserRole, sizeRole = Qt::UserRole + 1, builtInRole = Qt::UserRole + 2;
constexpr const char *savedGroup = "Saved";
constexpr const char *groupKey = "properties/framePresetGroup";

FramePresetsSection::Namer &customNamer()
{
    static FramePresetsSection::Namer namer;
    return namer;
}

QString sizeText(QSizeF size)
{
    return QStringLiteral("%1 × %2").arg(NumberField::formatted(size.width()), NumberField::formatted(size.height()));
}

// The name at the left, the size quietly at the right.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem plain = option;
        initStyleOption(&plain, index);
        plain.text.clear();
        const QWidget *widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &plain, painter, widget);
        const QRect area = option.rect.adjusted(8, 0, -8, 0);
        const QColor ink = option.palette.color(option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::WindowText);
        painter->save();
        painter->setFont(option.font);
        painter->setPen(ink);
        painter->drawText(area, Qt::AlignVCenter | Qt::AlignLeft, painter->fontMetrics().elidedText(index.data(nameRole).toString(), Qt::ElideRight, area.width() * 6 / 10));
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->drawText(area, Qt::AlignVCenter | Qt::AlignRight, sizeText(index.data(sizeRole).toSizeF()));
        painter->restore();
    }
};
}

const std::vector<FramePresets::Preset> &FramePresets::builtIn()
{
    static const std::vector<Preset> all{
        {"Phone", "iPhone 16", QSizeF(393, 852)},
        {"Phone", "iPhone 16 Plus", QSizeF(430, 932)},
        {"Phone", "iPhone 16 Pro", QSizeF(402, 874)},
        {"Phone", "iPhone 16 Pro Max", QSizeF(440, 956)},
        {"Phone", "iPhone 8", QSizeF(375, 667)},
        {"Phone", "Android Compact", QSizeF(412, 917)},
        {"Tablet", "iPad mini 8.3\"", QSizeF(744, 1133)},
        {"Tablet", "iPad 10.9\"", QSizeF(820, 1180)},
        {"Tablet", "iPad Pro 11\"", QSizeF(834, 1194)},
        {"Tablet", "iPad Pro 12.9\"", QSizeF(1024, 1366)},
        {"Tablet", "Android Medium", QSizeF(700, 840)},
        {"Desktop", "Desktop", QSizeF(1440, 1024)},
        {"Desktop", "Full HD", QSizeF(1920, 1080)},
        {"Desktop", "Laptop", QSizeF(1366, 768)},
        {"Desktop", "MacBook Air", QSizeF(1280, 832)},
        {"Desktop", "MacBook Pro 14\"", QSizeF(1512, 982)},
        {"Desktop", "MacBook Pro 16\"", QSizeF(1728, 1117)},
        {"Social", "Instagram post", QSizeF(1080, 1080)},
        {"Social", "Instagram story", QSizeF(1080, 1920)},
        {"Social", "X post", QSizeF(1600, 900)},
        {"Social", "Facebook post", QSizeF(1200, 630)},
        {"Social", "LinkedIn banner", QSizeF(1584, 396)},
        {"Social", "YouTube thumbnail", QSizeF(1280, 720)},
        {"Paper", "A4", QSizeF(595, 842)},
        {"Paper", "A5", QSizeF(420, 595)},
        {"Paper", "A6", QSizeF(297, 420)},
        {"Paper", "Letter", QSizeF(612, 792)},
        {"Paper", "Tabloid", QSizeF(792, 1224)},
    };
    return all;
}

FramePresetsSection::FramePresetsSection(EditorSession &session, QWidget *parent)
    : PanelSection(QStringLiteral("Frame"), QStringLiteral("framePresets"), parent), m_session(session), m_group(new QComboBox(this)), m_list(new QListWidget(this)),
      m_options(new QToolButton(this)), m_note(new QLabel(this))
{
    m_group->setObjectName(QStringLiteral("framePresetGroup"));
    m_group->setAccessibleName(QStringLiteral("Frame preset group"));
    m_list->setObjectName(QStringLiteral("framePresetList"));
    m_list->setAccessibleName(QStringLiteral("Frame presets"));
    m_list->setFont(ToolHeaderStyle::controlFont());
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->installEventFilter(this);
    m_note->setObjectName(QStringLiteral("framePresetNote"));
    m_note->setForegroundRole(QPalette::PlaceholderText);
    m_note->setWordWrap(true);
    m_note->setFont(ToolHeaderStyle::controlFont());
    m_options->setObjectName(QStringLiteral("framePresetMenu"));
    m_options->setText(QStringLiteral("⋯"));
    m_options->setToolTip(QStringLiteral("Save the selected frame's size, or show hidden presets"));
    m_options->setAccessibleName(QStringLiteral("Frame preset options"));
    m_options->setAutoRaise(true);
    m_options->setPopupMode(QToolButton::InstantPopup);
    m_options->setFixedSize(NumberField::fieldHeight, NumberField::fieldHeight);
    auto *menu = new QMenu(m_options);
    m_save = menu->addAction(QStringLiteral("Save Selected Frame as Preset…"), this, [this] { saveSelectedFrame(); });
    m_save->setObjectName(QStringLiteral("saveFramePreset"));
    m_showHidden = menu->addAction(QStringLiteral("Show Hidden Presets"), this, [this] { showHidden(); });
    m_showHidden->setObjectName(QStringLiteral("showHiddenFramePresets"));
    m_options->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [this] { updateMenu(); });
    trailing->addWidget(m_options);
    body->addWidget(m_group);
    body->addWidget(m_list);
    body->addWidget(m_note);
    connect(m_group, &QComboBox::activated, this, [this] {
        QSettings().setValue(QLatin1String(groupKey), m_group->currentText());
        fill();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { drop(item->data(nameRole).toString(), item->data(sizeRole).toSizeF()); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](QPoint at) { rowMenu(m_list->itemAt(at), m_list->viewport()->mapToGlobal(at)); });
    connect(&m_session, &EditorSession::changed, this, [this] {
        if (isVisible())
            updateMenu();
    });
    summary = [this] { return m_group->currentText(); };
    // The file is read when the section first shows, so a panel that never shows it never touches the config.
    fill();
}

void FramePresetsSection::setNamer(Namer namer)
{
    customNamer() = std::move(namer);
}

void FramePresetsSection::reload()
{
    m_stored = PresetStore::read(PresetStore::frames);
    m_loaded = true;
    fill();
}

void FramePresetsSection::showEvent(QShowEvent *event)
{
    PanelSection::showEvent(event);
    // Another window may have saved or hidden presets since this one was built.
    const PresetStore::Section onDisk = PresetStore::read(PresetStore::frames);
    const auto same = [](const PresetStore::Entry &a, const PresetStore::Entry &b) { return a.name == b.name && a.points == b.points; };
    if (!m_loaded || onDisk.hidden != m_stored.hidden || !std::ranges::equal(onDisk.saved, m_stored.saved, same)) {
        m_stored = onDisk;
        fill();
    }
    m_loaded = true;
    updateMenu();
}

// Return or Space on a row picks it, as a click does.
bool FramePresetsSection::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && m_list->currentItem()) {
            drop(m_list->currentItem()->data(nameRole).toString(), m_list->currentItem()->data(sizeRole).toSizeF());
            return true;
        }
    }
    return PanelSection::eventFilter(watched, event);
}

// The groups that have something in them, then the one the user last looked at.
void FramePresetsSection::fill(const QString &prefer)
{
    const QString wanted = !prefer.isEmpty() ? prefer : m_group->currentText().isEmpty() ? QSettings().value(QLatin1String(groupKey)).toString() : m_group->currentText();
    m_rows.clear();
    for (const PresetStore::Entry &entry : m_stored.saved)
        m_rows.push_back({QLatin1String(savedGroup), entry.name, entry.points, false});
    for (const FramePresets::Preset &preset : FramePresets::builtIn())
        if (!m_stored.hidden.contains(QString::fromUtf8(preset.name)))
            m_rows.push_back({QString::fromUtf8(preset.group), QString::fromUtf8(preset.name), preset.size, true});
    QStringList groups;
    for (const Row &row : m_rows)
        if (!groups.contains(row.group))
            groups << row.group;
    {
        const QSignalBlocker quiet(m_group);
        m_group->clear();
        m_group->addItems(groups);
        const int at = groups.indexOf(wanted);
        m_group->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_list->clear();
    for (const Row &row : m_rows) {
        if (row.group != m_group->currentText())
            continue;
        auto *item = new QListWidgetItem(row.name, m_list);
        item->setData(nameRole, row.name);
        item->setData(sizeRole, row.size);
        item->setData(builtInRole, row.builtIn);
        item->setToolTip(QStringLiteral("%1 · %2 pt").arg(row.name, sizeText(row.size)));
        item->setSizeHint(QSize(0, NumberField::fieldHeight));
    }
    m_list->setFixedHeight(m_list->count() * NumberField::fieldHeight + 4);
    m_group->setVisible(m_group->count() > 0);
    m_list->setVisible(m_list->count() > 0);
    if (groups.isEmpty())
        m_note->setText(QStringLiteral("Every preset is hidden. Use ⋯ to show them again."));
    m_note->setVisible(!m_note->text().isEmpty());
    updateMenu();
}

void FramePresetsSection::updateMenu()
{
    const std::vector<QUuid> frames = m_session.selectedFrames();
    m_save->setEnabled(frames.size() == 1 && m_session.selection().size() == 1);
    m_showHidden->setEnabled(!m_stored.hidden.isEmpty());
}

void FramePresetsSection::drop(const QString &name, QSizeF size)
{
    if (!m_session.hasDocument() || size.isEmpty())
        return;
    m_session.addFrame(m_session.framePlacement(size), name);
    m_session.selectTool(Tool::select);
}

QString FramePresetsSection::nameProblem(const QString &name, const QString &ignoring) const
{
    for (const FramePresets::Preset &preset : FramePresets::builtIn())
        if (name.compare(QString::fromUtf8(preset.name), Qt::CaseInsensitive) == 0)
            return QStringLiteral("“%1” is the name of a built-in preset.").arg(name);
    for (const PresetStore::Entry &entry : m_stored.saved)
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0 && entry.name.compare(ignoring, Qt::CaseInsensitive) != 0)
            return QStringLiteral("There is already a preset called “%1”.").arg(name);
    return {};
}

std::optional<QString> FramePresetsSection::askName(const QString &title, const QString &initial)
{
    if (customNamer())
        return customNamer()(this, title, initial);
    bool accepted = false;
    const QString name = QInputDialog::getText(window(), title, QStringLiteral("Name"), QLineEdit::Normal, initial, &accepted);
    return accepted ? std::optional<QString>(name) : std::nullopt;
}

// The new list stays for this session even when it can't be written, and the note says why.
void FramePresetsSection::store(const PresetStore::Section &section, const QString &prefer)
{
    m_stored = section;
    m_note->setText(PresetStore::write(PresetStore::frames, section));
    fill(prefer);
}

void FramePresetsSection::saveSelectedFrame()
{
    const std::vector<QUuid> frames = m_session.selectedFrames();
    if (frames.size() != 1 || m_session.selection().size() != 1)
        return;
    const QRectF box = m_session.document()->bounds(frames.front());
    const QSizeF size(std::round(box.width()), std::round(box.height()));
    if (size.width() < 1 || size.height() < 1 || size.width() > PresetStore::maximumPoints || size.height() > PresetStore::maximumPoints) {
        m_note->setText(QStringLiteral("Frames from 1 to %1 can be saved.").arg(PresetStore::limitDescription()));
        m_note->show();
        return;
    }
    const std::optional<QString> asked = askName(QStringLiteral("Save Preset"), QString());
    const QString name = asked ? asked->trimmed() : QString();
    if (name.isEmpty())
        return;
    m_stored = PresetStore::read(PresetStore::frames);
    if (const QString problem = nameProblem(name, name); !problem.isEmpty()) {
        m_note->setText(problem);
        m_note->show();
        return;
    }
    PresetStore::Section next = m_stored;
    std::erase_if(next.saved, [&](const PresetStore::Entry &entry) { return entry.name.compare(name, Qt::CaseInsensitive) == 0; });
    next.saved.insert(next.saved.begin(), {name, size, LengthUnit::px});
    store(next, QLatin1String(savedGroup));
}

void FramePresetsSection::renameSaved(const QString &old)
{
    m_stored = PresetStore::read(PresetStore::frames);
    const auto has = std::ranges::any_of(m_stored.saved, [&](const PresetStore::Entry &entry) { return entry.name == old; });
    if (!has)
        return;
    const std::optional<QString> asked = askName(QStringLiteral("Rename Preset"), old);
    const QString name = asked ? asked->trimmed() : QString();
    if (name.isEmpty() || name == old)
        return;
    if (const QString problem = nameProblem(name, old); !problem.isEmpty()) {
        m_note->setText(problem);
        m_note->show();
        return;
    }
    PresetStore::Section next = m_stored;
    for (PresetStore::Entry &entry : next.saved)
        if (entry.name == old)
            entry.name = name;
    store(next, QLatin1String(savedGroup));
}

void FramePresetsSection::deleteSaved(const QString &name)
{
    PresetStore::Section next = PresetStore::read(PresetStore::frames);
    std::erase_if(next.saved, [&](const PresetStore::Entry &entry) { return entry.name == name; });
    store(next, next.saved.empty() ? QString() : QLatin1String(savedGroup));
}

void FramePresetsSection::hideBuiltIn(const QString &name)
{
    PresetStore::Section next = PresetStore::read(PresetStore::frames);
    if (!next.hidden.contains(name))
        next.hidden << name;
    store(next, QString());
}

void FramePresetsSection::showHidden()
{
    PresetStore::Section next = PresetStore::read(PresetStore::frames);
    next.hidden.clear();
    store(next, QString());
}

void FramePresetsSection::rowMenu(QListWidgetItem *item, QPoint at)
{
    if (!item)
        return;
    const QString name = item->data(nameRole).toString();
    QMenu menu(this);
    if (item->data(builtInRole).toBool()) {
        menu.addAction(QStringLiteral("Hide Preset"), this, [this, name] { hideBuiltIn(name); });
    } else {
        menu.addAction(QStringLiteral("Rename Preset…"), this, [this, name] { renameSaved(name); });
        menu.addAction(QStringLiteral("Delete Preset"), this, [this, name] { deleteSaved(name); });
    }
    menu.exec(at);
}
