#include "UI/PaintStack.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/NumberField.h"
#include "UI/PanelIcons.h"
#include "UI/ToolHeaderStyle.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QToolButton>
#include <QVBoxLayout>

namespace HexColor {
std::optional<QColor> parse(const QString &text)
{
    QString digits = text.trimmed();
    if (digits.startsWith(QLatin1Char('#')))
        digits.remove(0, 1);
    static const QRegularExpression hex(QStringLiteral("^([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$"));
    if (!hex.match(digits).hasMatch())
        return std::nullopt;
    if (digits.size() == 3)
        digits = QString(digits[0]) + digits[0] + digits[1] + digits[1] + digits[2] + digits[2];
    const QColor color = QColor::fromString(QLatin1Char('#') + digits);
    return color.isValid() ? std::optional(color) : std::nullopt;
}

QString format(const QColor &color)
{
    return color.name(QColor::HexRgb).mid(1).toUpper();
}
}

namespace {
QString entryName(bool strokes)
{
    return strokes ? QStringLiteral("stroke") : QStringLiteral("fill");
}

// The colour a row shows and edits: a solid's, or a gradient's first stop.
QColor shownColor(const Paint &paint)
{
    return paint.kind == PaintKind::none ? QColor() : paint.swatch();
}

Paint recolored(Paint paint, const QColor &color)
{
    paint.swatchId.clear();
    if (paint.stops.size() >= 2) {
        paint.stops.front().color = color;
        paint.color = color;
        return paint;
    }
    Paint solid = Paint::solid(color);
    return solid.withCompositeOf(paint);
}
}

PaintStack::PaintStack(EditorSession &session, bool strokes, FloatingPanel &picker, QWidget *parent)
    : QWidget(parent), m_session(session), m_strokes(strokes), m_picker(picker), m_rows(new QVBoxLayout)
{
    const QString prefix = entryName(strokes);
    setObjectName(prefix + QStringLiteral("Stack"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    auto *heading = new QHBoxLayout;
    heading->setSpacing(6);
    auto *caption = new QLabel(strokes ? QStringLiteral("Strokes") : QStringLiteral("Fills"), this);
    QFont font = caption->font();
    font.setPixelSize(11);
    caption->setFont(font);
    caption->setForegroundRole(QPalette::PlaceholderText);
    auto *add = new QToolButton(this);
    add->setObjectName(prefix + QStringLiteral("StackAdd"));
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    add->setFixedSize(22, 22);
    add->setToolTip(strokes ? QStringLiteral("Add stroke") : QStringLiteral("Add fill"));
    add->setAccessibleName(add->toolTip());
    connect(add, &QToolButton::clicked, this, &PaintStack::addEntry);
    heading->addWidget(caption);
    heading->addStretch(1);
    heading->addWidget(add);
    column->addLayout(heading);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(4);
    column->addLayout(m_rows);
}

std::optional<std::vector<StrokeStyle>> PaintStack::shared(const EditorSession &session, bool strokes)
{
    const auto entriesOf = [strokes](const std::vector<Paint> &fills, const std::vector<StrokeStyle> &lines) {
        if (strokes)
            return lines;
        std::vector<StrokeStyle> entries;
        for (const Paint &fill : fills) {
            StrokeStyle entry;
            entry.paint = fill;
            entries.push_back(entry);
        }
        return entries;
    };
    std::optional<std::vector<StrokeStyle>> found;
    if (session.document()) {
        for (const QUuid &id : session.selectedLeaves()) {
            const VectorObject *object = session.document()->find(id);
            if (!object || !object->hasPaint())
                continue;
            const std::vector<StrokeStyle> entries = entriesOf(object->fills(), object->strokes());
            if (!found)
                found = entries;
            else if (*found != entries)
                return std::nullopt;
        }
    }
    if (!found)
        found = entriesOf({session.defaultFill()}, {session.defaultStroke()});
    return found;
}

bool PaintStack::isStacked(const std::vector<StrokeStyle> &entries)
{
    return entries.size() > 1 || (entries.size() == 1 && !entries.front().paint.hasPlainComposite());
}

int PaintStack::activeIndex() const
{
    return std::clamp(m_active, 0, std::max(0, int(m_entries.size()) - 1));
}

void PaintStack::setActive(int index)
{
    m_active = index;
    refresh();
    emit activeChanged();
}

void PaintStack::apply(std::vector<StrokeStyle> entries, const QString &name)
{
    if (m_strokes) {
        m_session.setStrokesOfSelection(entries, name);
        return;
    }
    std::vector<Paint> fills;
    for (const StrokeStyle &entry : entries)
        fills.push_back(entry.paint);
    m_session.setFillsOfSelection(fills, name);
}

void PaintStack::addEntry()
{
    std::vector<StrokeStyle> entries = shared(m_session, m_strokes).value_or(std::vector<StrokeStyle>{});
    // A new entry copies the top one, as Figma's +; an empty stack starts black or white.
    StrokeStyle entry = entries.empty() ? StrokeStyle{} : entries.back();
    if (!entry.paint.isVisible())
        entry.paint = Paint::solid(m_strokes ? QColor(Qt::black) : QColor(Qt::white));
    entry.paint.isHidden = false;
    entries.push_back(entry);
    m_active = int(entries.size()) - 1;
    apply(entries, m_strokes ? QStringLiteral("Add Stroke") : QStringLiteral("Add Fill"));
}

void PaintStack::removeEntry(int index)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (index < 0 || index >= int(entries.size()))
        return;
    entries.erase(entries.begin() + index);
    m_active = std::min(m_active, int(entries.size()) - 1);
    apply(entries, m_strokes ? QStringLiteral("Remove Stroke") : QStringLiteral("Remove Fill"));
}

void PaintStack::moveEntry(int from, int to)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (from < 0 || from >= int(entries.size()) || to < 0 || to >= int(entries.size()) || from == to)
        return;
    const StrokeStyle moved = entries[size_t(from)];
    entries.erase(entries.begin() + from);
    entries.insert(entries.begin() + to, moved);
    if (m_active == from)
        m_active = to;
    apply(entries, m_strokes ? QStringLiteral("Reorder Strokes") : QStringLiteral("Reorder Fills"));
}

