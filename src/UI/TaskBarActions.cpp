#include "UI/TaskBarActions.h"
#include "Canvas/EditorCanvas.h"
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ContextMenus.h"
#include "UI/Menus.h"
#include "UI/NumberField.h"
#include <QEvent>
#include <QFontComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QToolTip>

namespace {
// A menu entry's words without the ellipsis, as a button says them.
QString plain(const QString &text)
{
    QString result = text;
    result.remove(QLatin1Char('&'));
    if (result.endsWith(QChar(0x2026)))
        result.chop(1);
    return result;
}

QString withKey(const QString &text, const QKeySequence &key)
{
    return key.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText));
}

QToolButton *button(const QString &name, const QString &label, QWidget *parent)
{
    auto *made = new QToolButton(parent);
    made->setObjectName(name);
    made->setText(label);
    made->setAccessibleName(label);
    made->setAutoRaise(true);
    made->setFocusPolicy(Qt::NoFocus);
    made->setToolButtonStyle(Qt::ToolButtonTextOnly);
    made->setMinimumHeight(26);
    return made;
}

// A menu bar entry as a button: its label, its state, and its current key in the tooltip.
class ActionButton : public QToolButton {
public:
    ActionButton(QAction *action, const QString &label, QWidget *parent) : QToolButton(parent), m_action(action)
    {
        setObjectName(QStringLiteral("taskBar:") + action->objectName());
        setText(label);
        setAccessibleName(label);
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setMinimumHeight(26);
        setEnabled(action->isEnabled());
        connect(action, &QAction::changed, this, [this] {
            if (m_action)
                setEnabled(m_action->isEnabled());
        });
        connect(this, &QToolButton::clicked, this, [this] {
            if (m_action && m_action->isEnabled())
                m_action->trigger();
        });
    }

protected:
    bool event(QEvent *event) override
    {
        // The tooltip reads the key as remapped now.
        if (event->type() == QEvent::ToolTip && m_action)
            setToolTip(withKey(plain(m_action->text()), m_action->shortcut()));
        return QToolButton::event(event);
    }

private:
    QPointer<QAction> m_action;
};

// A button whose menu is built as it opens, so it reflects the selection then.
QToolButton *menuButton(const QString &name, const QString &label, QWidget *parent, std::function<void(QMenu *)> fill)
{
    QToolButton *made = button(name, label, parent);
    made->setAccessibleName(label);
    made->setToolTip(label);
    auto *menu = new QMenu(made);
    menu->setObjectName(name + QStringLiteral("Menu"));
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, fill = std::move(fill)] {
        menu->clear();
        fill(menu);
    });
    made->setMenu(menu);
    made->setPopupMode(QToolButton::InstantPopup);
    return made;
}

QWidget *separator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("taskBarSeparator"));
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    line->setFixedHeight(18);
    return line;
}

// The field Ask AI… types into: Enter runs Edit with Instruction on the selection.
class AskField : public QLineEdit {
public:
    AskField(AgentBridge &agent, QAction *instruct, EditorCanvas &canvas, QWidget *parent)
        : QLineEdit(parent), m_agent(agent), m_instruct(instruct), m_canvas(canvas)
    {
        setObjectName(QStringLiteral("taskBarAsk"));
        setPlaceholderText(QStringLiteral("Ask AI…"));
        setAccessibleName(QStringLiteral("Ask AI"));
        setAccessibleDescription(QStringLiteral("Describe a change to the selection and press Enter. The result is a preview you keep or discard."));
        setToolTip(QStringLiteral("Describe a change to the selection, then press Enter"));
        setFixedWidth(190);
        setClearButtonEnabled(true);
        const auto follow = [this] {
            if (m_instruct)
                setEnabled(m_instruct->isEnabled());
        };
        if (instruct)
            connect(instruct, &QAction::changed, this, follow);
        follow();
        connect(this, &QLineEdit::returnPressed, this, [this] { ask(); });
    }

