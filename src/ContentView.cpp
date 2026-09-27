#include "ContentView.h"
#include "Logging.h"
#include "UI/AgentBridge.h"
#include "UI/AgentPanels.h"
#include "UI/ColorPaletteControls.h"
#include "UI/IsolationBar.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/LayersPanel.h"
#include "UI/NewDocumentSheet.h"
#include "UI/ProjectWorkspace.h"
#include "UI/PropertiesPanel.h"
#include "UI/ToolHeaders.h"
#include "UI/ToolIcons.h"
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <cmath>

const std::vector<std::vector<Tool>> ContentView::railGroups{
    {Tool::select, Tool::directSelect},
    {Tool::pen, Tool::pencil, Tool::text, Tool::line},
    {Tool::rectangle, Tool::roundedRectangle, Tool::ellipse, Tool::polygon, Tool::star, Tool::shapeBuilder, Tool::scissors},
    {Tool::rotate, Tool::scale, Tool::eyedropper},
    {Tool::hand, Tool::zoom},
};

namespace {
const QString splitKey = QStringLiteral("panelSplitState");

// The grip between Properties and Layers: a visible bar; a double-click resets it.
class SplitHandle : public QSplitterHandle {
public:
    SplitHandle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent)
    {
        setToolTip(QStringLiteral("Drag to share the space between Properties and Layers; double-click to reset"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor line = palette().color(QPalette::Mid);
        painter.fillRect(QRectF(0, height() / 2.0 - 0.5, width(), 1), line);
        QColor grip = palette().color(underMouse() ? QPalette::WindowText : QPalette::PlaceholderText);
        painter.setPen(Qt::NoPen);
        painter.setBrush(grip);
        painter.drawRoundedRect(QRectF(width() / 2.0 - 16, height() / 2.0 - 2, 32, 4), 2, 2);
    }
    void enterEvent(QEnterEvent *event) override
    {
        QSplitterHandle::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QSplitterHandle::leaveEvent(event);
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        const int total = splitter()->sizes().value(0) + splitter()->sizes().value(1);
        splitter()->setSizes({total * 3 / 5, total - total * 3 / 5});
        QSettings().remove(splitKey);
    }
};

class PanelSplitter : public QSplitter {
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle *createHandle() override { return new SplitHandle(orientation(), this); }
};
}

// A rail button: its tool's icon; a plate when chosen.
class ToolButton : public QToolButton {
public:
    ToolButton(Tool tool, QWidget *parent) : QToolButton(parent), m_tool(tool)
    {
        setObjectName(QStringLiteral("tool:") + rawValue(tool));
        setCheckable(true);
        setFixedSize(34, 34);
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName(title(tool));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        if (isChecked()) {
            QColor plate = ink, edge = ink;
            plate.setAlphaF(0.12f);
            edge.setAlphaF(0.14f);
            painter.setPen(edge);
            painter.setBrush(plate);
            painter.drawRoundedRect(QRectF(0.5, 0.5, 33, 33), 7, 7);
        }
        ToolIcons::paint(painter, m_tool, QPointF(8, 8), ToolIcons::points, ink);
    }

private:
    const Tool m_tool;
};

// Roast My Design, at the bottom of the rail: a flame, the one playful button.
class RoastButton : public QToolButton {
public:
    explicit RoastButton(QWidget *parent) : QToolButton(parent)
    {
        setObjectName(QStringLiteral("roastMyDesign"));
        setFixedSize(34, 34);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(QStringLiteral("Roast My Design"));
        setAccessibleName(QStringLiteral("Roast My Design"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor ink = palette().color(QPalette::WindowText);
        if (!isEnabled())
            ink.setAlphaF(0.35f);
        if (underMouse() && isEnabled()) {
            QColor plate = ink;
            plate.setAlphaF(0.08f);
            painter.setPen(Qt::NoPen);
            painter.setBrush(plate);
            painter.drawRoundedRect(QRectF(0.5, 0.5, 33, 33), 7, 7);
        }
        QPainterPath flame;
        flame.moveTo(17, 7);
        flame.cubicTo(18, 12, 24, 14, 24, 20);
        flame.cubicTo(24, 24.5, 21, 27, 17, 27);
        flame.cubicTo(13, 27, 10, 24.5, 10, 20.5);
        flame.cubicTo(10, 17, 12.5, 15.5, 13.5, 13);
        flame.cubicTo(14.5, 15.5, 15, 16.5, 16, 17);
        flame.cubicTo(16.5, 13.5, 16, 10, 17, 7);
        painter.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(flame);
    }
};

namespace {
// An 8-point grip: dragged left, the dock widens.
class PanelResizeEdge : public QWidget {
public:
    PanelResizeEdge(QWidget &dock, QWidget *parent) : QWidget(parent), m_dock(dock)
    {
        setObjectName(QStringLiteral("panelEdge"));
        setFixedWidth(8);
        setCursor(Qt::SplitHCursor);
        setToolTip(QStringLiteral("Drag to resize the panels"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(width() / 2, 0, 1, height()), palette().color(QPalette::Mid));
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_start = std::pair(event->globalPosition().x(), double(m_dock.width()));
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_start)
            return;
        const double wanted = std::round(m_start->second - (event->globalPosition().x() - m_start->first));
        const double width = std::clamp(wanted, ContentView::minimumPanelWidth, ContentView::maximumPanelWidth);
        if (width == m_dock.width())
            return;
        m_dock.setFixedWidth(int(width));
        ContentView::setPanelWidth(width);
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_start = std::nullopt; }

private:
    QWidget &m_dock;
    std::optional<std::pair<double, double>> m_start;
};

// An accent ring while a drop may land.
class DropRing : public QWidget {
public:
    explicit DropRing(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("dropRing"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(4.5, 4.5, -4.5, -4.5), 8, 8);
    }
};

QFrame *divider(QFrame::Shape shape, QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(shape);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}
}

ContentView::ContentView(EditorSession &session, ProjectWorkspace *workspace, QWidget *parent, AgentBridge *agent)
    : QWidget(parent), m_session(session), m_workspace(workspace), m_agent(agent), m_column(new QVBoxLayout(this)), m_canvasSlot(new QGridLayout),
      m_canvas(new EditorCanvas(session, this)), m_propertiesPanel(new PropertiesPanel(session, this)), m_layersPanel(new LayersPanel(session, this)),
      m_dropRing(new DropRing(this)), m_zoom(new QLabel(this)), m_pointer(new QLabel(this)), m_artboard(new QLabel(this)),
      m_selection(new QLabel(this)), m_hint(new QLabel(this))
{
    setMinimumSize(800, 520);
    m_column->setContentsMargins(0, 0, 0, 0);
    m_column->setSpacing(0);
    auto *canvas = new QWidget(this);
    auto *canvasColumn = new QVBoxLayout(canvas);
    canvasColumn->setContentsMargins(0, 0, 0, 0);
    canvasColumn->setSpacing(0);
    // The accept bar sits above the canvas, never over the art.
    if (m_agent) {
        m_proposalBar = new ProposalBar(*m_agent, session, canvas);
        canvasColumn->addWidget(m_proposalBar);
        connect(m_agent, &AgentBridge::proposalChanged, this, &ContentView::synchronize);
    }
    // Isolation mode's breadcrumb, above the art it isolates.
    canvasColumn->addWidget(new IsolationBar(session, canvas));
    canvasColumn->addLayout(m_canvasSlot, 1);
    m_canvasSlot->setContentsMargins(0, 0, 0, 0);
    // The welcome sits over the canvas.
    m_canvasSlot->addWidget(m_canvas, 0, 0);
    m_canvasSlot->addWidget(m_dropRing, 0, 0);
    m_canvas->installEventFilter(this);
    setAcceptDrops(true);

    m_dock = new QWidget(this);
    m_dock->setObjectName(QStringLiteral("panelDock"));
    auto *dockColumn = new QVBoxLayout(m_dock);
    dockColumn->setContentsMargins(0, 0, 0, 0);
    auto *split = new PanelSplitter(Qt::Vertical, m_dock);
    split->setObjectName(QStringLiteral("panelSplit"));
    split->setChildrenCollapsible(false);
    split->setHandleWidth(9);
    split->addWidget(m_propertiesPanel);
    split->addWidget(m_layersPanel);
    // Properties holds more fields; it takes the larger share until moved.
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    if (!split->restoreState(QSettings().value(splitKey).toByteArray()))
        split->setSizes({600, 400});
    connect(split, &QSplitter::splitterMoved, this, [split] { QSettings().setValue(splitKey, split->saveState()); });
    dockColumn->addWidget(split);
    m_dock->setFixedWidth(int(panelWidth()));

    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);
    middle->addWidget(makeRail());
    middle->addWidget(divider(QFrame::VLine, this));
    middle->addWidget(canvas, 1);
    middle->addWidget(new PanelResizeEdge(*m_dock, this));
    middle->addWidget(m_dock);
    m_column->addWidget(divider(QFrame::HLine, this));
    m_column->addLayout(middle, 1);
    m_column->addWidget(divider(QFrame::HLine, this));
    m_column->addWidget(makeStatus());

    connect(m_canvas, &EditorCanvas::pointerMoved, this, &ContentView::showPointer);
    connect(m_canvas, &EditorCanvas::textEditingChanged, m_propertiesPanel, &PropertiesPanel::setEditingText);
    connect(&m_session, &EditorSession::changed, this, &ContentView::synchronize);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &ContentView::retitleTools);
    retitleTools();
    synchronizePanels();
    showPointer(std::nullopt);
    synchronize();
}

// ~QWidget deletes children after members go; stop listening first.
ContentView::~ContentView()
{
    disconnect(&m_session, &EditorSession::changed, this, &ContentView::synchronize);
    m_canvas->removeEventFilter(this);
}

QWidget *ContentView::makeRail()
{
    auto *tools = new QWidget(this);
    auto *toolColumn = new QVBoxLayout(tools);
    toolColumn->setContentsMargins(7, 12, 7, 12);
    toolColumn->setSpacing(2);
    for (const std::vector<Tool> &group : railGroups) {
        if (&group != &railGroups.front()) {
            toolColumn->addSpacing(4);
            toolColumn->addWidget(divider(QFrame::HLine, tools));
            toolColumn->addSpacing(4);
        }
        for (const Tool tool : group) {
            auto *button = new ToolButton(tool, tools);
            connect(button, &QToolButton::clicked, this, [this, tool] {
                m_session.selectTool(tool);
                synchronize();
            });
            toolColumn->addWidget(button, 0, Qt::AlignHCenter);
            m_toolButtons.push_back({tool, button});
        }
    }
    toolColumn->addSpacing(10);
    m_palette = new ColorPaletteControls(m_session, tools);
    toolColumn->addWidget(m_palette, 0, Qt::AlignHCenter);
    toolColumn->addStretch(1);
    if (m_agent) {
        m_roast = new RoastButton(tools);
        connect(m_roast, &QToolButton::clicked, this, [this] { m_agent->roast(); });
        toolColumn->addWidget(m_roast, 0, Qt::AlignHCenter);
    }
    // Too short a window scrolls the rail.
    auto *rail = new QScrollArea(this);
    rail->setObjectName(QStringLiteral("toolRail"));
    rail->setWidget(tools);
    rail->setWidgetResizable(true);
    rail->setFixedWidth(56);
    rail->setFrameShape(QFrame::NoFrame);
    rail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return rail;
}

QString ContentView::toolTip(Tool tool)
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (!definition.isMenu() && ShortcutDefinition::tool(definition.original) == tool)
            return QStringLiteral("%1 (%2)").arg(title(tool), ShortcutSettings::shared().chord(definition).label());
    }
    return title(tool);
}

