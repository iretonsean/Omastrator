#include "UI/PropertiesPanel.h"
#include "UI/ColorPaletteControls.h"
#include "UI/NumberField.h"
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <array>
#include <tuple>

namespace {
QLabel *heading(const QString &words, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    QFont font = label->font();
    font.setPixelSize(12);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

// A titled block with the panels' margins.
QWidget *section(const QString &title, QWidget *parent, QVBoxLayout *&body)
{
    auto *block = new QWidget(parent);
    body = new QVBoxLayout(block);
    body->setContentsMargins(16, 12, 16, 14);
    body->setSpacing(8);
    body->addWidget(heading(title, block));
    return block;
}
}

PropertiesPanel::PropertiesPanel(EditorSession &session, QWidget *parent) : QScrollArea(parent), m_session(session)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setAccessibleName(QStringLiteral("Properties"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(this);
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *title = new QHBoxLayout;
    title->setContentsMargins(18, 18, 18, 12);
    title->addWidget(heading(QStringLiteral("Properties"), content));
    column->addLayout(title);
    for (QWidget *block : {transformSection(), artboardSection(), appearanceSection(), strokeSection(), alignSection(), pathfinderSection()}) {
        column->addWidget(divider(content));
        column->addWidget(block);
    }
    column->addStretch(1);
    setWidget(content);
    applyIcons();
    connect(&m_session, &EditorSession::changed, this, &PropertiesPanel::synchronize);
    synchronize();
}

QToolButton *PropertiesPanel::iconButton(const QString &name, const QString &tip, PanelIcon icon, const std::function<void()> &run)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setAutoRaise(true);
    button->setFixedSize(28, 28);
    button->setIconSize(QSize(18, 18));
    connect(button, &QToolButton::clicked, this, run);
    m_iconButtons.push_back({button, icon});
    return button;
}

// The icons take the palette's ink, again after each theme.
void PropertiesPanel::applyIcons()
{
    const QColor ink = palette().color(QPalette::WindowText);
    for (const auto &[button, icon] : m_iconButtons)
        button->setIcon(PanelIcons::pixmap(icon, 18, ink, devicePixelRatio()));
}

void PropertiesPanel::changeEvent(QEvent *event)
{
    QScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyIcons();
}

QWidget *PropertiesPanel::transformSection()
{
    QVBoxLayout *body = nullptr;
    m_transform = section(QStringLiteral("Transform"), this, body);
    m_transform->setObjectName(QStringLiteral("transformSection"));
    const auto field = [this](const QString &label, const QString &suffix, const QString &name, std::function<void(double)> change) {
        auto *made = new NumberField(label, suffix, std::move(change), m_transform);
        made->setObjectName(name);
        made->field->setObjectName(name + QStringLiteral("Field"));
        return made;
    };
    m_x = field(QStringLiteral("X"), QStringLiteral("pt"), QStringLiteral("transformX"), [this](double x) { moveTo(x, m_session.selectionBounds().top()); });
    m_y = field(QStringLiteral("Y"), QStringLiteral("pt"), QStringLiteral("transformY"), [this](double y) { moveTo(m_session.selectionBounds().left(), y); });
    m_width = field(QStringLiteral("W"), QStringLiteral("pt"), QStringLiteral("transformW"),
                    [this](double width) { resizeTo(width, m_session.selectionBounds().height()); });
    m_height = field(QStringLiteral("H"), QStringLiteral("pt"), QStringLiteral("transformH"),
                     [this](double height) { resizeTo(m_session.selectionBounds().width(), height); });
    // Objects keep no angle: typing one turns them.
    m_rotation = field(QStringLiteral("⟳"), QStringLiteral("°"), QStringLiteral("transformRotation"), [this](double degrees) {
        m_session.rotateSelection(-degrees);
        m_rotation->sync(0);
    });
    m_rotation->setToolTip(QStringLiteral("Rotate the selection counterclockwise by this angle"));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);
    grid->addWidget(m_x, 0, 0);
    grid->addWidget(m_y, 0, 1);
    grid->addWidget(m_width, 1, 0);
    grid->addWidget(m_height, 1, 1);
    grid->addWidget(m_rotation, 2, 0);
    body->addLayout(grid);
    return m_transform;
}

QWidget *PropertiesPanel::artboardSection()
{
    QVBoxLayout *body = nullptr;
    m_artboard = section(QStringLiteral("Artboard"), this, body);
    m_artboard->setObjectName(QStringLiteral("artboardSection"));
    m_artboardWidth = new NumberField(QStringLiteral("W"), QStringLiteral("pt"), [this](double width) {
        if (m_session.document() && width > 0)
            m_session.setArtboardSize(QSizeF(width, m_session.document()->size.height()));
    }, m_artboard);
    m_artboardWidth->field->setObjectName(QStringLiteral("artboardWidth"));
    m_artboardHeight = new NumberField(QStringLiteral("H"), QStringLiteral("pt"), [this](double height) {
        if (m_session.document() && height > 0)
            m_session.setArtboardSize(QSizeF(m_session.document()->size.width(), height));
    }, m_artboard);
    m_artboardHeight->field->setObjectName(QStringLiteral("artboardHeight"));
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(m_artboardWidth);
    row->addWidget(m_artboardHeight);
    body->addLayout(row);
    return m_artboard;
}