    void ask()
    {
        if (text().trimmed().isEmpty())
            return;
        const QString failure = m_agent.editWithInstruction(text());
        setProperty("failure", failure);
        if (!failure.isEmpty()) {
            QToolTip::showText(mapToGlobal(QPoint(0, height())), failure, this);
            return;
        }
        clear();
        m_canvas.setFocus(Qt::OtherFocusReason);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        // Escape gives the keys back to the canvas.
        if (event->key() == Qt::Key_Escape) {
            clear();
            m_canvas.setFocus(Qt::OtherFocusReason);
            return;
        }
        QLineEdit::keyPressEvent(event);
    }

private:
    AgentBridge &m_agent;
    QPointer<QAction> m_instruct;
    EditorCanvas &m_canvas;
};

ColorPaletteControls *railPalette(EditorCanvas &canvas)
{
    for (QWidget *up = canvas.parentWidget(); up; up = up->parentWidget()) {
        if (auto *content = qobject_cast<ContentView *>(up))
            return content->findChild<ColorPaletteControls *>();
    }
    return nullptr;
}

struct Filler {
    Menus &menus;
    AgentBridge *agent;
    EditorCanvas &canvas;
    QHBoxLayout &row;
    QWidget *parent;

    EditorSession &session() const { return canvas.session(); }

    void action(const char *name, const QString &label = QString())
    {
        if (QAction *entry = menus.action(QString::fromLatin1(name)))
            row.addWidget(new ActionButton(entry, label.isEmpty() ? plain(entry->text()) : label, parent));
    }

    void tool(Tool tool, const QString &name, const QString &label)
    {
        QToolButton *made = button(name, label, parent);
        made->setToolTip(ContentView::toolTip(tool));
        EditorSession &s = session();
        QObject::connect(made, &QToolButton::clicked, made, [&s, tool] { s.selectTool(tool); });
        row.addWidget(made);
    }

    void swatches()
    {
        EditorSession &s = session();
        auto *fill = new PaintSwatch([&s] { return ShownStyle::fill(s); }, false, parent);
        auto *stroke = new PaintSwatch([&s] { return ShownStyle::stroke(s).paint; }, true, parent);
        fill->setObjectName(QStringLiteral("taskBarFill"));
        stroke->setObjectName(QStringLiteral("taskBarStroke"));
        fill->setAccessibleName(QStringLiteral("Fill"));
        stroke->setAccessibleName(QStringLiteral("Stroke"));
        fill->setToolTip(QStringLiteral("Fill color"));
        stroke->setToolTip(QStringLiteral("Stroke color"));
        for (PaintSwatch *swatch : {fill, stroke}) {
            swatch->setFixedSize(20, 20);
            swatch->setCursor(Qt::PointingHandCursor);
            row.addWidget(swatch);
        }
        const auto sync = [&s, fill, stroke] {
            fill->setMixed(ShownStyle::fillMixed(s));
            stroke->setMixed(ShownStyle::strokeMixed(s));
            fill->update();
            stroke->update();
        };
        QObject::connect(&s, &EditorSession::changed, fill, sync);
        sync();
        EditorCanvas *on = &canvas;
        QObject::connect(fill, &QAbstractButton::clicked, fill, [on] {
            if (ColorPaletteControls *palette = railPalette(*on))
                palette->pickFill();
        });
        QObject::connect(stroke, &QAbstractButton::clicked, stroke, [on] {
            if (ColorPaletteControls *palette = railPalette(*on))
                palette->pickStroke();
        });
        row.addSpacing(2);
    }

    void align()
    {
        EditorSession &s = session();
        row.addWidget(menuButton(QStringLiteral("taskBarAlign"), QStringLiteral("Align"), parent, [&s](QMenu *menu) {
            ContextMenus::addAlign(menu, s);
            menu->addSeparator();
            menu->addAction(QStringLiteral("Distribute Horizontally"), menu, [&s] { s.distribute(DistributeAxis::horizontal); })
                ->setObjectName(QStringLiteral("taskBarDistributeHorizontal"));
            menu->addAction(QStringLiteral("Distribute Vertically"), menu, [&s] { s.distribute(DistributeAxis::vertical); })
                ->setObjectName(QStringLiteral("taskBarDistributeVertical"));
        }));
    }