void ContentView::retitleTools()
{
    for (const auto &[tool, button] : m_toolButtons)
        button->setToolTip(toolTip(tool));
}

void ContentView::synchronizePanels()
{
    const bool properties = showsPanel(propertiesKey), layers = showsPanel(layersKey);
    m_propertiesPanel->setVisible(properties);
    m_layersPanel->setVisible(layers);
    m_dock->setVisible(properties || layers);
    findChild<QWidget *>(QStringLiteral("panelEdge"))->setVisible(properties || layers);
}

// Shows what the session holds: every line compares, then sets.
void ContentView::synchronize()
{
    showHeader(m_session.tool());
    // A proposal pauses the tools: a click or a tool change would commit it.
    const bool proposal = hasProposal();
    m_canvas->setPaused(proposal);
    for (const auto &[tool, button] : m_toolButtons) {
        button->setChecked(m_session.tool() == tool);
        button->setEnabled(!proposal);
    }
    m_dock->setEnabled(!proposal);
    m_palette->setEnabled(!proposal);
    if (m_header)
        m_header->setEnabled(!proposal);
    if (m_roast)
        m_roast->setEnabled(m_session.hasDocument());
    showWelcome(!m_session.hasDocument());
    const std::optional<VectorDocument> &document = m_session.document();
    for (QLabel *label : {m_zoom, m_pointer, m_artboard, m_selection})
        label->setVisible(document.has_value());
    if (document) {
        m_zoom->setText(percent(m_session.viewport.zoom()));
        const QLocale english(QLocale::English, QLocale::UnitedStates);
        m_artboard->setText(QStringLiteral("%1 × %2 pt").arg(english.toString(document->size.width(), 'g', 6), english.toString(document->size.height(), 'g', 6)));
        const size_t count = m_session.selection().size();
        m_selection->setText(count == 0 ? QStringLiteral("No selection") : count == 1 ? QStringLiteral("1 object selected")
                                                                                     : QStringLiteral("%1 objects selected").arg(count));
    }
    m_hint->setText(document ? hint(m_session.tool()) : QStringLiteral("Ready when you are"));
}

