#pragma once
#include "../Agent/FakeHyprctl.h"
#include "UI/PageStandIn.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTest>

// A little Hyprland behind the fake hyprctl. It behaves as the real one does where the feature depends on it:
// - a window maps with the title its widget has at that moment (later title changes show up), on the focused
//   workspace, and takes focus;
// - workspaces have ids: numbered ones are positive, named ones negative and new on every creation;
// - a workspace that is empty and isn't the focused one is deleted at once (`deleted()` says which);
// - a dispatcher's selector is a number (that workspace by id), `name:<name>` (by name, else a new named
//   workspace, even for "1") or `special:<name>`; anything else is refused (`rejected()`).
// Dispatchers in the log are applied to it; `clients`, `activeworkspace` and `workspaces` are what the app then
// reads. Only hyprlang's dispatch lines are understood; a `--batch` call is applied one dispatcher at a time, in
// order, and each is in `history()` on its own, while `batches()` keeps the calls as they came.
class FakeHyprlandWorld {
public:
    struct Window {
        QString address;
        qint64 pid = 0;
        QString title;
        QString workspace;
        QPointer<QWidget> owner;
        // The title at map time, for tests that care what the app could find the window by.
        QString mappedTitle;
    };

    explicit FakeHyprlandWorld(FakeHyprctl &hyprctl, const QString &active = QStringLiteral("1")) : m_hyprctl(hyprctl), m_active(active)
    {
        resolve(isNumber(active) ? active : QStringLiteral("name:") + active);
        publish();
    }

    QString active() const { return m_active; }
    // Every dispatch the app has made since `clearHistory`.
    const QStringList &history() const { return m_history; }
    void clearHistory()
    {
        m_history.clear();
        m_batches.clear();
    }
    // Every call that dispatched, as sent: several dispatchers in one `--batch` are one entry.
    const QStringList &batches() const { return m_batches; }
    QList<Window> &windows() { return m_windows; }
    // The user changes workspace (Super+Tab, a swipe): the app hears of it from the event stream.
    void go(const QString &workspace)
    {
        m_active = resolve(isNumber(workspace) ? workspace : QStringLiteral("name:") + workspace);
        focusOn(m_active);
        prune();
        publish();
    }
    // A window put on a special workspace (the Desk puts the editor on `special:omastrator-desk`).
    void stash(const QString &address, const QString &special)
    {
        if (Window *w = window(address))
            w->workspace = resolve(QStringLiteral("special:") + special);
        prune();
        publish();
    }
    // The editor: mapped on the focused workspace like any window.
    QString addEditor(QWidget &editor) { return add(QCoreApplication::applicationPid(), titleOf(&editor), &editor); }
    // Someone else's window, mapped straight onto `workspace` (the user put it there).
    QString addForeign(const QString &workspace, const QString &title = QStringLiteral("Firefox"))
    {
        const QString address = add(4242, title, nullptr);
        window(address)->workspace = resolve(isNumber(workspace) ? workspace : QStringLiteral("name:") + workspace);
        prune();
        publish();
        return address;
    }
    Window *window(const QString &address)
    {
        for (Window &w : m_windows) {
            if (w.address == address)
                return &w;
        }
        return nullptr;
    }
    QString workspaceOf(const QString &address) { return window(address) ? window(address)->workspace : QString(); }
    QStringList on(const QString &workspace) const
    {
        QStringList result;
        for (const Window &w : m_windows) {
            if (w.workspace == workspace)
                result << w.address;
        }
        return result;
    }
    // The workspaces that exist now, in the order they were made.
    QStringList workspaces() const
    {
        QStringList names;
        for (const Workspace &w : m_spaces)
            names << w.name;
        return names;
    }
    bool exists(const QString &workspace) const { return idOf(workspace) != 0; }
    // 0 when there is no such workspace.
    int idOf(const QString &workspace) const
    {
        for (const Workspace &w : m_spaces) {
            if (w.name == workspace)
                return w.id;
        }
        return 0;
    }
    // Workspaces deleted since `clearDeleted`, in order (a name twice was made again in between).
    const QStringList &deleted() const { return m_deleted; }
    void clearDeleted() { m_deleted.clear(); }
    // Selectors the fake couldn't make sense of (a bare name where Hyprland wants `name:`).
    const QStringList &rejected() const { return m_rejected; }
    QString focused() const { return m_focused; }
    QString stand(const QString &workspace)
    {
        for (const QString &address : on(workspace)) {
            if (qobject_cast<PageStandIn *>(window(address)->owner))
                return address;
        }
        return {};
    }

