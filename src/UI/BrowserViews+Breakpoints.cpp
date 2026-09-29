#include "Document/EditorSession.h"
#include "Live/Breakpoints.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include <QJsonDocument>

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
    BrowserViews::pool()->call(found->key, QStringLiteral("Runtime.evaluate"),
                               {{"expression", Breakpoints::scanScript()}, {"returnByValue", true}},
                               [this, origin, frame](const QJsonObject &result, const QString &error) {
        if (!error.isEmpty())
            return;
        const QJsonObject scan = QJsonDocument::fromJson(result["result"]["value"].toString().toUtf8()).object();
        if (scan.isEmpty())
            return;
        const QList<int> widths = Breakpoints::fromScan(scan);
        QMetaObject::invokeMethod(this, [this, origin, frame, widths] {
            if (m_breakpoints.value(origin) == widths)
                return;
            m_breakpoints.insert(origin, widths);
            emit frameChanged(frame);
            scheduleRepaint(frame);
        }, Qt::QueuedConnection);
    });
}
