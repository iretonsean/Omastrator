#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>

namespace {
const std::array<const char *, 4> cornerNames{"Top left", "Top right", "Bottom right", "Bottom left"};
}

PanelSection *PropertiesPanel::shapeSection()
{
    m_shape = new PanelSection(QStringLiteral("Shape"), QStringLiteral("shape"), this);
    QVBoxLayout *body = m_shape->body;
    // One radius for all four corners; unlinked, a field each.
    m_radius = new NumberField(QStringLiteral("Radius"), QStringLiteral("pt"), [this](double radius) { m_session.setCornerRadius(radius); }, m_shape);
    m_radius->setObjectName(QStringLiteral("shapeCornerRadius"));
    m_radius->field->setObjectName(QStringLiteral("shapeCornerRadiusField"));
    m_radius->lengths = true;
    m_radius->minimum = 0;
    m_radius->setToolTip(QStringLiteral("Corner radius of every corner, in points"));
    m_radius->gesture = [this](bool starting) {
        if (starting)
            m_session.beginEdit(QStringLiteral("Corner Radius"));
        else
            m_session.endEdit();
    };
    m_cornersLinked = iconButton(QStringLiteral("shapeCornersLinked"), QStringLiteral("Same radius on every corner; off for a radius each"),
                                 PanelIcon::cornerRadius, [this] { synchronize(); });
    m_cornersLinked->setCheckable(true);
    m_cornersLinked->setChecked(true);
    auto *style = iconButton(QStringLiteral("shapeCornerStyle"), QStringLiteral("Corner style"), PanelIcon::more, [] {});
    style->setPopupMode(QToolButton::InstantPopup);
    auto *styles = new QMenu(style);
    for (const auto &[text, name, kind] : {std::tuple{"Round", "cornerRound", CornerStyle::round},
                                           std::tuple{"Inverted Round", "cornerInverted", CornerStyle::inverted},
                                           std::tuple{"Chamfer", "cornerChamfer", CornerStyle::chamfer}}) {
        QAction *entry = styles->addAction(QString::fromLatin1(text), this, [this, kind] { m_session.setCornerStyle(kind); });
        entry->setObjectName(QString::fromLatin1(name));
    }
    style->setMenu(styles);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(m_radius, 1);
    row->addWidget(m_cornersLinked);
    row->addWidget(style);
    body->addLayout(row);
    m_corners = new QWidget(m_shape);
    m_corners->setObjectName(QStringLiteral("shapeCorners"));
    auto *grid = new QGridLayout(m_corners);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    for (int corner = 0; corner < 4; ++corner) {
        static const std::array<const char *, 4> labels{"TL", "TR", "BR", "BL"};
        auto *field = new NumberField(QString::fromLatin1(labels[size_t(corner)]), QStringLiteral("pt"),
                                      [this, corner](double radius) { m_session.setCornerRadius(radius, corner); }, m_corners);
        field->setObjectName(QStringLiteral("shapeCorner%1").arg(corner));
        field->field->setObjectName(QStringLiteral("shapeCorner%1Field").arg(corner));
        field->lengths = true;
        field->minimum = 0;
        field->setToolTip(QStringLiteral("%1 corner radius, in points").arg(QString::fromLatin1(cornerNames[size_t(corner)])));
        field->field->setAccessibleName(QStringLiteral("%1 corner radius").arg(QString::fromLatin1(cornerNames[size_t(corner)])));
        m_corner[size_t(corner)] = field;
        // Clockwise from the top left, as the corners sit.
        grid->addWidget(field, corner == 0 || corner == 1 ? 0 : 1, corner == 0 || corner == 3 ? 0 : 1);
    }
    body->addWidget(m_corners);
    // Compound paths: which overlaps fill.
    m_fillRuleRow = new QWidget(m_shape);
    auto *rule = new QHBoxLayout(m_fillRuleRow);
    rule->setContentsMargins(0, 0, 0, 0);
    rule->setSpacing(6);
    auto *caption = new QLabel(QStringLiteral("Fill rule"), m_fillRuleRow);
    caption->setForegroundRole(QPalette::PlaceholderText);
    m_fillRule = new QComboBox(m_fillRuleRow);
    m_fillRule->setObjectName(QStringLiteral("fillRule"));
    m_fillRule->setAccessibleName(QStringLiteral("Fill rule"));
    m_fillRule->setToolTip(QStringLiteral("Non-zero fills every overlap; even-odd leaves every second one as a hole"));
    m_fillRule->addItems({QStringLiteral("Non-zero"), QStringLiteral("Even-odd")});
    connect(m_fillRule, &QComboBox::activated, this, [this](int index) {
        m_session.setFillRuleOfSelection(index == 1 ? Qt::OddEvenFill : Qt::WindingFill);
    });
    rule->addWidget(caption);
    rule->addWidget(m_fillRule, 1);
    body->addWidget(m_fillRuleRow);
    return m_shape;
}

void PropertiesPanel::synchronizeShape()
{
    const VectorDocument &document = *m_session.document();
    const std::vector<QUuid> shapes = m_session.selectedShapes();
    std::array<std::optional<double>, 4> radii;
    std::array<bool, 4> mixed{};
    for (const QUuid &id : shapes) {
        const LiveRectangle &shape = *document.find(id)->liveShape();
        for (size_t corner = 0; corner < 4; ++corner) {
            if (radii[corner] && *radii[corner] != shape.radii[corner])
                mixed[corner] = true;
            radii[corner] = shape.radii[corner];
        }
    }
    const bool uneven = std::any_of(mixed.begin(), mixed.end(), [](bool each) { return each; })
        || std::any_of(radii.begin(), radii.end(), [&](const std::optional<double> &radius) { return radius != radii[0]; });
    // Different corners show one field each, whatever the link said.
    if (uneven && m_cornersLinked->isChecked()) {
        const QSignalBlocker quiet(m_cornersLinked);
        m_cornersLinked->setChecked(false);
    }
    const bool live = !shapes.empty();
    for (QWidget *each : {static_cast<QWidget *>(m_radius), static_cast<QWidget *>(m_cornersLinked),
                          static_cast<QWidget *>(m_shape->findChild<QToolButton *>(QStringLiteral("shapeCornerStyle")))})
        each->setVisible(live);
    m_corners->setVisible(live && !m_cornersLinked->isChecked());
    if (uneven)
        m_radius->syncMixed();
    else
        m_radius->sync(radii[0].value_or(0));
    for (size_t corner = 0; corner < 4; ++corner) {
        if (mixed[corner])
            m_corner[corner]->syncMixed();
        else
            m_corner[corner]->sync(radii[corner].value_or(0));
    }
    const std::vector<QUuid> compound = m_session.selectedCompoundPaths();
    m_fillRuleRow->setVisible(!compound.empty());
    if (!compound.empty()) {
        const QSignalBlocker quiet(m_fillRule);
        m_fillRule->setCurrentIndex(document.find(compound.front())->path.fillRule == Qt::OddEvenFill ? 1 : 0);
    }
}