    // Maps new stand-ins, applies what the app dispatched, forgets closed windows, and tells the fake hyprctl.
    void step()
    {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *standIn = qobject_cast<PageStandIn *>(widget);
            if (!standIn || !standIn->isVisible())
                continue;
            const bool known = std::any_of(m_windows.begin(), m_windows.end(), [standIn](const Window &w) { return w.owner == standIn; });
            if (!known)
                add(QCoreApplication::applicationPid(), titleOf(standIn), standIn);
        }
        static const QRegularExpression move(QStringLiteral("^dispatch movetoworkspace(silent)? (.*),address:(0x\\w+)$"));
        static const QRegularExpression focus(QStringLiteral("^dispatch workspace (.*)$"));
        static const QRegularExpression focusWindow(QStringLiteral("^dispatch focuswindow address:(0x\\w+)$"));
        static const QRegularExpression unmap(QStringLiteral("^# unmap (0x\\w+)$"));
        QStringList lines;
        for (const QString &call : m_hyprctl.log()) {
            if (call.startsWith(QLatin1String("-j ")))
                continue;
            if (!call.startsWith(QLatin1Char('#')))
                m_batches << call;
            if (call.startsWith(QLatin1String("--batch ")))
                lines << call.mid(8).split(QStringLiteral(" ; "));
            else
                lines << call;
        }
        for (const QString &line : lines) {
            // A widget deleted between two dispatches unmaps there, and an emptied workspace goes at once.
            if (const auto u = unmap.match(line); u.hasMatch()) {
                m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(), [&](const Window &w) { return w.address == u.captured(1); }),
                                m_windows.end());
                prune();
                continue;
            }
            m_history << line;
            if (const auto m = move.match(line); m.hasMatch()) {
                Window *w = window(m.captured(3));
                const QString target = resolve(m.captured(2));
                if (target.isEmpty()) {
                    m_rejected << line;
                } else if (w) {
                    const QString from = w->workspace;
                    w->workspace = target;
                    if (m.captured(1).isEmpty()) {
                        m_active = target;
                        m_focused = w->address;
                    } else if (m_focused == w->address) {
                        // The focused window left silently: focus falls to what's still there.
                        m_focused = on(from).isEmpty() ? QString() : on(from).last();
                    }
                }
            } else if (const auto f = focus.match(line); f.hasMatch()) {
                const QString target = resolve(f.captured(1));
                if (target.isEmpty()) {
                    m_rejected << line;
                } else {
                    m_active = target;
                    focusOn(target);
                }
            } else if (const auto fw = focusWindow.match(line); fw.hasMatch()) {
                if (Window *w = window(fw.captured(1))) {
                    m_active = w->workspace;
                    m_focused = w->address;
                }
            }
            prune();
        }
        m_hyprctl.clearLog();
        // Windows whose widgets are gone are unmapped, and the rest show their widgets' titles.
        m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(), [](const Window &w) { return w.pid == QCoreApplication::applicationPid() && w.owner.isNull(); }),
                        m_windows.end());
        for (Window &w : m_windows) {
            if (w.owner)
                w.title = titleOf(w.owner);
        }
        if (!window(m_focused))
            m_focused = on(m_active).isEmpty() ? QString() : on(m_active).last();
        prune();
        publish();
    }
    // Lets the app's timers run, feeding the world between turns.
    void settle(int turns = 8)
    {
        for (int i = 0; i < turns; ++i) {
            QTest::qWait(120);
            step();
        }
    }
    void publish()
    {
        QJsonArray clients;
        for (const Window &w : m_windows) {
            clients.append(QJsonObject{{"address", w.address},
                                       {"pid", w.pid},
                                       {"class", "io.github.iretonsean.Omastrator"},
                                       {"title", w.title},
                                       {"at", QJsonArray{0, 0}},
                                       {"size", QJsonArray{100, 100}},
                                       {"workspace", QJsonObject{{"id", idOf(w.workspace)}, {"name", w.workspace}}}});
        }
        QJsonArray spaces;
        for (const Workspace &w : m_spaces)
            spaces.append(QJsonObject{{"id", w.id}, {"name", w.name}});
        m_hyprctl.answer(QStringLiteral("clients"), QJsonDocument(clients).toJson());
        m_hyprctl.answer(QStringLiteral("activeworkspace"), QJsonDocument(QJsonObject{{"id", idOf(m_active)}, {"name", m_active}}).toJson());
        m_hyprctl.answer(QStringLiteral("workspaces"), QJsonDocument(spaces).toJson());
    }

