#include "UI/CloudBadge.h"
#include "Cloud/CloudProviders.h"
#include <QPainter>
#include <QPixmap>

namespace CloudBadge {
QIcon icon(const QString &type)
{
    const CloudProvider provider = CloudProviders::forType(type);
    QIcon made;
    for (const int size : {16, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(provider.color);
        painter.drawRoundedRect(QRectF(0, 0, size, size), size * 0.25, size * 0.25);
        QFont font;
        font.setPixelSize(std::max(6, int(size * (provider.badge.size() > 1 ? 0.42 : 0.6))));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, provider.badge);
        made.addPixmap(pixmap);
    }
    return made;
}
}