    void pathfinder()
    {
        EditorSession &s = session();
        Menus &m = menus;
        row.addWidget(menuButton(QStringLiteral("taskBarPathfinder"), QStringLiteral("Pathfinder"), parent,
                                 [&m, &s](QMenu *menu) { ContextMenus::addPathfinder(menu, m, s); }));
    }

    void pathMenu()
    {
        Menus &m = menus;
        row.addWidget(menuButton(QStringLiteral("taskBarPath"), QStringLiteral("Path"), parent, [&m](QMenu *menu) {
            for (const char *name : {"outlineStroke", "offsetPath", "simplify"}) {
                if (QAction *entry = m.action(QString::fromLatin1(name)))
                    menu->addAction(entry);
            }
        }));
    }

    void typeFields()
    {
        EditorSession &s = session();
        auto *family = new QFontComboBox(parent);
        family->setObjectName(QStringLiteral("taskBarFont"));
        family->setAccessibleName(QStringLiteral("Font family"));
        family->setToolTip(QStringLiteral("Font family"));
        family->setFixedWidth(150);
        family->setFocusPolicy(Qt::ClickFocus);
        QObject::connect(family, &QFontComboBox::currentFontChanged, family, [&s](const QFont &font) {
            const QString chosen = font.family();
            if (chosen == s.shownText().family)
                return;
            s.updateText([&chosen](TextContent &text) {
                const int weight = text.isBold() ? 700 : 400;
                const bool italic = text.isItalic();
                text.family = chosen;
                text.style = TextContent::styleFor(chosen, weight, italic);
            }, QStringLiteral("Font"));
        });
        auto *size = new NumberField(QString(), QStringLiteral("pt"), [&s](double value) {
            s.updateText([value](TextContent &text) { text.size = std::clamp(value, 0.1, 1296.0); }, QStringLiteral("Font Size"));
        }, parent);
        size->setObjectName(QStringLiteral("taskBarSize"));
        size->field->setObjectName(QStringLiteral("taskBarSizeField"));
        size->field->setAccessibleName(QStringLiteral("Font size"));
        size->field->setToolTip(QStringLiteral("Font size"));
        size->field->setFixedWidth(44);
        size->step = 1;
        const auto sync = [&s, family, size] {
            if (s.selectedTexts().empty())
                return;
            const TextContent text = s.shownText();
            const QSignalBlocker quiet(family);
            family->setCurrentFont(QFont(text.family));
            size->sync(text.size);
        };
        QObject::connect(&s, &EditorSession::changed, family, sync);
        sync();
        row.addWidget(family);
        row.addWidget(size);
    }

    void end()
    {
        EditorSession &s = session();
        if (agent) {
            row.addWidget(separator(parent));
            row.addWidget(new AskField(*agent, menus.action(QStringLiteral("editWithInstruction")), canvas, parent));
        }
        QToolButton *more = button(QStringLiteral("taskBarMore"), QStringLiteral("⋯"), parent);
        more->setAccessibleName(QStringLiteral("More Actions"));
        more->setToolTip(QStringLiteral("More actions"));
        Menus &m = menus;
        EditorCanvas *on = &canvas;
        QObject::connect(more, &QToolButton::clicked, more, [&m, &s, on, more] {
            QMenu *menu = ContextMenus::forCanvas(m, s, *on, {}, on);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(more->mapToGlobal(QPoint(0, more->height() + 4)));
        });
        row.addWidget(more);
    }