private:
    struct Workspace {
        int id;
        QString name;
    };
    static bool isNumber(const QString &text)
    {
        bool ok = false;
        return text.toInt(&ok) > 0 && ok;
    }
    // The title Hyprland is told: the widget's, as the app itself reads it back.
    static QString titleOf(QWidget *widget)
    {
        return widget->windowTitle().replace(QLatin1String("[*]"), widget->isWindowModified() ? QStringLiteral("*") : QString());
    }
    // A selector's workspace, made if it doesn't exist; empty when the selector isn't one Hyprland knows.
    QString resolve(const QString &selector)
    {
        if (selector.startsWith(QLatin1String("name:")))
            return ensure(selector.mid(5), m_nextNamed);
        if (selector.startsWith(QLatin1String("special:")))
            return ensure(selector, m_nextSpecial);
        if (isNumber(selector)) {
            for (const Workspace &w : m_spaces) {
                if (w.id == selector.toInt())
                    return w.name;
            }
            m_spaces.append({selector.toInt(), selector});
            return selector;
        }
        return {};
    }
    QString ensure(const QString &name, int &nextId)
    {
        if (idOf(name) == 0)
            m_spaces.append({nextId--, name});
        return name;
    }
    // Hyprland deletes a workspace that is empty and not focused.
    void prune()
    {
        for (int i = int(m_spaces.size()) - 1; i >= 0; --i) {
            const QString name = m_spaces.at(i).name;
            if (name != m_active && on(name).isEmpty()) {
                m_deleted << name;
                m_spaces.removeAt(i);
            }
        }
    }
    void focusOn(const QString &workspace) { m_focused = on(workspace).isEmpty() ? QString() : on(workspace).last(); }
    // A new window maps on the focused workspace and takes focus.
    QString add(qint64 pid, const QString &title, QWidget *owner)
    {
        const QString address = QStringLiteral("0x%1").arg(0xa00 + m_next++, 0, 16);
        m_windows.append({address, pid, title, m_active, owner, title});
        if (owner)
            QObject::connect(owner, &QObject::destroyed, &m_alive, [hyprctl = &m_hyprctl, address] { hyprctl->note(QStringLiteral("unmap ") + address); });
        m_focused = address;
        publish();
        return address;
    }
    FakeHyprctl &m_hyprctl;
    // Unmap notes stop when the world goes, whatever outlives it.
    QObject m_alive;
    QString m_active;
    QString m_focused;
    QList<Window> m_windows;
    QList<Workspace> m_spaces;
    QStringList m_batches;
    QStringList m_history;
    QStringList m_deleted;
    QStringList m_rejected;
    int m_next = 1;
    int m_nextNamed = -1337;
    int m_nextSpecial = -99;
};
