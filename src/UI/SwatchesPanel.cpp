#include "UI/SwatchesPanel.h"
#include "Document/EditorSession.h"
#include "Document/Swatches.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include <QPainterPath>
#include <QAbstractButton>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace {
constexpr int chip = 22;
constexpr int perRow = 10;

class SwatchChip : public QAbstractButton {
public:
    SwatchChip(const Swatch &swatch, QWidget *parent) : QAbstractButton(parent), m_color(swatch.color), m_global(swatch.global)
    {
        setFixedSize(chip, chip);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("%1 (%2)%3\nClick for fill, Shift-click for stroke")
                       .arg(swatch.name, swatch.color.name(), swatch.global ? QStringLiteral(", global") : QString()));
        setAccessibleName(swatch.name);
    }
    Qt::KeyboardModifiers modifiers;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        modifiers = event->modifiers();
        QAbstractButton::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1));
        painter.setBrush(m_color);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 4, 4);
        // Illustrator marks a global swatch with a white corner.
        if (m_global) {
            const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
            QPainterPath corner(box.bottomRight() - QPointF(0, 8));
            corner.lineTo(box.bottomRight());
            corner.lineTo(box.bottomRight() - QPointF(8, 0));
            corner.closeSubpath();
            painter.setPen(QPen(palette().color(QPalette::Mid), 0.75));
            painter.setBrush(Qt::white);
            painter.drawPath(corner);
        }
        if (underMouse()) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        }
    }

private:
    QColor m_color;
    bool m_global = false;
};
}

SwatchesPanel::SwatchesPanel(Swatches &library, std::function<EditorSession &()> session, QWidget *parent)
    : QWidget(parent), m_library(library), m_session(std::move(session)), m_column(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("swatches"));
    setFixedWidth(chip * perRow + 4 * (perRow - 1) + 32);
    m_column->setContentsMargins(16, 14, 16, 14);
    m_column->setSpacing(8);
    connect(&m_library, &Swatches::changed, this, &SwatchesPanel::rebuild);
    rebuild();
}

void SwatchesPanel::rebuild()
{
    while (QLayoutItem *item = m_column->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item->layout();
        delete item;
    }
    if (m_library.groups().empty()) {
        auto *empty = new QLabel(QStringLiteral("No swatches yet. Pick a colour from anywhere on screen with the "
                                                "Capture tab, or load your Omarchy theme's colours."),
                                 this);
        empty->setObjectName(QStringLiteral("swatchesEmpty"));
        empty->setWordWrap(true);
        m_column->addWidget(empty);
        return;
    }
    for (const SwatchGroup &group : m_library.groups()) {
        auto *title = new QLabel(group.name, this);
        title->setObjectName(QStringLiteral("swatchGroupTitle"));
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        title->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(title, &QLabel::customContextMenuRequested, this, [this, name = group.name, title](const QPoint &at) {
            QMenu menu;
            menu.addAction(QStringLiteral("Delete Group"), this, [this, name] { m_library.removeGroup(name); });
            menu.exec(title->mapToGlobal(at));
        });
        m_column->addWidget(title);
        auto *grid = new QWidget(this);
        auto *layout = new QGridLayout(grid);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        for (int index = 0; index < int(group.swatches.size()); ++index) {
            const Swatch &swatch = group.swatches[size_t(index)];
            auto *button = new SwatchChip(swatch, grid);
            button->setObjectName(QStringLiteral("swatch"));
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QAbstractButton::clicked, this, [this, button, swatch] {
                EditorSession &session = m_session();
                if (session.isInteracting())
                    return;
                Paint paint = Paint::solid(swatch.color);
                if (swatch.global)
                    paint.swatchId = swatch.id;
                if (button->modifiers & Qt::ShiftModifier) {
                    StrokeStyle stroke = ShownStyle::stroke(session);
                    stroke.paint = paint.withCompositeOf(stroke.paint);
                    stroke.paint.isHidden = false;
                    stroke.width = std::max(stroke.width, 1.0);
                    session.setStrokeOfSelection(stroke);
                } else {
                    session.setFillOfSelection(paint);
                }
            });
            connect(button, &QWidget::customContextMenuRequested, this, [this, button, swatch, name = group.name, index](const QPoint &at) {
                QMenu menu;
                menu.addAction(QStringLiteral("Edit Color…"), this, [this, swatch, name, index] {
                    ColorPickerSheet::showIn(m_picker, swatch.name, swatch.color,
                                             [this, name, index](const QColor &color) { m_library.setColor(name, index, color); });
                });
                QAction *global = menu.addAction(QStringLiteral("Global"));
                global->setObjectName(QStringLiteral("swatchGlobal"));
                global->setCheckable(true);
                global->setChecked(swatch.global);
                global->setToolTip(QStringLiteral("Objects painted with a global swatch change when it does"));
                connect(global, &QAction::toggled, this, [this, name, index](bool on) { m_library.setGlobal(name, index, on); });
                menu.addSeparator();
                menu.addAction(QStringLiteral("Delete Swatch"), this, [this, name, index] { m_library.remove(name, index); });
                menu.exec(button->mapToGlobal(at));
            });
            layout->addWidget(button, index / perRow, index % perRow);
        }
        m_column->addWidget(grid);
    }
    m_column->addStretch();
}
