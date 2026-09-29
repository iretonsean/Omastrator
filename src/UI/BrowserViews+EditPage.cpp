#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QJsonArray>

// Edit Page's side of the host (docs/LIVE-IN-FRAME.md, section 3): Live runs on the frame's tab, its overlay reports
// the boxes, and this hands them to the canvas to draw.

QString BrowserViews::beginEditPage(const QUuid &frame)
{
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return QStringLiteral("That isn't a Browser View.");
    if (object->browser->url.isEmpty())
        return QStringLiteral("Type an address first; there's no page to edit.");
    LiveFrames *live = LiveFrames::of(m_session);
    // Once per session; the connection lives as long as the frames do.
    connect(live, &LiveFrames::changed, this, &BrowserViews::onLiveChanged, Qt::UniqueConnection);
    if (!live->active(frame)) {
        const QString failure = live->start(frame);
        if (!failure.isEmpty())
            return failure;
    }
    live->setPageEditing(frame, true);
    return {};
}

void BrowserViews::endEditPage(const QUuid &frame)
{
    if (LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly); live && live->active(frame))
        live->setPageEditing(frame, false);
}

void BrowserViews::onLiveChanged(const QUuid &frame)
{
    scheduleRepaint(frame);
    LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (m_canvas && m_canvas->editPageFrame() == frame && (!live || !live->active(frame)))
        m_canvas->leaveEditPage();
}

namespace {
BrowserViewHost::EditBox boxOf(const QJsonObject &each)
{
    const QJsonObject rect = each["rect"].toObject();
    BrowserViewHost::EditBox box;
    box.rect = QRectF(rect["x"].toDouble(), rect["y"].toDouble(), rect["width"].toDouble(), rect["height"].toDouble());
    box.label = QStringLiteral("%1  %2 × %3").arg(each["tag"].toString()).arg(qRound(box.rect.width())).arg(qRound(box.rect.height()));
    return box;
}
}

BrowserViewHost::EditBoxes BrowserViews::editBoxes(const QUuid &frame) const
{
    EditBoxes boxes;
    const LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (!live || !live->active(frame))
        return boxes;
    const QJsonObject geometry = live->snapshot(frame).geometry;
    if (geometry["hover"].isObject())
        boxes.hover = boxOf(geometry["hover"].toObject());
    for (const QJsonValue &each : geometry["selection"].toArray())
        boxes.selection.append(boxOf(each.toObject()));
    return boxes;
}
