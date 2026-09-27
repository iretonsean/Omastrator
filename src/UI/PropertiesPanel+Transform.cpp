#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QSettings>
#include <array>
#include <cmath>
#include <tuple>

namespace {
const QString linkKey = QStringLiteral("properties/constrainProportions");
const QString scaleStrokesKey = QStringLiteral("properties/scaleStrokes");

QTransform about(QPointF pivot, const QTransform &transform)
{
    return QTransform::fromTranslate(-pivot.x(), -pivot.y()) * transform * QTransform::fromTranslate(pivot.x(), pivot.y());
}
}

PanelSection *PropertiesPanel::transformSection()
{
    m_transform = new PanelSection(QStringLiteral("Transform"), QStringLiteral("transform"), this);
    QVBoxLayout *body = m_transform->body;
    const auto field = [this](const QString &label, const QString &suffix, const QString &name, std::function<void(double)> change) {
        auto *made = new NumberField(label, suffix, std::move(change), m_transform);
        made->setObjectName(name);
        made->field->setObjectName(name + QStringLiteral("Field"));
        // Figma shows no units; points still read, and pt px mm cm in pc convert.
        made->lengths = suffix.isEmpty() || suffix == QLatin1String("pt");
        made->setToolTip(QStringLiteral("%1, in points").arg(label));
        return made;
    };
    m_x = field(QStringLiteral("X"), QString(), QStringLiteral("transformX"), [this](double x) { moveTo(x, reference().y()); });
    m_y = field(QStringLiteral("Y"), QString(), QStringLiteral("transformY"), [this](double y) { moveTo(reference().x(), y); });
    m_width = field(QStringLiteral("W"), QString(), QStringLiteral("transformW"), [this](double width) {
        const QRectF bounds = m_session.selectionBounds();
        const bool linked = m_link->isChecked() && bounds.width() > 0;
        resizeTo(width, linked ? bounds.height() * width / bounds.width() : bounds.height());
    });
    m_height = field(QStringLiteral("H"), QString(), QStringLiteral("transformH"), [this](double height) {
        const QRectF bounds = m_session.selectionBounds();
        const bool linked = m_link->isChecked() && bounds.height() > 0;
        resizeTo(linked ? bounds.width() * height / bounds.height() : bounds.width(), height);
    });
    // Objects keep no angle: typing one turns them about the reference point.
    m_rotation = field(QString(QChar(0x2009)), QStringLiteral("°"), QStringLiteral("transformRotation"), [this](double degrees) {
        // A scrub turns by what changed since its last step, about where it began.
        const double by = degrees - m_rotation->value();
        m_session.rotateSelection(-by, m_rotationPivot.value_or(reference()));
        m_rotation->sync(m_rotationPivot ? degrees : 0);
    });
    m_rotation->field->setAccessibleName(QStringLiteral("Rotation"));
    m_rotation->lengths = false;
    m_rotation->setToolTip(QStringLiteral("Rotation: turns the selection counterclockwise by this angle"));
    m_rotation->handle()->setToolTip(QStringLiteral("Rotation. Drag to turn: Shift for ten times, Alt for a tenth"));
    m_rotation->handle()->setAccessibleName(QStringLiteral("Rotation"));
    m_rotation->gesture = [this](bool starting) {
        if (starting) {
            m_rotationPivot = reference();
            m_session.beginEdit(QStringLiteral("Rotate"));
        } else {
            m_rotationPivot.reset();
            m_session.endEdit();
            m_rotation->sync(0);
        }
    };
    for (NumberField *length : {m_x, m_y, m_width, m_height}) {
        length->gesture = [this, length](bool starting) {
            if (starting)
                m_session.beginEdit(length == m_x || length == m_y ? QStringLiteral("Move") : QStringLiteral("Scale"));
            else
                m_session.endEdit();
        };
    }
    m_x->changeEach = [this](const std::function<double(double)> &change) { moveEach(false, change); };
    m_y->changeEach = [this](const std::function<double(double)> &change) { moveEach(true, change); };
    m_width->changeEach = [this](const std::function<double(double)> &change) { resizeEach(false, change); };
    m_height->changeEach = [this](const std::function<double(double)> &change) { resizeEach(true, change); };
    m_width->minimum = m_height->minimum = 0.01;

    m_reference = new ReferencePointPicker(m_transform);
    connect(m_reference, &ReferencePointPicker::pointChanged, this, &PropertiesPanel::synchronize);
    m_link = iconButton(QStringLiteral("transformLink"), QStringLiteral("Constrain width and height proportions"), PanelIcon::unlink, [this] {
        QSettings().setValue(linkKey, m_link->isChecked());
        applyIcons();
    });
    m_link->setCheckable(true);
    m_link->setChecked(QSettings().value(linkKey, false).toBool());
    m_link->setFixedSize(22, 28);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    grid->addWidget(m_reference, 0, 0, 2, 1, Qt::AlignTop);
    grid->addWidget(m_x, 0, 1);
    grid->addWidget(m_y, 0, 3);
    grid->addWidget(m_width, 1, 1);
    grid->addWidget(m_link, 1, 2);
    grid->addWidget(m_height, 1, 3);
    grid->addWidget(m_rotation, 2, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    body->addLayout(grid);

    // Options that change what scaling does, as Illustrator's Transform panel menu holds.
    auto *options = iconButton(QStringLiteral("transformOptions"), QStringLiteral("Transform options"), PanelIcon::more, [] {});
    options->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(options);
    m_scaleStrokes = menu->addAction(QStringLiteral("Scale Strokes && Effects"));
    m_scaleStrokes->setObjectName(QStringLiteral("scaleStrokes"));
    m_scaleStrokes->setCheckable(true);
    m_scaleStrokes->setChecked(QSettings().value(scaleStrokesKey, false).toBool());
    connect(m_scaleStrokes, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(scaleStrokesKey, on);
        m_session.scaleStrokes = on;
    });
    options->setMenu(menu);
    options->setFixedSize(24, 22);
    m_transform->trailing->addWidget(options);
    return m_transform;
}

