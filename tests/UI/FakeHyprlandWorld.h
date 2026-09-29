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

// A little Hyprland behind the fake hyprctl: windows on named workspaces. Our own windows appear where the
// focus is when they're shown (as on Wayland); dispatchers in the log are applied to it; `clients` and
// `activeworkspace` are what the app then reads. Only hyprlang's dispatch lines are understood.
class FakeHyprlandWorld {
public:
    struct Window {
        QString address;
        qint64 pid = 0;
        QString title;
        QString workspace;
        QPointer<QWidget> owner;
    };

    explicit FakeHyprlandWorld(FakeHyprctl &hyprctl, const QString &active = QStringLiteral("1")) : m_hyprctl(hyprctl), m_active(active) { publish(); }

    QString active() const { return m_active; }
    // Every dispatch the app has made since `clearHistory`.
    const QStringList &history() const { return m_history; }
    void clearHistory() { m_history.clear(); }
    QList<Window> &windows() { return m_windows; }
    // The editor: mapped on the focused workspace like any window.
    QString addEditor(QWidget &editor)
    {
        return add(QCoreApplication::applicationPid(), editor.windowTitle().replace(QLatin1String("[*]"), QString()), &editor);
    }
    // Someone else's window.
    QString addForeign(const QString &workspace, const QString &title = QStringLiteral("Firefox"))
    {
        const QString address = add(4242, title, nullptr);
        window(address)->workspace = workspace;
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
    QStringList workspaces() const
    {
        QStringList names;
        for (const Window &w : m_windows) {
            if (!names.contains(w.workspace))
                names << w.workspace;
        }
        return names;
    }
    // A workspace that isn't focused and has no windows is gone.
    bool exists(const QString &workspace) const { return workspace == m_active || !on(workspace).isEmpty(); }
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
                add(QCoreApplication::applicationPid(), standIn->firstTitle(), standIn);
        }
        static const QRegularExpression move(QStringLiteral("^dispatch movetoworkspace(silent)? name:(.*),address:(0x\\w+)$"));
        static const QRegularExpression focus(QStringLiteral("^dispatch workspace (.*)$"));
        for (const QString &line : m_hyprctl.dispatches()) {
            m_history << line;
            if (const auto m = move.match(line); m.hasMatch()) {
                if (Window *w = window(m.captured(3)))
                    w->workspace = m.captured(2);
                if (m.captured(1).isEmpty())
                    m_active = m.captured(2);
            } else if (const auto f = focus.match(line); f.hasMatch()) {
                m_active = f.captured(1);
            }
        }
        m_hyprctl.clearLog();
        // Windows whose widgets are gone are unmapped.
        m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(), [](const Window &w) { return w.pid == QCoreApplication::applicationPid() && w.owner.isNull(); }),
                        m_windows.end());
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
                                       {"workspace", QJsonObject{{"id", -99}, {"name", w.workspace}}}});
        }
        m_hyprctl.answer(QStringLiteral("clients"), QJsonDocument(clients).toJson());
        m_hyprctl.answer(QStringLiteral("activeworkspace"), QJsonDocument(QJsonObject{{"id", -99}, {"name", m_active}}).toJson());
        m_hyprctl.answer(QStringLiteral("workspaces"), QJsonDocument(QJsonArray{QJsonObject{{"id", 1}, {"name", m_active}}}).toJson());
    }

private:
    QString add(qint64 pid, const QString &title, QWidget *owner)
    {
        const QString address = QStringLiteral("0x%1").arg(0xa00 + m_next++, 0, 16);
        m_windows.append({address, pid, title, m_active, owner});
        publish();
        return address;
    }
    FakeHyprctl &m_hyprctl;
    QString m_active;
    QList<Window> m_windows;
    QStringList m_history;
    int m_next = 1;
};
