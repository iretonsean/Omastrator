#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStandardItemModel>

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
    m_layoutAdd->setToolTip(QStringLiteral("Lay the frame's children out in a row or column (Shift+A)"));
    connect(m_layoutAdd, &QPushButton::clicked, this, [this] { m_session.addAutoLayout(); });
    body->addWidget(m_layoutAdd);

    m_layoutRows = new QWidget(m_layout);
    auto *rows = new QVBoxLayout(m_layoutRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(6);
    // The flow: a row, a column, or a row that wraps.
    auto *flowRow = new QHBoxLayout;
    flowRow->setSpacing(6);
    m_layoutFlow = new QComboBox(m_layoutRows);
    m_layoutFlow->setObjectName(QStringLiteral("layoutFlow"));
    m_layoutFlow->setAccessibleName(QStringLiteral("Direction"));
    m_layoutFlow->addItems({QStringLiteral("Horizontal"), QStringLiteral("Vertical"), QStringLiteral("Wrap")});
    connect(m_layoutFlow, &QComboBox::activated, this, [this](int index) {
        changeLayout(QStringLiteral("Direction"), [index](AutoLayout &layout) {
            layout.direction = index == 1 ? LayoutDirection::vertical : LayoutDirection::horizontal;
            layout.wrap = index == 2;
        });
    });
    auto *remove = new QPushButton(QStringLiteral("Remove"), m_layoutRows);
    remove->setObjectName(QStringLiteral("layoutRemove"));
    remove->setToolTip(QStringLiteral("Remove auto layout; the children stay where they are (Alt+Shift+A)"));
    connect(remove, &QPushButton::clicked, this, [this] { m_session.removeAutoLayout(); });
    flowRow->addWidget(m_layoutFlow, 1);
    flowRow->addWidget(remove);
    rows->addLayout(flowRow);
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
    rows->addLayout(gapRow);
    // Padding, sides in pairs as Figma shows them first.
    auto *padRow = new QHBoxLayout;
    padRow->setSpacing(10);
    m_layoutPadX = new NumberField(QStringLiteral("Pad ↔"), QStringLiteral("pt"), [this](double padding) {
        changeLayout(QStringLiteral("Padding"), [padding](AutoLayout &layout) { layout.paddingLeft = layout.paddingRight = std::max(0.0, padding); });
    }, m_layoutRows);
    m_layoutPadX->setObjectName(QStringLiteral("layoutPaddingX"));
    m_layoutPadX->field->setObjectName(QStringLiteral("layoutPaddingXField"));
    m_layoutPadY = new NumberField(QStringLiteral("Pad ↕"), QStringLiteral("pt"), [this](double padding) {
        changeLayout(QStringLiteral("Padding"), [padding](AutoLayout &layout) { layout.paddingTop = layout.paddingBottom = std::max(0.0, padding); });
    }, m_layoutRows);
    m_layoutPadY->setObjectName(QStringLiteral("layoutPaddingY"));
    m_layoutPadY->field->setObjectName(QStringLiteral("layoutPaddingYField"));
    for (NumberField *field : {m_layoutPadX, m_layoutPadY}) {
        field->lengths = true;
        field->minimum = 0;
    }
    padRow->addWidget(m_layoutPadX, 1);
    padRow->addWidget(m_layoutPadY, 1);
    rows->addLayout(padRow);
    // The alignment grid: where the items sit in the frame, as the frame is laid out.
    auto *alignRow = new QHBoxLayout;
    alignRow->setSpacing(10);
    alignRow->addWidget(caption(QStringLiteral("Align"), m_layoutRows));
    auto *grid = new QGridLayout;
    grid->setSpacing(2);
    auto *cells = new QButtonGroup(m_layoutRows);
    cells->setExclusive(true);
    static const std::array<const char *, 3> across{"top", "middle", "bottom"}, along{"left", "centre", "right"};
    for (int cell = 0; cell < 9; ++cell) {
        auto *button = new QToolButton(m_layoutRows);
        button->setObjectName(QStringLiteral("layoutAlign%1").arg(cell));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFixedSize(20, 20);
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
    alignRow->addLayout(grid);
    alignRow->addStretch(1);
    rows->addLayout(alignRow);
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
    body->addWidget(m_layoutAbsolute);
    m_layoutClip = new QCheckBox(QStringLiteral("Clip content"), m_layout);
    m_layoutClip->setObjectName(QStringLiteral("layoutClip"));
    m_layoutClip->setToolTip(QStringLiteral("Show the frame's children only inside its box"));
    connect(m_layoutClip, &QCheckBox::toggled, this, [this](bool on) { m_session.setClipsContent(on); });
    body->addWidget(m_layoutClip);
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
        if (layout->paddingLeft == layout->paddingRight)
            m_layoutPadX->sync(layout->paddingLeft);
        else
            m_layoutPadX->syncMixed();
        if (layout->paddingTop == layout->paddingBottom)
            m_layoutPadY->sync(layout->paddingTop);
        else
            m_layoutPadY->syncMixed();
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
    m_layoutClip->setVisible(!frames.empty());
    {
        const QSignalBlocker quiet(m_layoutClip);
        m_layoutClip->setChecked(m_session.selectedFramesClip());
    }
}