    void fill(const QString &kind)
    {
        if (kind == QLatin1String("path")) {
            swatches();
            tool(Tool::directSelect, QStringLiteral("taskBarEditPath"), QStringLiteral("Edit Path"));
            pathMenu();
        } else if (kind == QLatin1String("paths")) {
            swatches();
            pathfinder();
            tool(Tool::shapeBuilder, QStringLiteral("taskBarShapeBuilder"), QStringLiteral("Shape Builder"));
            align();
            action("group");
        } else if (kind == QLatin1String("objects")) {
            align();
            action("group");
            if (session().canCombine())
                pathfinder();
            swatches();
        } else if (kind.startsWith(QLatin1String("text"))) {
            typeFields();
            action("createOutlines");
            if (kind == QLatin1String("text:point"))
                action("convertToAreaType", QStringLiteral("Area Type"));
            else
                action("convertToPointType", QStringLiteral("Point Type"));
        } else if (kind == QLatin1String("image")) {
            action("imageTraceMake", QStringLiteral("Image Trace"));
            action("vectorizeWithAI");
        } else if (kind == QLatin1String("group")) {
            if (!session().selectedInstances().empty())
                action("detachInstance", QStringLiteral("Detach"));
            action("ungroup");
            QToolButton *isolate = button(QStringLiteral("taskBarIsolate"), QStringLiteral("Isolate"), parent);
            isolate->setToolTip(QStringLiteral("Isolate Selected Group"));
            EditorCanvas *on = &canvas;
            EditorSession &s = session();
            QObject::connect(isolate, &QToolButton::clicked, isolate, [on, &s] {
                if (s.selection().size() == 1)
                    on->isolateGroup(s.selection().front());
            });
            row.addWidget(isolate);
            swatches();
        } else if (kind == QLatin1String("frame")) {
            // A Browser View's first action is picking its page's elements.
            const VectorObject *selected = session().document()->find(session().selection().front());
            if (selected && selected->browser)
                action("browserViewEditPage", QStringLiteral("Edit Page"));
            swatches();
            if (session().canRemoveAutoLayout())
                action("removeAutoLayout", QStringLiteral("Remove Auto Layout"));
            else
                action("addAutoLayout", QStringLiteral("Auto Layout"));
            action("clipContent");
            action("ungroup", QStringLiteral("Remove Frame"));
        } else if (kind == QLatin1String("clipGroup")) {
            action("releaseClippingMask", QStringLiteral("Release Clipping Mask"));
            action("ungroup");
        }
        end();
    }
};
}

QString TaskBarActions::kind(const EditorSession &session)
{
    if (!session.hasDocument() || !session.hasSelection())
        return {};
    const VectorDocument &document = *session.document();
    const std::vector<QUuid> &selected = session.selection();
    if (selected.size() == 1) {
        const VectorObject *object = document.find(selected.front());
        if (!object)
            return {};
        switch (object->kind) {
        case ObjectKind::group: return object->isClipGroup ? QStringLiteral("clipGroup") : QStringLiteral("group");
        case ObjectKind::text: return object->text.area ? QStringLiteral("text:area") : QStringLiteral("text:point");
        case ObjectKind::image: return QStringLiteral("image");
        case ObjectKind::path: return QStringLiteral("path");
        case ObjectKind::frame: return QStringLiteral("frame");
        case ObjectKind::layer: return {};
        }
        return {};
    }
    bool paths = true, texts = true, point = false;
    for (const QUuid &id : selected) {
        const VectorObject *object = document.find(id);
        if (!object)
            return {};
        paths = paths && object->kind == ObjectKind::path;
        texts = texts && object->kind == ObjectKind::text;
        point = point || (object->kind == ObjectKind::text && !object->text.area);
    }
    if (texts)
        return point ? QStringLiteral("text:point") : QStringLiteral("text:area");
    return paths ? QStringLiteral("paths") : QStringLiteral("objects");
}

TaskBar *TaskBarActions::attach(Menus &menus, AgentBridge *agent, EditorCanvas &canvas)
{
    auto *bar = new TaskBar(canvas);
    EditorSession &session = canvas.session();
    bar->setFiller([&session] { return kind(session); },
                   [&menus, agent, &canvas, bar](QHBoxLayout &row) { Filler{menus, agent, canvas, row, bar}.fill(bar->shownKind()); });
    if (agent) {
        // An edit on its way, or its proposal waiting for Keep or Discard: the bar steps aside.
        bar->setBlocked([agent, &session] {
            const auto &waiting = agent->waiting();
            return agent->hasProposalIn(session)
                || (waiting && (waiting->task == AgentBridge::Task::edit || waiting->task == AgentBridge::Task::vectorize));
        });
        QObject::connect(agent, &AgentBridge::proposalChanged, bar, &TaskBar::refresh);
        QObject::connect(agent, &AgentBridge::waitingChanged, bar, &TaskBar::refresh);
    }
    return bar;
}