void PaintStack::setEntryHidden(int index, bool hidden)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (index < 0 || index >= int(entries.size()))
        return;
    entries[size_t(index)].paint.isHidden = hidden;
    const QString what = m_strokes ? QStringLiteral("Stroke") : QStringLiteral("Fill");
    apply(entries, (hidden ? QStringLiteral("Hide ") : QStringLiteral("Show ")) + what);
}

void PaintStack::setEntryOpacity(int index, double opacity)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (index < 0 || index >= int(entries.size()))
        return;
    entries[size_t(index)].paint.opacity = std::clamp(opacity, 0.0, 1.0);
    apply(entries, m_strokes ? QStringLiteral("Stroke Opacity") : QStringLiteral("Fill Opacity"));
}

void PaintStack::setEntryBlendMode(int index, LayerBlendMode mode)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (index < 0 || index >= int(entries.size()))
        return;
    entries[size_t(index)].paint.blendMode = mode;
    apply(entries, m_strokes ? QStringLiteral("Stroke Blending Mode") : QStringLiteral("Fill Blending Mode"));
}

void PaintStack::setEntryColor(int index, const QColor &color)
{
    std::vector<StrokeStyle> entries = m_entries;
    if (index < 0 || index >= int(entries.size()))
        return;
    entries[size_t(index)].paint = recolored(entries[size_t(index)].paint, color);
    apply(entries, m_strokes ? QStringLiteral("Stroke") : QStringLiteral("Fill"));
}

