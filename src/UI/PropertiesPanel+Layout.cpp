#include "UI/KeyboardShortcuts.h"
#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStandardItemModel>
#include <QToolButton>

// Frames and auto layout (docs/AUTO-LAYOUT.md), laid out as Figma's panel is: the flow, gap and
// padding, the alignment grid, then how the selection sizes itself.

namespace {
QComboBox *sizingMenu(const QString &name, const QString &axis, QWidget *parent)
{
    auto *menu = new QComboBox(parent);
    menu->setObjectName(name);
    menu->setAccessibleName(axis + QStringLiteral(" resizing"));
    menu->setToolTip(QStringLiteral("%1: Fixed keeps it, Hug takes the content's, Fill takes the room the auto-layout frame leaves").arg(axis));
    menu->addItem(QStringLiteral("Fixed"), int(LayoutSizing::fixed));
    menu->addItem(QStringLiteral("Hug"), int(LayoutSizing::hug));
    menu->addItem(QStringLiteral("Fill"), int(LayoutSizing::fill));
    return menu;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

void PropertiesPanel::changeLayout(const QString &editName, const std::function<void(AutoLayout &)> &change)
{
    const std::vector<QUuid> frames = m_session.selectedFrames();
    if (frames.empty())
        return;
    AutoLayout layout = m_session.document()->find(frames.front())->autoLayout.value_or(AutoLayout{});
    change(layout);
    m_session.setAutoLayout(layout, editName);
}

PanelSection *PropertiesPanel::layoutSection()
{
    m_layout = new PanelSection(QStringLiteral("Layout"), QStringLiteral("layout"), this);
    QVBoxLayout *body = m_layout->body;
    m_layoutAdd = new QPushButton(QStringLiteral("Add auto layout"), m_layout);
    m_layoutAdd->setObjectName(QStringLiteral("layoutAdd"));
    m_layoutAdd->setToolTip(ShortcutSettings::shared().tip(QStringLiteral("Lay the frame's children out in a row or column"), QStringLiteral("Add Auto Layout")));
    connect(m_layoutAdd, &QPushButton::clicked, this, [this] { m_session.addAutoLayout(); });
    body->addWidget(m_layoutAdd);

    m_layoutRows = new QWidget(m_layout);
    auto *rows = new QVBoxLayout(m_layoutRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(6);
    // The flow (a row, a column, or a row that wraps) and Remove sit in the heading, as Figma's do.
    m_layoutHeading = new QWidget(m_layout);
    auto *flowRow = new QHBoxLayout(m_layoutHeading);
    flowRow->setContentsMargins(0, 0, 0, 0);
    flowRow->setSpacing(2);
    m_layoutFlow = new QComboBox(m_layoutHeading);
    m_layoutFlow->setObjectName(QStringLiteral("layoutFlow"));
    m_layoutFlow->setAccessibleName(QStringLiteral("Direction"));
    m_layoutFlow->addItems({QStringLiteral("Horizontal"), QStringLiteral("Vertical"), QStringLiteral("Wrap")});
    connect(m_layoutFlow, &QComboBox::activated, this, [this](int index) {
        changeLayout(QStringLiteral("Direction"), [index](AutoLayout &layout) {
            layout.direction = index == 1 ? LayoutDirection::vertical : LayoutDirection::horizontal;
            layout.wrap = index == 2;
        });
    });
    auto *remove = new QToolButton(m_layoutHeading);
    remove->setText(QStringLiteral("−"));
    remove->setAutoRaise(true);
    remove->setFixedSize(NumberField::fieldHeight, NumberField::fieldHeight);
    remove->setAccessibleName(QStringLiteral("Remove auto layout"));
    remove->setObjectName(QStringLiteral("layoutRemove"));
    remove->setToolTip(ShortcutSettings::shared().tip(QStringLiteral("Remove auto layout; the children stay where they are"), QStringLiteral("Remove Auto Layout")));
    connect(remove, &QToolButton::clicked, this, [this] { m_session.removeAutoLayout(); });
    m_layoutFlow->setFixedWidth(96);
    flowRow->addWidget(m_layoutFlow);
    flowRow->addWidget(remove);
    m_layout->trailing->addWidget(m_layoutHeading);
    // The alignment grid on the left; gap and padding beside it.
    auto *beside = new QHBoxLayout;
    beside->setSpacing(6);
    auto *fields = new QVBoxLayout;
    fields->setSpacing(6);
    // Gap, or Auto: the room spread between the items.
    auto *gapRow = new QHBoxLayout;
    gapRow->setSpacing(6);
    m_layoutGap = new NumberField(QStringLiteral("Gap"), QStringLiteral("pt"), [this](double gap) {
        changeLayout(QStringLiteral("Gap"), [gap](AutoLayout &layout) {
            layout.gap = gap;
            layout.spaceBetween = false;
        });
    }, m_layoutRows);
    m_layoutGap->setObjectName(QStringLiteral("layoutGap"));
    m_layoutGap->field->setObjectName(QStringLiteral("layoutGapField"));
    m_layoutGap->lengths = true;
    m_layoutAutoGap = new QCheckBox(QStringLiteral("Auto"), m_layoutRows);
    m_layoutAutoGap->setObjectName(QStringLiteral("layoutAutoGap"));
    m_layoutAutoGap->setToolTip(QStringLiteral("Spread the items across the frame, the room between them"));
    connect(m_layoutAutoGap, &QCheckBox::toggled, this, [this](bool on) {
        changeLayout(QStringLiteral("Gap"), [on](AutoLayout &layout) { layout.spaceBetween = on; });
    });
    gapRow->addWidget(m_layoutGap, 1);
    gapRow->addWidget(m_layoutAutoGap);
    fields->addLayout(gapRow);
    // Padding, sides in pairs as Figma shows them first; the toggle beside them opens a field per side.
    // Both sets share the grid's cells, and only one is visible at a time.
    auto *padGrid = new QGridLayout;
    padGrid->setHorizontalSpacing(6);
    padGrid->setVerticalSpacing(6);
    padGrid->setColumnStretch(0, 1);
    padGrid->setColumnStretch(1, 1);
    const auto padField = [this](const QString &label, const QString &name, const QString &tip,
                                 std::function<void(AutoLayout &, double)> set) {
        auto *field = new NumberField(label, QStringLiteral("pt"), [this, set](double padding) {
            changeLayout(QStringLiteral("Padding"), [&](AutoLayout &layout) { set(layout, std::max(0.0, padding)); });
        }, m_layoutRows);
        field->setObjectName(name);
        field->field->setObjectName(name + QStringLiteral("Field"));
        field->field->setAccessibleName(tip);
        field->setToolTip(tip);
        field->lengths = true;
        field->minimum = 0;
        // A scrub is one undo step, however many values it passes through.
        field->gesture = [this](bool starting) {
            if (starting)
                m_session.beginEdit(QStringLiteral("Padding"));
            else
                m_session.endEdit();
        };
        return field;
    };
    m_layoutPadX = padField(QStringLiteral("↔"), QStringLiteral("layoutPaddingX"), QStringLiteral("Padding left and right"),
                            [](AutoLayout &layout, double padding) { layout.paddingLeft = layout.paddingRight = padding; });
    m_layoutPadY = padField(QStringLiteral("↕"), QStringLiteral("layoutPaddingY"), QStringLiteral("Padding top and bottom"),
                            [](AutoLayout &layout, double padding) { layout.paddingTop = layout.paddingBottom = padding; });
    m_layoutPad[0] = padField(QStringLiteral("L"), QStringLiteral("layoutPaddingLeft"), QStringLiteral("Padding left"),
                              [](AutoLayout &layout, double padding) { layout.paddingLeft = padding; });
    m_layoutPad[1] = padField(QStringLiteral("T"), QStringLiteral("layoutPaddingTop"), QStringLiteral("Padding top"),
                              [](AutoLayout &layout, double padding) { layout.paddingTop = padding; });
    m_layoutPad[2] = padField(QStringLiteral("R"), QStringLiteral("layoutPaddingRight"), QStringLiteral("Padding right"),
                              [](AutoLayout &layout, double padding) { layout.paddingRight = padding; });
    m_layoutPad[3] = padField(QStringLiteral("B"), QStringLiteral("layoutPaddingBottom"), QStringLiteral("Padding bottom"),
                              [](AutoLayout &layout, double padding) { layout.paddingBottom = padding; });
    m_layoutPadSides = iconButton(QStringLiteral("layoutPaddingSides"), QStringLiteral("Padding on each side; off for one value across and one down"),
                                  PanelIcon::padding, [this] { synchronize(); });
    m_layoutPadSides->setCheckable(true);
    padGrid->addWidget(m_layoutPadX, 0, 0);
    padGrid->addWidget(m_layoutPadY, 0, 1);
    padGrid->addWidget(m_layoutPad[0], 0, 0);
    padGrid->addWidget(m_layoutPad[1], 0, 1);
    padGrid->addWidget(m_layoutPad[2], 1, 0);
    padGrid->addWidget(m_layoutPad[3], 1, 1);
    padGrid->addWidget(m_layoutPadSides, 0, 2, Qt::AlignTop);
    fields->addLayout(padGrid);
    // The alignment grid: where the items sit in the frame, as the frame is laid out.
    auto *grid = new QGridLayout;
    grid->setSpacing(0);
    auto *cells = new QButtonGroup(m_layoutRows);
    cells->setExclusive(true);
    static const std::array<const char *, 3> across{"top", "middle", "bottom"}, along{"left", "centre", "right"};
    for (int cell = 0; cell < 9; ++cell) {
        auto *button = new QToolButton(m_layoutRows);
        button->setObjectName(QStringLiteral("layoutAlign%1").arg(cell));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFixedSize(18, 18);
        button->setText(QStringLiteral("·"));
        button->setToolTip(QStringLiteral("Align %1 %2").arg(QString::fromLatin1(across[size_t(cell / 3)]), QString::fromLatin1(along[size_t(cell % 3)])));
        cells->addButton(button, cell);
        grid->addWidget(button, cell / 3, cell % 3);
        m_layoutAlign[size_t(cell)] = button;
        connect(button, &QToolButton::clicked, this, [this, cell] {
            changeLayout(QStringLiteral("Alignment"), [cell](AutoLayout &layout) {
                const auto at = [](int index) { return index == 0 ? LayoutAlign::start : index == 1 ? LayoutAlign::center : LayoutAlign::end; };
                const bool horizontal = layout.direction == LayoutDirection::horizontal;
                layout.primary = at(horizontal ? cell % 3 : cell / 3);
                layout.counter = at(horizontal ? cell / 3 : cell % 3);
            });
        });
    }
    beside->addLayout(grid);
    beside->addLayout(fields, 1);
    rows->addLayout(beside);
    body->addWidget(m_layoutRows);

    // How the selection sizes itself, and whether it takes part in the flow.
    m_layoutSizing = new QWidget(m_layout);
    auto *sizing = new QGridLayout(m_layoutSizing);
    sizing->setContentsMargins(0, 0, 0, 0);
    sizing->setHorizontalSpacing(6);
    sizing->setVerticalSpacing(6);
    m_layoutWidth = sizingMenu(QStringLiteral("layoutWidth"), QStringLiteral("Width"), m_layoutSizing);
    m_layoutHeight = sizingMenu(QStringLiteral("layoutHeight"), QStringLiteral("Height"), m_layoutSizing);
    for (auto [menu, axis] : {std::pair{m_layoutWidth, Qt::Horizontal}, std::pair{m_layoutHeight, Qt::Vertical}}) {
        connect(menu, &QComboBox::activated, this, [this, menu, axis](int index) {
            m_session.setLayoutSizing(axis, LayoutSizing(menu->itemData(index).toInt()));
        });
    }
    sizing->addWidget(caption(QStringLiteral("W"), m_layoutSizing), 0, 0);
    sizing->addWidget(m_layoutWidth, 0, 1);
    sizing->addWidget(caption(QStringLiteral("H"), m_layoutSizing), 0, 2);
    sizing->addWidget(m_layoutHeight, 0, 3);
    sizing->setColumnStretch(1, 1);
    sizing->setColumnStretch(3, 1);
    body->addWidget(m_layoutSizing);
    m_layoutAbsolute = new QCheckBox(QStringLiteral("Absolute position"), m_layout);
    m_layoutAbsolute->setObjectName(QStringLiteral("layoutAbsolute"));
    m_layoutAbsolute->setToolTip(QStringLiteral("Keep it where it is, out of the auto-layout flow"));
    connect(m_layoutAbsolute, &QCheckBox::toggled, this, [this](bool on) { m_session.setAbsolutePosition(on); });
    // Constraints: how a child outside a flow follows its frame's resize.
    m_layoutConstraints = new QWidget(m_layout);
    auto *constraints = new QGridLayout(m_layoutConstraints);
    constraints->setContentsMargins(0, 0, 0, 0);
    constraints->setHorizontalSpacing(6);
    m_constraintX = new QComboBox(m_layoutConstraints);
    m_constraintX->setObjectName(QStringLiteral("constraintX"));
    m_constraintX->setAccessibleName(QStringLiteral("Horizontal constraint"));
    m_constraintX->setToolTip(QStringLiteral("What it keeps when its frame is resized across"));
    m_constraintX->addItems({QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Left & Right"), QStringLiteral("Center"), QStringLiteral("Scale")});
    m_constraintY = new QComboBox(m_layoutConstraints);
    m_constraintY->setObjectName(QStringLiteral("constraintY"));
    m_constraintY->setAccessibleName(QStringLiteral("Vertical constraint"));
    m_constraintY->setToolTip(QStringLiteral("What it keeps when its frame is resized up or down"));
    m_constraintY->addItems({QStringLiteral("Top"), QStringLiteral("Bottom"), QStringLiteral("Top & Bottom"), QStringLiteral("Center"), QStringLiteral("Scale")});
    for (auto [menu, axis] : {std::pair{m_constraintX, Qt::Horizontal}, std::pair{m_constraintY, Qt::Vertical}}) {
        // The menus list the constraints in LayoutConstraint's order.
        connect(menu, &QComboBox::activated, this, [this, axis](int index) { m_session.setConstraint(axis, LayoutConstraint(index)); });
    }
    constraints->addWidget(caption(QStringLiteral("↔"), m_layoutConstraints), 0, 0);
    constraints->addWidget(m_constraintX, 0, 1);
    constraints->addWidget(caption(QStringLiteral("↕"), m_layoutConstraints), 0, 2);
    constraints->addWidget(m_constraintY, 0, 3);
    constraints->setColumnStretch(1, 1);
    constraints->setColumnStretch(3, 1);
    body->addWidget(m_layoutConstraints);
    m_layoutClip = new QCheckBox(QStringLiteral("Clip content"), m_layout);
    m_layoutClip->setObjectName(QStringLiteral("layoutClip"));
    m_layoutClip->setToolTip(QStringLiteral("Show the frame's children only inside its box"));
    connect(m_layoutClip, &QCheckBox::toggled, this, [this](bool on) { m_session.setClipsContent(on); });
    // The two switches share a row; only one of them usually shows.
    auto *switches = new QHBoxLayout;
    switches->setSpacing(12);
    switches->addWidget(m_layoutAbsolute);
    switches->addWidget(m_layoutClip);
    switches->addStretch(1);
    body->addLayout(switches);
    // For the children of a Browser View: a note or callout that stays put while the site is previewed at other widths.
    m_layoutFixedPreview = new QCheckBox(QStringLiteral("Fixed while previewing"), m_layout);
    m_layoutFixedPreview->setObjectName(QStringLiteral("layoutFixedPreview"));
    m_layoutFixedPreview->setToolTip(QStringLiteral("Stay where the design put it when the Browser View is previewed at another width"));
    connect(m_layoutFixedPreview, &QCheckBox::toggled, this, [this](bool on) { m_session.setFixedWhilePreviewing(on); });
    body->addWidget(m_layoutFixedPreview);
    m_layout->summary = [this] {
        const std::optional<AutoLayout> layout = m_session.selectedAutoLayout();
        if (!layout)
            return m_layoutClip->isVisible() && m_layoutClip->isChecked() ? QStringLiteral("Clips content") : QString();
        return QStringLiteral("%1 · gap %2").arg(m_layoutFlow->currentText(), layout->spaceBetween ? QStringLiteral("auto") : NumberField::formatted(layout->gap));
    };
    return m_layout;
}

void PropertiesPanel::synchronizeLayout()
{
    const VectorDocument &document = *m_session.document();
    const std::vector<QUuid> frames = m_session.selectedFrames();
    const VectorObject *first = m_session.selection().empty() ? nullptr : document.find(m_session.selection().front());
    const VectorObject *parent = first && first->parentID ? document.find(*first->parentID) : nullptr;
    const bool inFlow = parent && parent->autoLayout;
    const std::optional<AutoLayout> layout = m_session.selectedAutoLayout();
    const bool anyLayout = std::any_of(frames.begin(), frames.end(), [&](const QUuid &id) { return document.find(id)->autoLayout.has_value(); });
    m_layoutAdd->setVisible(!frames.empty() && !anyLayout);
    m_layoutRows->setVisible(layout.has_value());
    m_layoutHeading->setVisible(layout.has_value());
    if (layout) {
        const QSignalBlocker quietFlow(m_layoutFlow), quietAuto(m_layoutAutoGap);
        m_layoutFlow->setCurrentIndex(layout->wrap && layout->direction == LayoutDirection::horizontal ? 2
                                      : layout->direction == LayoutDirection::vertical              ? 1
                                                                                                    : 0);
        m_layoutAutoGap->setChecked(layout->spaceBetween);
        if (layout->spaceBetween)
            m_layoutGap->syncUnset(layout->gap, QStringLiteral("Auto"));
        else
            m_layoutGap->sync(layout->gap);
        // Sides that differ show a field each, whatever the toggle said.
        const bool uneven = layout->paddingLeft != layout->paddingRight || layout->paddingTop != layout->paddingBottom;
        if (uneven && !m_layoutPadSides->isChecked()) {
            const QSignalBlocker quiet(m_layoutPadSides);
            m_layoutPadSides->setChecked(true);
        }
        const bool sides = m_layoutPadSides->isChecked();
        for (NumberField *field : {m_layoutPadX, m_layoutPadY})
            field->setVisible(!sides);
        for (NumberField *field : m_layoutPad)
            field->setVisible(sides);
        if (layout->paddingLeft == layout->paddingRight)
            m_layoutPadX->sync(layout->paddingLeft);
        else
            m_layoutPadX->syncMixed();
        if (layout->paddingTop == layout->paddingBottom)
            m_layoutPadY->sync(layout->paddingTop);
        else
            m_layoutPadY->syncMixed();
        m_layoutPad[0]->sync(layout->paddingLeft);
        m_layoutPad[1]->sync(layout->paddingTop);
        m_layoutPad[2]->sync(layout->paddingRight);
        m_layoutPad[3]->sync(layout->paddingBottom);
        const auto index = [](LayoutAlign align) { return align == LayoutAlign::start ? 0 : align == LayoutAlign::center ? 1 : 2; };
        const bool horizontal = layout->direction == LayoutDirection::horizontal;
        const int column = index(horizontal ? layout->primary : layout->counter), row = index(horizontal ? layout->counter : layout->primary);
        for (int cell = 0; cell < 9; ++cell) {
            const QSignalBlocker quiet(m_layoutAlign[size_t(cell)]);
            m_layoutAlign[size_t(cell)]->setChecked(cell == row * 3 + column);
            m_layoutAlign[size_t(cell)]->setText(cell == row * 3 + column ? QStringLiteral("●") : QStringLiteral("·"));
        }
    }
    // Hug is for content that lays itself out; Fill for a child of an auto-layout frame.
    const bool hugs = first && (first->autoLayout || first->kind == ObjectKind::text);
    m_layoutSizing->setVisible(first && (hugs || inFlow));
    for (auto [menu, sizing] : {std::pair{m_layoutWidth, first ? first->layout.width : LayoutSizing::fixed},
                                std::pair{m_layoutHeight, first ? first->layout.height : LayoutSizing::fixed}}) {
        const QSignalBlocker quiet(menu);
        auto *model = qobject_cast<QStandardItemModel *>(menu->model());
        if (model) {
            model->item(1)->setEnabled(hugs);
            model->item(2)->setEnabled(inFlow);
        }
        menu->setCurrentIndex(menu->findData(int(sizing)));
    }
    m_layoutAbsolute->setVisible(inFlow);
    {
        const QSignalBlocker quiet(m_layoutAbsolute);
        m_layoutAbsolute->setChecked(first && first->layout.absolute);
    }
    // Constraints apply where no flow places it: in a plain frame, or Absolute in an auto-layout one.
    const bool constrained = first && parent && parent->kind == ObjectKind::frame && (!parent->autoLayout || first->layout.absolute);
    m_layoutConstraints->setVisible(constrained);
    if (constrained) {
        const QSignalBlocker quietX(m_constraintX), quietY(m_constraintY);
        m_constraintX->setCurrentIndex(int(first->layout.horizontal));
        m_constraintY->setCurrentIndex(int(first->layout.vertical));
    }
    const bool inBrowserView = first && parent && parent->browser;
    m_layoutFixedPreview->setVisible(inBrowserView);
    if (inBrowserView) {
        const QSignalBlocker quiet(m_layoutFixedPreview);
        m_layoutFixedPreview->setChecked(first->layout.previewRule == PreviewRule::fixed);
    }
    m_layoutClip->setVisible(!frames.empty());
    {
        const QSignalBlocker quiet(m_layoutClip);
        m_layoutClip->setChecked(m_session.selectedFramesClip());
    }
}