QPointF PropertiesPanel::reference() const
{
    return ReferencePointPicker::locate(m_session.selectionBounds(), m_reference->point());
}

void PropertiesPanel::moveTo(double x, double y)
{
    const QPointF from = reference();
    m_session.transformSelection(QTransform::fromTranslate(x - from.x(), y - from.y()), QStringLiteral("Move"));
}

void PropertiesPanel::resizeTo(double width, double height)
{
    const QRectF bounds = m_session.selectionBounds();
    if (!(width > 0 && height > 0))
        return;
    const double sx = bounds.width() > 0 ? width / bounds.width() : 1;
    const double sy = bounds.height() > 0 ? height / bounds.height() : 1;
    m_session.transformSelection(about(reference(), QTransform::fromScale(sx, sy)), QStringLiteral("Scale"), true);
}

void PropertiesPanel::moveEach(bool vertical, const std::function<double(double)> &change)
{
    const int point = m_reference->point();
    m_session.transformEach([&](const QRectF &bounds) {
        const QPointF at = ReferencePointPicker::locate(bounds, point);
        return vertical ? QTransform::fromTranslate(0, change(at.y()) - at.y()) : QTransform::fromTranslate(change(at.x()) - at.x(), 0);
    }, QStringLiteral("Move"));
}

void PropertiesPanel::resizeEach(bool vertical, const std::function<double(double)> &change)
{
    const int point = m_reference->point();
    const bool linked = m_link->isChecked();
    m_session.transformEach([&](const QRectF &bounds) {
        const double from = vertical ? bounds.height() : bounds.width();
        const double to = change(from);
        if (!(from > 0 && to > 0))
            return QTransform();
        const double factor = to / from;
        const double sx = vertical && !linked ? 1 : factor, sy = !vertical && !linked ? 1 : factor;
        return about(ReferencePointPicker::locate(bounds, point), QTransform::fromScale(sx, sy));
    }, QStringLiteral("Scale"), true);
}

