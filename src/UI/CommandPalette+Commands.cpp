#include "Agent/AgentLauncher.h"
#include "UI/AgentBridge.h"
#include "UI/CommandPalette.h"
#include "UI/ContextMenus.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/Menus.h"
#include "UI/ObjectDialogs.h"
#include <QMenu>
#include <QMenuBar>
#include <QSet>

namespace {
QString plain(const QString &text)
{
    QString result = text;
    result.remove(QLatin1Char('&'));
    return result;
}

// A menu entry by the name it's searched for: short entries take their menu's words, as Illustrator names them.
QString titleFor(const QAction &entry, const QString &menu)
{
    const QString name = entry.objectName(), text = plain(entry.text());
    if (name.startsWith(QLatin1String("selectSame")) && name != QLatin1String("selectSameLayers"))
        return QStringLiteral("Select Same ") + text;
    if (name == QLatin1String("selectSameLayers"))
        return QStringLiteral("Select All on Same Layers");
    if (name == QLatin1String("selectAll"))
        return QStringLiteral("Select All");
    if (name == QLatin1String("selectInverse"))
        return QStringLiteral("Select Inverse");
    if (menu == QLatin1String("Select ▸ Object"))
        return QStringLiteral("Select ") + text;
    if (menu.endsWith(QLatin1String("Compound Path")) || menu.endsWith(QLatin1String("Clipping Mask")))
        return text + QLatin1Char(' ') + menu.section(QStringLiteral(" ▸ "), -1);
    if (name == QLatin1String("imageTraceMake"))
        return QStringLiteral("Image Trace");
    if (menu.endsWith(QLatin1String("Export")))
        return QStringLiteral("Export ") + text;
    return text;
}

// Words people search for that the names don't hold.
QString keywordsFor(const QString &name)
{
    static const QHash<QString, QString> words{
        {"artboardSize", "document setup canvas dimensions width height"},
        {"showGrid", "grid"},
        {"snapToGrid", "snapping grid"},
        {"outline", "outline mode wireframe preview"},
        {"preferences", "settings options"},
        {"keyboardShortcuts", "keys hotkeys remap bindings"},
        {"generate", "ai new art create variations"},
        {"editWithInstruction", "ai ask change instruct"},
        {"vectorizeWithAI", "ai trace smart"},
        {"connectAgent", "ai claude codex mcp"},
        {"showLayers", "panel"},
        {"showProperties", "panel inspector"},
        {"showSwatches", "panel colors palette"},
        {"contextualTaskBar", "task bar toolbar floating"},
        {"shareWithClient", "share client link send upload publish copy link"},
        {"shareOptions", "share client link format destination"},
        {"sharedLinks", "shared links unshare client feedback"},
        {"exportPNG", "save image"},
        {"exportSVG", "save vector"},
        {"exportPDF", "save print"},
        {"exportJPEG", "save image jpg"},
        {"createOutlines", "text to paths convert"},
        {"findFont", "missing fonts replace font typeface"},
        {"showTypeStyles", "character paragraph text styles panel"},
        {"duplicate", "copy clone"},
        {"copyProperties", "style appearance format painter eyedropper"},
        {"pasteProperties", "style appearance format painter apply"},
        {"delete", "remove"},
    };
    return words.value(name);
}

QString toolKey(Tool tool)
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (!definition.isMenu() && ShortcutDefinition::tool(definition.original) == tool)
            return ShortcutSettings::shared().chord(definition).label();
    }
    return {};
}
}

void CommandPalette::gather()
{
    m_commands.clear();
    gatherMenus();
    gatherContext();
    gatherRest();
}

void CommandPalette::gatherMenus()
{
    auto *bar = qobject_cast<QMenuBar *>(m_menus.parent());
    if (!bar)
        return;
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &path) {
        for (QAction *entry : menu->actions()) {
            if (entry->isSeparator())
                continue;
            if (QMenu *inner = entry->menu()) {
                // Open Recent lists its files on its own.
                if (entry->objectName() != QLatin1String("openRecent"))
                    walk(inner, path + QStringLiteral(" ▸ ") + plain(entry->text()));
                continue;
            }
            const QString name = entry->objectName();
            if (name.isEmpty() || name == QLatin1String("commandPalette"))
                continue;
            QPointer<QAction> action = entry;
            m_commands.push_back({QStringLiteral("action:") + name, titleFor(*entry, path), path,
                                  entry->shortcut().toString(QKeySequence::NativeText), keywordsFor(name), entry->isEnabled(),
                                  entry->isCheckable() && entry->isChecked(), [action] {
                                      if (action)
                                          action->trigger();
                                      return QString();
                                  }});
        }
    };
    for (QAction *top : bar->actions()) {
        if (QMenu *menu = top->menu())
            walk(menu, plain(top->text()));
    }
}

