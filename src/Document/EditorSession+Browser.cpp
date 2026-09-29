#include "Document/EditorSession.h"
#include "Document/BrowserAddress.h"

void EditorSession::setBrowserLocation(const QUuid &frame, const QUrl &url, QPointF scroll)
{
    VectorObject *object = m_document ? m_document->find(frame) : nullptr;
    if (!object || !object->browser || !BrowserAddress::allowed(url))
        return;
    const QUrl before = object->browser->url;
    if (before == url && object->browser->scroll == scroll)
        return;
    object->browser->url = url;
    object->browser->scroll = scroll;
    // A preview in flight would put the old address back when it ends.
    if (m_interaction) {
        for (VectorDocument *held : {&m_interaction->before, &m_interaction->base}) {
            if (VectorObject *recorded = held->find(frame); recorded && recorded->browser) {
                recorded->browser->url = url;
                recorded->browser->scroll = scroll;
            }
        }
    }
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
    // The picture is view state, not an edit: every snapshot and a preview in flight share this one image, so history
    // holds a single picture and an undo or the end of a preview never brings an old one back.
    const auto share = [&](VectorDocument &document) {
        VectorObject *recorded = document.find(frame);
        if (recorded && recorded->browser)
            recorded->browser->picture = picture;
    };
    if (m_interaction) {
        share(m_interaction->before);
        share(m_interaction->base);
    }
    m_history.mapDocuments(share);
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

void EditorSession::beginPreview(const QString &name)
{
    beginInteraction(name);
    if (m_interaction)
        m_interaction->discard = true;
}

void EditorSession::previewFrameBox(const QUuid &frame, const QRectF &box)
{
    if (!m_document || !m_interaction || !m_interaction->base.find(frame))
        return;
    VectorDocument document = m_interaction->base;
    document.resizeFrame(frame, box, true);
    m_document = std::move(document);
    notify();
}

QRectF EditorSession::designBox(const QUuid &frame) const
{
    const VectorDocument *document = m_interaction ? &m_interaction->before : m_document ? &*m_document : nullptr;
    return document && document->find(frame) ? document->bounds(frame) : QRectF();
}

void EditorSession::setDesignBox(const QUuid &frame, const QRectF &box)
{
    const VectorObject *object = m_document ? m_document->find(frame) : nullptr;
    if (!object || !object->browser || box.width() < 1 || box.height() < 1)
        return;
    if (m_interaction && m_interaction->discard)
        cancelInteraction();
    if (m_document->bounds(frame) == box)
        return;
    edit(QStringLiteral("Design Width"), [&](VectorDocument &document) { document.resizeFrame(frame, box); });
}

void EditorSession::setPreviewAsDesignWidth()
{
    const std::optional<QUuid> frame = selectedBrowserView();
    if (frame && isPreviewOnly())
        setDesignBox(*frame, m_document->bounds(*frame));
}

void EditorSession::setFixedWhilePreviewing(bool fixed)
{
    if (!m_document)
        return;
    const std::vector<QUuid> ids = m_selection;
    edit(QStringLiteral("Fixed While Previewing"), [&](VectorDocument &document) {
        for (const QUuid &id : ids) {
            if (VectorObject *object = document.find(id))
                object->layout.previewRule = fixed ? PreviewRule::fixed : PreviewRule::constraints;
        }
    });
}