PanelSection *PropertiesPanel::alignSection()
{
    m_align = new PanelSection(QStringLiteral("Align"), QStringLiteral("align"), this);
    const std::array<std::tuple<AlignEdge, const char *, const char *, PanelIcon>, 6> edges{{
        {AlignEdge::left, "alignLeft", "Align left edges", PanelIcon::alignLeft},
        {AlignEdge::horizontalCenter, "alignHorizontalCenter", "Align horizontal centers", PanelIcon::alignHorizontalCenter},
        {AlignEdge::right, "alignRight", "Align right edges", PanelIcon::alignRight},
        {AlignEdge::top, "alignTop", "Align top edges", PanelIcon::alignTop},
        {AlignEdge::verticalCenter, "alignVerticalCenter", "Align vertical centers", PanelIcon::alignVerticalCenter},
        {AlignEdge::bottom, "alignBottom", "Align bottom edges", PanelIcon::alignBottom},
    }};
    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    for (const auto &[edge, name, tip, icon] : edges) {
        m_alignButtons.push_back(iconButton(QString::fromLatin1(name), QString::fromLatin1(tip), icon, [this, edge] {
            m_session.align(edge, m_alignTarget->currentIndex() == 1 ? AlignTarget::artboard : AlignTarget::selection);
        }));
        row->addWidget(m_alignButtons.back());
    }
    row->addStretch(1);
    m_align->body->addLayout(row);
    auto *second = new QHBoxLayout;
    second->setSpacing(2);
    m_distributeButtons.push_back(iconButton(QStringLiteral("distributeHorizontal"), QStringLiteral("Distribute horizontal centers (three or more objects)"),
                                             PanelIcon::distributeHorizontal, [this] { m_session.distribute(DistributeAxis::horizontal); }));
    m_distributeButtons.push_back(iconButton(QStringLiteral("distributeVertical"), QStringLiteral("Distribute vertical centers (three or more objects)"),
                                             PanelIcon::distributeVertical, [this] { m_session.distribute(DistributeAxis::vertical); }));
    for (QToolButton *button : m_distributeButtons)
        second->addWidget(button);
    second->addSpacing(8);
    m_alignTarget = new QComboBox(m_align);
    m_alignTarget->setObjectName(QStringLiteral("alignTarget"));
    m_alignTarget->setAccessibleName(QStringLiteral("Align to"));
    m_alignTarget->setToolTip(QStringLiteral("What the selection aligns to"));
    m_alignTarget->addItems({QStringLiteral("To selection"), QStringLiteral("To artboard")});
    second->addWidget(m_alignTarget, 1);
    m_align->body->addLayout(second);
    return m_align;
}

PanelSection *PropertiesPanel::pathfinderSection()
{
    m_pathfinder = new PanelSection(QStringLiteral("Pathfinder"), QStringLiteral("pathfinder"), this);
    const std::array<std::tuple<BooleanOperation, const char *, const char *, PanelIcon>, 4> operations{{
        {BooleanOperation::unite, "unite", "Unite (Pathfinder): merge the shapes into one", PanelIcon::unite},
        {BooleanOperation::minusFront, "minusFront", "Minus Front (Pathfinder): cut the front shapes out of the back one", PanelIcon::minusFront},
        {BooleanOperation::intersect, "intersect", "Intersect (Pathfinder): keep where the shapes overlap", PanelIcon::intersect},
        {BooleanOperation::exclude, "exclude", "Exclude (Pathfinder): keep where the shapes don't overlap", PanelIcon::exclude},
    }};
    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    for (const auto &[operation, name, tip, icon] : operations) {
        m_pathfinderButtons.push_back(iconButton(QString::fromLatin1(name), QString::fromUtf8(tip), icon,
                                                 [this, operation] { m_session.combineSelection(operation); }));
        m_pathfinderButtons.back()->setAccessibleName(QString::fromUtf8(tip).section(QStringLiteral(" ("), 0, 0));
        row->addWidget(m_pathfinderButtons.back());
    }
    row->addStretch(1);
    m_pathfinder->body->addLayout(row);
    return m_pathfinder;
}
