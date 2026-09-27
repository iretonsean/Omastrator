#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include "UI/PanelIcons.h"
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

QLabel *text(const QString &words, int pixels, QFont::Weight weight, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    return label;
}
}

LayersPanel::LayersPanel(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_count(text(QStringLiteral("0"), 11, QFont::Normal, this)), m_list(new NativeLayerList(session, this))
{
    setObjectName(QStringLiteral("layersPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *heading = new QHBoxLayout;
    heading->setContentsMargins(18, 14, 18, 12);
    heading->addWidget(text(QStringLiteral("Layers"), 12, QFont::DemiBold, this));
    heading->addStretch(1);
    m_count->setObjectName(QStringLiteral("layerCount"));
    m_count->setForegroundRole(QPalette::PlaceholderText);
    heading->addWidget(m_count);
    column->addLayout(heading);
    column->addWidget(divider(this));
    column->addWidget(m_list, 1);
    column->addWidget(divider(this));
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(8, 2, 8, 2);
    footer->setSpacing(0);
    m_newLayer = footerButton(QStringLiteral("newLayer"), QStringLiteral("New layer"), [this] { m_session.addLayer(); });
    footer->addWidget(m_newLayer);
    footer->addStretch(1);
    m_delete = footerButton(QStringLiteral("deleteLayer"), QStringLiteral("Delete selection"), [this] { deleteTarget(); });
    footer->addWidget(m_delete);
    column->addLayout(footer);
    applyIcons();
    connect(&m_session, &EditorSession::changed, this, &LayersPanel::synchronize);
    synchronize();
}

void LayersPanel::deleteTarget()
{
    if (m_session.hasSelection())
        m_session.deleteSelection();
    else if (const std::optional<QUuid> layer = m_session.activeLayer())
        m_session.deleteObjects({*layer});
}

// The icons take the palette's ink, again after each theme.
void LayersPanel::applyIcons()
{
    const QColor ink = palette().color(QPalette::PlaceholderText);
    m_newLayer->setIcon(PanelIcons::pixmap(PanelIcon::newLayer, 16, ink, devicePixelRatio()));
    m_delete->setIcon(PanelIcons::pixmap(PanelIcon::trash, 16, ink, devicePixelRatio()));
}

void LayersPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyIcons();
}

QToolButton *LayersPanel::footerButton(const QString &name, const QString &tip, const std::function<void()> &run)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setAccessibleName(tip);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setFixedSize(32, 32);
    connect(button, &QToolButton::clicked, this, run);
    return button;
}

void LayersPanel::synchronize()
{
    const std::optional<VectorDocument> &document = m_session.document();
    m_count->setText(QString::number(document ? document->layers().size() : 0));
    m_newLayer->setEnabled(document.has_value());
    m_delete->setEnabled(document.has_value());
    const QString what = m_session.hasSelection() ? QStringLiteral("Delete selection") : QStringLiteral("Delete layer");
    m_delete->setToolTip(what);
    m_delete->setAccessibleName(what);
}