PaintStack::Row PaintStack::makeRow(int index)
{
    const QString prefix = entryName(m_strokes);
    Row parts;
    auto *row = new QWidget(this);
    parts.widget = row;
    row->setObjectName(prefix + QStringLiteral("StackRow"));
    row->setContextMenuPolicy(Qt::CustomContextMenu);
    row->setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto *grip = new QLabel(QStringLiteral("⋮⋮"), row);
    grip->setObjectName(prefix + QStringLiteral("StackGrip"));
    grip->setCursor(Qt::OpenHandCursor);
    grip->setToolTip(QStringLiteral("Drag to reorder"));
    grip->setForegroundRole(QPalette::PlaceholderText);
    grip->setFixedWidth(10);
    grip->installEventFilter(this);
    parts.grip = grip;
    parts.eye = new QToolButton(row);
    parts.eye->setObjectName(prefix + QStringLiteral("StackEye"));
    parts.eye->setAutoRaise(true);
    parts.eye->setFixedSize(22, 22);
    parts.eye->setIconSize(QSize(15, 15));
    connect(parts.eye, &QToolButton::clicked, this, [this, index] {
        if (index < int(m_entries.size()))
            setEntryHidden(index, !m_entries[size_t(index)].paint.isHidden);
    });
    parts.well = new PaintSwatch([this, index] {
        if (index >= int(m_entries.size()))
            return Paint::none();
        Paint paint = m_entries[size_t(index)].paint;
        paint.isHidden = false;
        return paint;
    }, m_strokes, row);
    parts.well->setObjectName(prefix + QStringLiteral("StackWell"));
    parts.well->setFixedSize(28, 22);
    connect(parts.well, &QAbstractButton::clicked, this, [this, index] {
        if (index >= int(m_entries.size()))
            return;
        const QColor start = shownColor(m_entries[size_t(index)].paint);
        ColorPickerSheet::showIn(m_picker, m_strokes ? QStringLiteral("Stroke Color") : QStringLiteral("Fill Color"),
                                 start.isValid() ? start : QColor(Qt::black), [this, index](const QColor &color) { setEntryColor(index, color); });
    });
    parts.hex = new QLineEdit(row);
    parts.hex->setObjectName(prefix + QStringLiteral("StackHex"));
    parts.hex->setAccessibleName(m_strokes ? QStringLiteral("Stroke hex color") : QStringLiteral("Fill hex color"));
    parts.hex->setMinimumWidth(54);
    connect(parts.hex, &QLineEdit::editingFinished, this, [this, index, field = parts.hex] {
        if (index >= int(m_entries.size()) || !field->isModified())
            return;
        field->setModified(false);
        if (const std::optional<QColor> color = HexColor::parse(field->text()))
            setEntryColor(index, *color);
        else
            refresh();
    });
    if (m_strokes) {
        parts.value = new NumberField(QString(), QStringLiteral("pt"), [this, index](double width) {
            std::vector<StrokeStyle> entries = m_entries;
            if (index >= int(entries.size()))
                return;
            entries[size_t(index)].width = std::max(0.0, width);
            apply(entries, QStringLiteral("Stroke"));
        }, row);
        parts.value->step = 0.5;
        parts.value->minimum = 0;
        parts.value->setToolTip(QStringLiteral("Stroke weight"));
    } else {
        parts.value = new NumberField(QString(), QStringLiteral("%"), [this, index](double percent) {
            setEntryOpacity(index, percent / 100);
        }, row);
        parts.value->minimum = 0;
        parts.value->maximum = 100;
        parts.value->setToolTip(QStringLiteral("Fill opacity"));
    }
    parts.value->field->setObjectName(prefix + QStringLiteral("StackValue"));
    parts.value->setFixedWidth(62);
    parts.value->gesture = [this](bool starting) {
        if (starting)
            m_session.beginEdit(m_strokes ? QStringLiteral("Stroke") : QStringLiteral("Fill Opacity"));
        else
            m_session.endEdit();
    };
    auto *remove = new QToolButton(row);
    remove->setObjectName(prefix + QStringLiteral("StackRemove"));
    remove->setText(QStringLiteral("−"));
    remove->setAutoRaise(true);
    remove->setFixedSize(22, 22);
    remove->setToolTip(m_strokes ? QStringLiteral("Remove stroke") : QStringLiteral("Remove fill"));
    remove->setAccessibleName(remove->toolTip());
    connect(remove, &QToolButton::clicked, this, [this, index] { removeEntry(index); });
    for (QWidget *control : {static_cast<QWidget *>(parts.hex), static_cast<QWidget *>(parts.value->field)}) {
        control->setFont(ToolHeaderStyle::controlFont());
        control->setFixedHeight(24);
    }
    layout->addWidget(grip);
    layout->addWidget(parts.eye);
    layout->addWidget(parts.well);
    layout->addWidget(parts.hex, 1);
    layout->addWidget(parts.value);
    layout->addWidget(remove);
    row->installEventFilter(this);
    // Right-click: blend mode and opacity, one step away.
    connect(row, &QWidget::customContextMenuRequested, this, [this, row, index](const QPoint &at) {
        if (index >= int(m_entries.size()))
            return;
        QMenu menu;
        QMenu *blend = menu.addMenu(QStringLiteral("Blending Mode"));
        for (const LayerBlendMode mode : allLayerBlendModes) {
            QAction *action = blend->addAction(rawValue(mode), this, [this, index, mode] { setEntryBlendMode(index, mode); });
            action->setCheckable(true);
            action->setChecked(m_entries[size_t(index)].paint.blendMode == mode);
        }
        if (m_strokes) {
            QMenu *opacity = menu.addMenu(QStringLiteral("Opacity"));
            for (const int percent : {100, 75, 50, 25}) {
                QAction *action = opacity->addAction(QStringLiteral("%1%").arg(percent), this, [this, index, percent] { setEntryOpacity(index, percent / 100.0); });
                action->setCheckable(true);
                action->setChecked(std::abs(m_entries[size_t(index)].paint.opacity * 100 - percent) < 0.5);
            }
        }
        menu.addSeparator();
        menu.addAction(m_strokes ? QStringLiteral("Remove Stroke") : QStringLiteral("Remove Fill"), this, [this, index] { removeEntry(index); });
        menu.exec(row->mapToGlobal(at));
    });
    return parts;
}