QWidget *PropertiesPanel::alignSection()
{
    QVBoxLayout *body = nullptr;
    QWidget *block = section(QStringLiteral("Align"), this, body);
    const std::array<std::tuple<AlignEdge, const char *, const char *, PanelIcon>, 6> edges{{
        {AlignEdge::left, "alignLeft", "Horizontal align left", PanelIcon::alignLeft},
        {AlignEdge::horizontalCenter, "alignHorizontalCenter", "Horizontal align center", PanelIcon::alignHorizontalCenter},
        {AlignEdge::right, "alignRight", "Horizontal align right", PanelIcon::alignRight},
        {AlignEdge::top, "alignTop", "Vertical align top", PanelIcon::alignTop},
        {AlignEdge::verticalCenter, "alignVerticalCenter", "Vertical align center", PanelIcon::alignVerticalCenter},
        {AlignEdge::bottom, "alignBottom", "Vertical align bottom", PanelIcon::alignBottom},
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
    body->addLayout(row);
    auto *second = new QHBoxLayout;
    second->setSpacing(2);
    m_distributeButtons.push_back(iconButton(QStringLiteral("distributeHorizontal"), QStringLiteral("Horizontal distribute center"),
                                             PanelIcon::distributeHorizontal, [this] { m_session.distribute(DistributeAxis::horizontal); }));
    m_distributeButtons.push_back(iconButton(QStringLiteral("distributeVertical"), QStringLiteral("Vertical distribute center"),
                                             PanelIcon::distributeVertical, [this] { m_session.distribute(DistributeAxis::vertical); }));
    for (QToolButton *button : m_distributeButtons)
        second->addWidget(button);
    second->addStretch(1);
    m_alignTarget = new QComboBox(block);
    m_alignTarget->setObjectName(QStringLiteral("alignTarget"));
    m_alignTarget->addItems({QStringLiteral("Align to Selection"), QStringLiteral("Align to Artboard")});
    second->addWidget(m_alignTarget);
    body->addLayout(second);
    return block;
}

QWidget *PropertiesPanel::pathfinderSection()
{
    QVBoxLayout *body = nullptr;
    QWidget *block = section(QStringLiteral("Pathfinder"), this, body);
    const std::array<std::tuple<BooleanOperation, const char *, const char *, PanelIcon>, 4> operations{{
        {BooleanOperation::unite, "unite", "Unite", PanelIcon::unite},
        {BooleanOperation::minusFront, "minusFront", "Minus Front", PanelIcon::minusFront},
        {BooleanOperation::intersect, "intersect", "Intersect", PanelIcon::intersect},
        {BooleanOperation::exclude, "exclude", "Exclude", PanelIcon::exclude},
    }};
    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    for (const auto &[operation, name, tip, icon] : operations) {
        m_pathfinderButtons.push_back(iconButton(QString::fromLatin1(name), QString::fromLatin1(tip), icon,
                                                 [this, operation] { m_session.combineSelection(operation); }));
        row->addWidget(m_pathfinderButtons.back());
    }
    row->addStretch(1);
    body->addLayout(row);
    return block;
}

void PropertiesPanel::moveTo(double x, double y)
{
    const QRectF bounds = m_session.selectionBounds();
    m_session.transformSelection(QTransform::fromTranslate(x - bounds.left(), y - bounds.top()), QStringLiteral("Move"));
}

// Scales about the top-left corner, as the fields read it.
void PropertiesPanel::resizeTo(double width, double height)
{
    const QRectF bounds = m_session.selectionBounds();
    if (!(width > 0 && height > 0))
        return;
    const double sx = bounds.width() > 0 ? width / bounds.width() : 1;
    const double sy = bounds.height() > 0 ? height / bounds.height() : 1;
    const QTransform transform = QTransform::fromTranslate(-bounds.left(), -bounds.top()) * QTransform::fromScale(sx, sy)
        * QTransform::fromTranslate(bounds.left(), bounds.top());
    m_session.transformSelection(transform, QStringLiteral("Scale"));
}

void PropertiesPanel::synchronize()
{
    const bool drawn = m_session.document().has_value();
    const bool selected = m_session.hasSelection();
    widget()->setEnabled(drawn);
    m_transform->setVisible(selected);
    m_artboard->setVisible(!selected);
    const QRectF bounds = m_session.selectionBounds();
    m_x->sync(bounds.left());
    m_y->sync(bounds.top());
    m_width->sync(bounds.width());
    m_height->sync(bounds.height());
    m_rotation->sync(0);
    const QSizeF size = drawn ? m_session.document()->size : QSizeF(0, 0);
    m_artboardWidth->sync(size.width());
    m_artboardHeight->sync(size.height());
    m_fill->synchronize();
    m_strokePaint->synchronize();
    const StrokeStyle stroke = ShownStyle::stroke(m_session);
    m_strokeWidth->sync(stroke.width);
    m_cap->setCurrentIndex(stroke.cap == Qt::RoundCap ? 1 : stroke.cap == Qt::SquareCap ? 2 : 0);
    m_join->setCurrentIndex(stroke.join == Qt::RoundJoin ? 1 : stroke.join == Qt::BevelJoin ? 2 : 0);
    if (!m_dashes->hasFocus()) {
        QStringList numbers;
        for (const double dash : stroke.dashes)
            numbers << NumberField::formatted(dash);
        m_dashes->setText(numbers.join(QLatin1Char(' ')));
    }
    for (QToolButton *button : m_alignButtons)
        button->setEnabled(selected);
    for (QToolButton *button : m_distributeButtons)
        button->setEnabled(m_session.selection().size() >= 3);
    for (QToolButton *button : m_pathfinderButtons)
        button->setEnabled(m_session.canCombine());
}
