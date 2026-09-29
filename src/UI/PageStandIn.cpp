#include "UI/PageStandIn.h"
#include <QCloseEvent>
#include <QPaintEvent>
#include <QPainter>

PageStandIn::PageStandIn(int number) : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_number(number)
{
    setObjectName(QStringLiteral("pageStandIn"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    // Closing the editor while a stand-in is left must still let the app quit.
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    resize(1280, 820);
    setWindowTitle(firstTitle());
}

QString PageStandIn::firstTitle() const
{
    return QStringLiteral("omastrator-standin-%1").arg(m_number);
}

void PageStandIn::setLabel(const QString &label)
{
    m_label = label;
    setWindowTitle(label + QStringLiteral(" — Omastrator"));
    update();
}

void PageStandIn::setPicture(const QImage &picture)
{
    m_picture = picture;
    update();
}

void PageStandIn::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    if (m_picture.isNull()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, m_label);
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSize fitted = m_picture.size().scaled(size(), Qt::KeepAspectRatio);
    painter.drawImage(QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted), m_picture);
}

void PageStandIn::closeEvent(QCloseEvent *event)
{
    event->accept();
    emit closedByUser(this);
}