void PaintStack::rebuild()
{
    for (const Row &parts : m_rowParts) {
        m_rows->removeWidget(parts.widget);
        parts.widget->deleteLater();
    }
    m_rowParts.clear();
    // The top of the stack reads first, as Figma lists it.
    for (int index = int(m_entries.size()) - 1; index >= 0; --index) {
        m_rowParts.push_back(makeRow(index));
        m_rows->addWidget(m_rowParts.back().widget);
    }
}

void PaintStack::refresh()
{
    const QColor ink = palette().color(QPalette::WindowText);
    for (size_t row = 0; row < m_rowParts.size(); ++row) {
        const int index = int(m_entries.size()) - 1 - int(row);
        const Row &parts = m_rowParts[row];
        const Paint &paint = m_entries[size_t(index)].paint;
        parts.eye->setIcon(PanelIcons::pixmap(paint.isHidden ? PanelIcon::eyeSlash : PanelIcon::eye, 15, ink, devicePixelRatio()));
        parts.eye->setToolTip(paint.isHidden ? QStringLiteral("Show") : QStringLiteral("Hide"));
        parts.eye->setAccessibleName(parts.eye->toolTip());
        parts.well->update();
        if (!parts.hex->hasFocus()) {
            const QColor color = shownColor(paint);
            parts.hex->setText(color.isValid() ? HexColor::format(color) : QString());
            parts.hex->setPlaceholderText(QStringLiteral("None"));
            parts.hex->setModified(false);
        }
        if (m_strokes)
            parts.value->sync(m_entries[size_t(index)].width);
        else
            parts.value->sync(std::round(paint.opacity * 1000) / 10);
        const bool active = m_strokes && index == activeIndex() && m_entries.size() > 1;
        parts.widget->setBackgroundRole(active ? QPalette::AlternateBase : QPalette::Window);
    }
}

