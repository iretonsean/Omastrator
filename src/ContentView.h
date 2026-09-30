#pragma once
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QGridLayout>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

class AgentBridge;
class CapturePanel;
class ContextBar;
class LayersPanel;
class MotionTimeline;
class ProposalBar;
class PropertiesPanel;
class ProjectWorkspace;
class QMimeData;
class QTabWidget;
class ToolSlot;

// One document's editor area (docs/WINDOW-LAYOUT.md): the selection and AI row, the island, the rail,
// Layers, the canvas, the Properties and Capture dock, and the status bar.
class ContentView : public QWidget {
    Q_OBJECT
public:
    // Without a workspace the welcome and drops do nothing.
    // Without an agent bridge there is no Roast My Design and no accept bar.
    explicit ContentView(EditorSession &session, ProjectWorkspace *workspace = nullptr, QWidget *parent = nullptr,
                         AgentBridge *agent = nullptr);
    ~ContentView() override;

    // The rail's slots, top to bottom, in Illustrator's groups; a slot with more than one
    // tool flies out on right-click or a long press, and shows the last one picked.
    static const std::vector<std::vector<std::vector<Tool>>> toolSlotGroups;
    // Every slot, flattened, in rail order (what railGroups used to be, one level deeper).
    static std::vector<std::vector<Tool>> toolSlots();
    // View ▸ Toolbar, and the rail's own context menu: which tools the rail hides.
    enum class ToolPreset { basic, advanced };
    static ToolPreset toolPreset();
    static void setToolPreset(ToolPreset preset);
    // Basic hides the tools Illustrator considers advanced; a tool picked by key still
    // shows in its slot while it's the active one, even when it's on this list.
    static bool hiddenInBasic(Tool tool);
    // The status bar's words for a tool.
    static QString hint(Tool tool);
    // Zoom as percent, one decimal at most.
    static QString percent(double zoom);
    // Window ▸ Layers and Properties, remembered across launches.
    static const QString layersKey;
    static const QString propertiesKey;
    static bool showsPanel(const QString &key);
    static void setShowsPanel(const QString &key, bool shown);
    // The dock's width, remembered across launches.
    static constexpr double minimumPanelWidth = 220;
    static constexpr double maximumPanelWidth = 400;
    static constexpr double defaultPanelWidth = 264;
    static double panelWidth();
    static void setPanelWidth(double width);
    // The Layers panel's width on the left, remembered across launches, in the same range.
    static constexpr double defaultLayersWidth = 240;
    static double layersWidth();
    static void setLayersWidth(double width);
    // A tool's tooltip: its name and current key.
    static QString toolTip(Tool tool);

    EditorCanvas &canvas() const { return *m_canvas; }
    LayersPanel &layersPanel() const { return *m_layersPanel; }
    PropertiesPanel &propertiesPanel() const { return *m_propertiesPanel; }
    ContextBar &contextBar() const { return *m_contextBar; }
    CapturePanel &capturePanel() const { return *m_capturePanel; }
    // Shows the dock's panels as the settings say.
    void synchronizePanels();
    bool acceptsDrop(const QMimeData &data) const;
    // An agent's proposal is open here: tools pause until Enter or Esc.
    bool hasProposal() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QWidget *makeRail();
    QWidget *makeStatus();
    void synchronize();
    void showHeader(Tool tool);
    void showWelcome(bool shown);
    // Remapped keys, then tool letters, before the canvas.
    bool canvasKey(QKeyEvent *event);
    // Canvas keys pressed while a panel holds focus: true when the canvas took them.
    bool panelKey(QWidget *focus, QKeyEvent *event);
    void showPointer(std::optional<QPointF> point);
    // A line in the status bar's hint for a few seconds.
    void flash(const QString &text);
    void retitleTools();

    EditorSession &m_session;
    const QPointer<ProjectWorkspace> m_workspace;
    const QPointer<AgentBridge> m_agent;
    ContextBar *m_contextBar = nullptr;
    QWidget *m_contextDivider = nullptr;
    ProposalBar *m_proposalBar = nullptr;
    QToolButton *m_roast = nullptr;
    QWidget *m_palette = nullptr;
    QVBoxLayout *const m_column;
    ToolHeaderBar *m_header = nullptr;
    std::optional<Tool> m_shownTool;
    std::vector<ToolSlot *> m_toolSlots;
    QGridLayout *const m_canvasSlot;
    EditorCanvas *const m_canvas;
    MotionTimeline *m_timeline = nullptr;
    PropertiesPanel *const m_propertiesPanel;
    LayersPanel *const m_layersPanel;
    QWidget *m_dock = nullptr;
    QTabWidget *m_dockTabs = nullptr;
    CapturePanel *m_capturePanel = nullptr;
    QWidget *m_layersDock = nullptr;
    QWidget *const m_dropRing;
    QWidget *m_welcome = nullptr;
    QLabel *const m_zoom;
    QLabel *const m_pointer;
    QLabel *const m_artboard;
    QLabel *const m_selection;
    QLabel *const m_hint;
    bool m_forwarding = false;
    // A passing message on the hint label, like "Moved to Page 2".
    QString m_flash;
    int m_flashNumber = 0;
};