void CommandPalette::gatherContext()
{
    delete m_context;
    EditorCanvas *canvas = m_menus.canvas();
    EditorSession &session = m_menus.workspace().current().session;
    if (!canvas || !session.hasSelection())
        return;
    // What only the right-click menu holds: Align, Pathfinder, Isolate.
    m_context = ContextMenus::forCanvas(m_menus, session, *canvas, {}, this);
    m_context->hide();
    QSet<QString> known;
    for (const Command &command : m_commands)
        known.insert(command.id);
    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &where) {
        for (QAction *entry : menu->actions()) {
            if (QMenu *inner = entry->menu()) {
                walk(inner, plain(entry->text()));
                continue;
            }
            const QString name = entry->objectName();
            const QString id = QStringLiteral("action:") + name;
            if (name.isEmpty() || entry->isSeparator() || known.contains(id) || name == QLatin1String("askAI") || name == QLatin1String("selectLayer"))
                continue;
            known.insert(id);
            const QString title = name.startsWith(QLatin1String("contextAlign")) ? QStringLiteral("Align ") + plain(entry->text()) : plain(entry->text());
            QPointer<QAction> action = entry;
            m_commands.push_back({id, title, where.isEmpty() ? QStringLiteral("Selection") : where, QString(), keywordsFor(name), entry->isEnabled(),
                                  false, [action] {
                                      if (action)
                                          action->trigger();
                                      return QString();
                                  }});
        }
    };
    walk(m_context, QString());
}

void CommandPalette::gatherRest()
{
    ProjectWorkspace &workspace = m_menus.workspace();
    EditorSession &session = workspace.current().session;
    AgentBridge *agent = m_menus.agent();
    const bool proposal = agent && agent->hasProposalIn(session);
    // The front session when it runs, not when the palette opened.
    const auto front = [&workspace]() -> EditorSession & { return workspace.current().session; };
    for (const Tool tool : allTools) {
        m_commands.push_back({QStringLiteral("tool:") + rawValue(tool), title(tool) + QStringLiteral(" Tool"), QStringLiteral("Tools"), toolKey(tool),
                              rawValue(tool), !proposal, session.tool() == tool, [front, tool] {
                                  front().selectTool(tool);
                                  return QString();
                              }});
    }
    for (const QString &path : ProjectWorkspace::recentFiles()) {
        m_commands.push_back({QStringLiteral("recent:") + path, ProjectWorkspace::recentLabel(path), QStringLiteral("File ▸ Open Recent"), QString(),
                              path + QStringLiteral(" open recent file"), !workspace.isManaging(), false, [&workspace, path] {
                                  workspace.openFile(path);
                                  return QString();
                              }});
    }
    const bool drawn = session.hasDocument();
    m_commands.push_back({QStringLiteral("setting:smartGuides"), QStringLiteral("Smart Guides"), QStringLiteral("View"), QString(),
                          QStringLiteral("snapping snap align guides"), drawn, session.usesSmartGuides, [front] {
                              front().usesSmartGuides = !front().usesSmartGuides;
                              return QString();
                          }});
    QWidget *window = &m_menus.window();
    m_commands.push_back({QStringLiteral("setting:keyboardIncrement"), QStringLiteral("Keyboard Increment…"),
                          QStringLiteral("Preferences · %1 pt").arg(EditorCanvas::keyboardIncrement()), QString(),
                          QStringLiteral("nudge arrow keys step distance"), true, false, [window] {
                              ObjectDialogs::preferences(window);
                              return QString();
                          }});
    if (!agent)
        return;
    m_commands.push_back({QStringLiteral("ai:roast"), QStringLiteral("Roast My Design"),
                          QStringLiteral("AI · Heat: %1").arg(AgentLauncher::title(AgentLauncher::savedRoastHeat())), QString(),
                          QStringLiteral("critique feedback review ai"), drawn, false, [agent] { return agent->roast(); }});
    m_commands.push_back({QStringLiteral("panel:variations"), QStringLiteral("Variations"), QStringLiteral("Window · AI"), QString(),
                          QStringLiteral("panel generate results"), true, false, [agent] {
                              agent->showVariationsPanel();
                              return QString();
                          }});
    m_commands.push_back({QStringLiteral("panel:live"), QStringLiteral("Live"), QStringLiteral("Window"), QString(),
                          QStringLiteral("panel web site deploy"), true, false, [agent] {
                              agent->showLivePanel();
                              return QString();
                          }});
}

std::optional<CommandPalette::Command> CommandPalette::ask(const QString &request) const
{
    AgentBridge *agent = m_menus.agent();
    EditorSession &session = m_menus.workspace().current().session;
    if (!agent || request.isEmpty() || !session.hasDocument())
        return std::nullopt;
    const bool selected = session.hasSelection();
    const bool empty = std::none_of(session.document()->objects.begin(), session.document()->objects.end(),
                                    [](const VectorObject &object) { return object.kind != ObjectKind::layer; });
    // Nothing selected and new art asked for, or nothing drawn yet: Generate; otherwise an edit, previewed.
    const bool generate = !selected && (empty || asksForNewArt(request));
    Command command;
    command.id = QStringLiteral("ask");
    command.title = QStringLiteral("Ask %1: %2").arg(m_agentName, request);
    command.where = selected ? QStringLiteral("Edit the selection") : generate ? QStringLiteral("Generate new art") : QStringLiteral("Edit the document");
    command.run = [agent, request, generate] { return generate ? agent->generate(request, 3, false) : agent->editWithInstruction(request); };
    return command;
}
