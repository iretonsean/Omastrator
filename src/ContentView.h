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
class LayersPanel;
class ProposalBar;
class PropertiesPanel;
class ProjectWorkspace;
class QMimeData;
class QSplitter;
class ToolButton;

// One document's editor area: tool bar, rail, canvas, dock, status.
class ContentView : public QWidget {
    Q_OBJECT
public:
    // Without a workspace the welcome and drops do nothing.
    // Without an agent bridge there is no Roast My Design and no accept bar.
    explicit ContentView(EditorSession &session, ProjectWorkspace *workspace = nullptr, QWidget *parent = nullptr,
                         AgentBridge *agent = nullptr);
    ~ContentView() override;

    // The rail, top to bottom, in Illustrator's groups.
    static const std::vector<std::vector<Tool>> railGroups;
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
    // A tool's tooltip: its name and current key.
    static QString toolTip(Tool tool);

    EditorCanvas &canvas() const { return *m_canvas; }
    LayersPanel &layersPanel() const { return *m_layersPanel; }
    PropertiesPanel &propertiesPanel() const { return *m_propertiesPanel; }
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
    void showPointer(std::optional<QPointF> point);
    void retitleTools();

    EditorSession &m_session;
    const QPointer<ProjectWorkspace> m_workspace;
    const QPointer<AgentBridge> m_agent;
    ProposalBar *m_proposalBar = nullptr;
    QToolButton *m_roast = nullptr;
    QWidget *m_palette = nullptr;
    QVBoxLayout *const m_column;
    ToolHeaderBar *m_header = nullptr;
    std::optional<Tool> m_shownTool;
    std::vector<std::pair<Tool, ToolButton *>> m_toolButtons;
    QGridLayout *const m_canvasSlot;
    EditorCanvas *const m_canvas;
    PropertiesPanel *const m_propertiesPanel;
    LayersPanel *const m_layersPanel;
    QWidget *m_dock = nullptr;
    QWidget *const m_dropRing;
    QWidget *m_welcome = nullptr;
    QLabel *const m_zoom;
    QLabel *const m_pointer;
    QLabel *const m_artboard;
    QLabel *const m_selection;
    QLabel *const m_hint;
    bool m_forwarding = false;
};
