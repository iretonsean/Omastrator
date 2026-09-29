#include "Document/EditorSession.h"
#include "Live/Breakpoints.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QPointer>

// The breakpoint buttons' widths (docs/BROWSER-VIEW.md, section 7).

QList<int> BrowserViews::breakpoints(const QUuid &frame) const
{
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser || !owned(frame))
        return Breakpoints::defaults();
    return m_breakpoints.value(ProjectRegistry::originOf(object->browser->url), Breakpoints::defaults());
}

void BrowserViews::scanBreakpoints(const QUuid &frame)
{
    const auto found = m_entries.constFind(frame);
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (found == m_entries.constEnd() || !object || !object->browser || !owned(frame))
        return;
    const QString origin = ProjectRegistry::originOf(object->browser->url);
    // The reply comes on the pool's thread, possibly after this is gone.
    const QPointer<BrowserViews> guard(this);
    BrowserViews::pool()->call(found->key, QStringLiteral("Runtime.evaluate"),
                               {{"expression", Breakpoints::scanScript()}, {"returnByValue", true}},
                               [guard, origin, frame](const QJsonObject &result, const QString &error) {
        if (!error.isEmpty())
            return;
        const QJsonObject scan = QJsonDocument::fromJson(result["result"]["value"].toString().toUtf8()).object();
        if (scan.isEmpty())
            return;
        const QList<int> widths = Breakpoints::fromScan(scan);
        QMetaObject::invokeMethod(qApp, [guard, origin, frame, widths] {
            if (!guard || guard->m_breakpoints.value(origin) == widths)
                return;
            guard->m_breakpoints.insert(origin, widths);
            emit guard->frameChanged(frame);
            guard->scheduleRepaint(frame);
        }, Qt::QueuedConnection);
    });
}
