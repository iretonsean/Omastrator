#include "ContentView.h"
#include <QHBoxLayout>
#include <QLocale>

namespace {
QLabel *statusText(QLabel *label, const QString &name)
{
    label->setObjectName(name);
    QFont font = label->font();
    font.setPixelSize(11);
    label->setFont(font);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

QWidget *ContentView::makeStatus()
{
    statusText(m_zoom, QStringLiteral("zoomStatus"))->setFixedWidth(62);
    statusText(m_pointer, QStringLiteral("pointerStatus"))->setFixedWidth(150);
    statusText(m_artboard, QStringLiteral("artboardStatus"));
    statusText(m_selection, QStringLiteral("selectionStatus"));
    // The hint takes what is left, and shrinks first.
    statusText(m_hint, QStringLiteral("hintStatus"));
    m_hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *status = new QWidget(this);
    status->setObjectName(QStringLiteral("statusBar"));
    status->setFixedHeight(30);
    auto *row = new QHBoxLayout(status);
    row->setContentsMargins(18, 0, 18, 0);
    row->setSpacing(16);
    row->addWidget(m_zoom);
    row->addWidget(m_pointer);
    row->addWidget(m_artboard);
    row->addWidget(m_selection);
    row->addWidget(m_hint, 1);
    return status;
}

QString ContentView::percent(double zoom)
{
    QString number = QLocale(QLocale::English, QLocale::UnitedStates).toString(zoom * 100, 'f', 1);
    if (number.endsWith(QLatin1String(".0")))
        number.chop(2);
    return number + QLatin1Char('%');
}

void ContentView::showPointer(std::optional<QPointF> point)
{
    m_pointer->setText(point ? QStringLiteral("X %1  Y %2 pt").arg(point->x(), 0, 'f', 1).arg(point->y(), 0, 'f', 1) : QStringLiteral("X –  Y –"));
}

QString ContentView::hint(Tool tool)
{
    switch (tool) {
    case Tool::select: return QStringLiteral("Click to select · Shift-click adds · Drag to move · Handles scale · Alt-drag duplicates");
    case Tool::directSelect: return QStringLiteral("Click anchors to pick them · Drag anchors and handles · Delete removes anchors");
    case Tool::pen: return QStringLiteral("Click for corners · Drag for curves · Close on the first anchor · Return or Escape ends");
    case Tool::pencil: return QStringLiteral("Drag to draw · Finish near the start to close");
    case Tool::text: return QStringLiteral("Click to type · Click text to edit · Escape finishes");
    case Tool::line: return QStringLiteral("Drag to draw a line · Shift 45°");
    case Tool::rectangle:
    case Tool::roundedRectangle: return QStringLiteral("Drag to draw · Shift square · Alt from center");
    case Tool::ellipse: return QStringLiteral("Drag to draw · Shift circle · Alt from center");
    case Tool::polygon:
    case Tool::star: return QStringLiteral("Drag from the center · Shift keeps it upright");
    case Tool::rotate: return QStringLiteral("Drag to rotate the selection · Shift 45° steps");
    case Tool::scale: return QStringLiteral("Drag to scale the selection · Shift keeps proportions");
    case Tool::eyedropper: return QStringLiteral("Click an object to take its fill and stroke");
    case Tool::hand: return QStringLiteral("Drag to pan · Space pans from any tool");
    case Tool::zoom: return QStringLiteral("Click to zoom in · Alt-click to zoom out");
    }
    return QString();
}
