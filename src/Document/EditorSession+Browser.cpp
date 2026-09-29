#include "Document/EditorSession.h"

void EditorSession::setBrowserLocation(const QUuid &frame, const QUrl &url, QPointF scroll)
{
    VectorObject *object = m_document ? m_document->find(frame) : nullptr;
    if (!object || !object->browser)
        return;
    const QUrl before = object->browser->url;
    if (before == url && object->browser->scroll == scroll)
        return;
    object->browser->url = url;
    object->browser->scroll = scroll;
    // Scrolling alone is view state, like the picture: saved with the file, never a reason to ask about saving.
    if (before != url) {
        m_history.mapDocuments([&](VectorDocument &document) {
            VectorObject *recorded = document.find(frame);
            if (recorded && recorded->browser && recorded->browser->url == before)
                recorded->browser->url = url;
        });
        m_history.markUnsaved();
        notify(false);
    }
}

void EditorSession::setBrowserPicture(const QUuid &frame, const QImage &picture)
{
    VectorObject *object = m_document ? m_document->find(frame) : nullptr;
    if (!object || !object->browser)
        return;
    object->browser->picture = picture;
}

QUuid EditorSession::addBrowserView(const QRectF &rect, const QUrl &url)
{
    VectorObject frame = VectorObject::frame(rect, m_document ? m_document->uniqueName(QStringLiteral("Browser View")) : QStringLiteral("Browser View"));
    frame.browser = BrowserView{.url = url, .scroll = {}, .picture = {}};
    return addFrameObject(frame, QStringLiteral("Draw Browser View"));
}

void EditorSession::setBrowserUrl(const QUuid &frame, const QUrl &url)
{
    const VectorObject *object = m_document ? m_document->find(frame) : nullptr;
    if (!object || !object->browser || object->browser->url == url)
        return;
    edit(QStringLiteral("Change URL"), [&](VectorDocument &document) {
        document.find(frame)->browser->url = url;
        document.find(frame)->browser->scroll = {};
    });
}

std::optional<QUuid> EditorSession::selectedBrowserView() const
{
    if (!m_document || m_selection.size() != 1)
        return std::nullopt;
    const VectorObject *object = m_document->find(m_selection.front());
    return object && object->browser ? std::optional(object->id) : std::nullopt;
}