// No document shows the welcome; a document takes keys.
void ContentView::showWelcome(bool shown)
{
    if (shown == (m_welcome != nullptr))
        return;
    if (!shown) {
        m_welcome->hide();
        m_welcome->deleteLater();
        m_welcome = nullptr;
        m_canvas->setFocus(Qt::OtherFocusReason);
        return;
    }
    m_welcome = new NewDocumentSheet(
        [this](QSizeF size) {
            if (m_workspace)
                m_workspace->createDocument(size);
            else
                m_session.createDocument(size);
        },
        [this] {
            if (m_workspace)
                m_workspace->open();
        },
        [this](const QString &path) {
            if (m_workspace)
                m_workspace->openFile(path);
        },
        this, m_workspace ? std::function<void()>([this] { m_workspace->connectCloud(); }) : std::function<void()>());
    m_canvasSlot->addWidget(m_welcome, 0, 0, Qt::AlignCenter);
    m_welcome->show();
}

void ContentView::showHeader(Tool tool)
{
    // Hand and Zoom share a bar; others have their own.
    if (m_shownTool && ToolHeaders::family(*m_shownTool) == ToolHeaders::family(tool))
        return;
    m_shownTool = tool;
    delete m_header;
    m_header = ToolHeaders::make(m_session, tool, this);
    m_header->setObjectName(QStringLiteral("toolHeader"));
    m_column->insertWidget(0, m_header);
    // A layout shows a late child only later: show now.
    m_header->show();
}
