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