void PaintStack::synchronize()
{
    const std::vector<StrokeStyle> entries = shared(m_session, m_strokes).value_or(std::vector<StrokeStyle>{});
    const bool resized = entries.size() != m_entries.size();
    m_entries = entries;
    if (resized)
        rebuild();
    refresh();
}

int PaintStack::rowAt(int y) const
{
    for (size_t row = 0; row < m_rowParts.size(); ++row) {
        const QRect box = m_rowParts[row].widget->geometry();
        if (y < box.bottom() || row + 1 == m_rowParts.size())
            return int(m_entries.size()) - 1 - int(row);
    }
    return 0;
}

bool PaintStack::eventFilter(QObject *watched, QEvent *event)
{
    const auto owner = std::find_if(m_rowParts.begin(), m_rowParts.end(), [watched](const Row &parts) {
        return parts.grip == watched || parts.widget == watched;
    });
    if (owner == m_rowParts.end())
        return QWidget::eventFilter(watched, event);
    const int index = int(m_entries.size()) - 1 - int(owner - m_rowParts.begin());
    if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        if (m_strokes && index != m_active)
            setActive(index);
        if (watched == owner->grip) {
            m_dragging = index;
            owner->grip->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease && watched == owner->grip && m_dragging) {
        const int from = *std::exchange(m_dragging, std::nullopt);
        owner->grip->setCursor(Qt::OpenHandCursor);
        const QPoint at = owner->grip->mapTo(this, static_cast<QMouseEvent *>(event)->position().toPoint());
        moveEntry(from, rowAt(at.y()));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

SelectionColors::SelectionColors(EditorSession &session, FloatingPanel &picker, QWidget *parent)
    : QWidget(parent), m_session(session), m_picker(picker), m_chips(new QWidget(this))
{
    setObjectName(QStringLiteral("selectionColors"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 4, 0, 0);
    column->setSpacing(4);
    auto *caption = new QLabel(QStringLiteral("Selection colors"), this);
    QFont font = caption->font();
    font.setPixelSize(11);
    caption->setFont(font);
    caption->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(caption);
    auto *grid = new QGridLayout(m_chips);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    column->addWidget(m_chips);
}

bool SelectionColors::isWanted() const
{
    if (!m_session.document())
        return false;
    int painted = 0;
    for (const QUuid &id : m_session.selectedLeaves()) {
        const VectorObject *object = m_session.document()->find(id);
        painted += object && object->hasPaint() ? 1 : 0;
    }
    return painted >= 2;
}

void SelectionColors::synchronize()
{
    const std::vector<QColor> colors = isWanted() ? m_session.selectionColors() : std::vector<QColor>{};
    setVisible(!colors.empty());
    if (colors == m_colors)
        return;
    m_colors = colors;
    auto *grid = static_cast<QGridLayout *>(m_chips->layout());
    while (QLayoutItem *item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    constexpr int perRow = 8;
    for (size_t index = 0; index < m_colors.size(); ++index) {
        const QColor color = m_colors[index];
        auto *chip = new PaintSwatch([color] { return Paint::solid(color); }, false, m_chips);
        chip->setObjectName(QStringLiteral("selectionColor"));
        chip->setFixedSize(22, 22);
        chip->setToolTip(QStringLiteral("#%1: click to change every use").arg(HexColor::format(color)));
        chip->setAccessibleName(QStringLiteral("Selection color #%1").arg(HexColor::format(color)));
        connect(chip, &QAbstractButton::clicked, this, [this, color] {
            ColorPickerSheet::showIn(m_picker, QStringLiteral("Selection Color"), color,
                                     [this, color](const QColor &to) { m_session.replaceColor(color, to); });
        });
        grid->addWidget(chip, int(index) / perRow, int(index) % perRow);
    }
}
