#include "Canvas/EditorCanvasState.h"
#include <QPainter>

void EditorCanvas::setBrowserViewHost(BrowserViewHost *host)
{
    if (m_state->browserHost == host)
        return;
    m_state->browserHost = host;
    update();
}

BrowserViewHost *EditorCanvas::browserViewHost() const
{
    return m_state->browserHost;
}

QTransform EditorCanvas::documentToView() const
{
    return m_state->documentToView();
}

void EditorCanvas::State::drawBrowserMessages(QPainter &painter) const
{
    if (!browserHost || !session.hasDocument())
        return;
    const VectorDocument &document = *session.document();
    QFont font = canvas.font();
    font.setPixelSize(12);
    const QFontMetricsF metrics(font);
    painter.save();
    painter.setFont(font);
    for (const VectorObject &object : document.objects) {
        if (!object.showsPage() || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        const QString message = browserHost->message(object.id);
        if (message.isEmpty())
            continue;
        const QRectF frame = documentToView().mapRect(document.bounds(object.id));
        if (frame.width() < 40)
            continue;
        // A bar across the frame's middle, in the panel's own colours so it reads on any page.
        const QString text = metrics.elidedText(message, Qt::ElideRight, frame.width() - 24);
        QRectF bar(0, 0, metrics.horizontalAdvance(text) + 20, metrics.height() + 10);
        bar.moveCenter(frame.center());
        painter.setPen(Qt::NoPen);
        painter.setBrush(canvas.palette().color(QPalette::Window));
        painter.drawRoundedRect(bar, 6, 6);
        painter.setPen(canvas.palette().color(QPalette::WindowText));
        painter.drawText(bar, Qt::AlignCenter, text);
    }
    painter.restore();
}
