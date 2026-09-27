#include "UI/OpenTypePopover.h"
#include "Document/FontFeatures.h"
#include "UI/ToolHeaderStyle.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// On across every stretch shown, off across all of them, or mixed.
Qt::CheckState stateOf(const std::vector<TextContent> &texts, const QString &tag)
{
    int on = 0;
    for (const TextContent &text : texts)
        on += FontFeatures::isOn(text.features, tag) ? 1 : 0;
    return on == 0 ? Qt::Unchecked : on == int(texts.size()) ? Qt::Checked : Qt::PartiallyChecked;
}

// Lining or oldstyle, tabular or proportional: one of a pair, or the font's default.
int pairChoice(const std::vector<TextContent> &texts, const QString &first, const QString &second)
{
    const auto choice = [&](const TextContent &text) {
        return FontFeatures::isOn(text.features, first) ? 1 : FontFeatures::isOn(text.features, second) ? 2 : 0;
    };
    const int shown = choice(texts.front());
    for (const TextContent &text : texts) {
        if (choice(text) != shown)
            return -1;
    }
    return shown;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(ToolHeaderStyle::controlFont());
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

namespace OpenTypePopover {
QFrame *show(EditorSession &session, QWidget *anchor)
{
    auto *popover = new QFrame(anchor, Qt::Popup);
    popover->setObjectName(QStringLiteral("openTypePopover"));
    popover->setAttribute(Qt::WA_DeleteOnClose);
    popover->setFrameShape(QFrame::StyledPanel);
    popover->setAccessibleName(QStringLiteral("OpenType features"));
    auto *column = new QVBoxLayout(popover);
    column->setContentsMargins(12, 10, 12, 12);
    column->setSpacing(6);
    column->addWidget(caption(QStringLiteral("OpenType"), popover));
    const QPointer<EditorSession> watched(&session);
    const auto edit = [watched](const std::function<void(std::map<QString, int> &)> &change) {
        if (watched)
            watched->updateText([&change](TextContent &text) { change(text.features); }, QStringLiteral("OpenType Features"));
    };
    std::vector<std::pair<QCheckBox *, QString>> boxes;
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    int row = 0, column_ = 0;
    for (const FontFeatures::Feature &feature : FontFeatures::offered()) {
        const QString tag = QString::fromLatin1(feature.tag);
        // Figures are two either-or choices below.
        if (tag == QLatin1String("lnum") || tag == QLatin1String("onum") || tag == QLatin1String("tnum") || tag == QLatin1String("pnum"))
            continue;
        auto *box = new QCheckBox(QString::fromLatin1(feature.name), popover);
        box->setObjectName(QStringLiteral("openType:") + tag);
        box->setFont(ToolHeaderStyle::controlFont());
        QObject::connect(box, &QCheckBox::clicked, popover, [edit, tag](bool on) { edit([&](std::map<QString, int> &features) { FontFeatures::set(features, tag, on); }); });
        grid->addWidget(box, row, column_);
        boxes.push_back({box, tag});
        if (++column_ == 2) {
            column_ = 0;
            ++row;
        }
    }
    column->addLayout(grid);
    auto *figures = new QGridLayout;
    figures->setHorizontalSpacing(8);
    const auto pair = [&](const QString &name, const QString &label, const QStringList &items, const QString &first, const QString &second, int at) {
        auto *combo = new QComboBox(popover);
        combo->setObjectName(name);
        combo->setAccessibleName(label);
        combo->setFont(ToolHeaderStyle::controlFont());
        combo->addItems(items);
        QObject::connect(combo, &QComboBox::activated, popover, [edit, first, second](int index) {
            edit([&](std::map<QString, int> &features) {
                FontFeatures::set(features, first, index == 1);
                FontFeatures::set(features, second, index == 2);
            });
        });
        figures->addWidget(caption(label, popover), at, 0);
        figures->addWidget(combo, at, 1);
        return combo;
    };
    QComboBox *style = pair(QStringLiteral("openTypeFigures"), QStringLiteral("Figures"),
                            {QStringLiteral("Default"), QStringLiteral("Lining"), QStringLiteral("Oldstyle")}, QStringLiteral("lnum"), QStringLiteral("onum"), 0);
    QComboBox *spacing = pair(QStringLiteral("openTypeSpacing"), QStringLiteral("Spacing"),
                              {QStringLiteral("Default"), QStringLiteral("Tabular"), QStringLiteral("Proportional")}, QStringLiteral("tnum"),
                              QStringLiteral("pnum"), 1);
    figures->setColumnStretch(1, 1);
    column->addLayout(figures);
    column->addWidget(caption(QStringLiteral("Stylistic sets"), popover));
    auto *sets = new QGridLayout;
    sets->setSpacing(2);
    std::vector<std::pair<QToolButton *, QString>> setButtons;
    const QStringList tags = FontFeatures::stylisticSets();
    for (int index = 0; index < tags.size(); ++index) {
        auto *button = new QToolButton(popover);
        button->setObjectName(QStringLiteral("openType:") + tags[index]);
        button->setText(QString::number(index + 1));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFixedSize(26, 22);
        button->setFont(ToolHeaderStyle::controlFont());
        button->setToolTip(QStringLiteral("Stylistic set %1").arg(index + 1));
        button->setAccessibleName(button->toolTip());
        const QString tag = tags[index];
        QObject::connect(button, &QToolButton::clicked, popover, [edit, tag](bool on) {
            edit([&](std::map<QString, int> &features) { FontFeatures::set(features, tag, on); });
        });
        sets->addWidget(button, index / 10, index % 10);
        setButtons.push_back({button, tag});
    }
    column->addLayout(sets);
    // Follows the selection and undo while it's open.
    const auto refresh = [watched, boxes, style, spacing, setButtons] {
        if (!watched)
            return;
        const std::vector<TextContent> texts = watched->shownTexts();
        const QSet<QString> supported = FontFeatures::supported(texts.front().character().font(1));
        for (const auto &[box, tag] : boxes) {
            const Qt::CheckState state = stateOf(texts, tag);
            box->setTristate(state == Qt::PartiallyChecked);
            box->setCheckState(state);
            box->setEnabled(supported.contains(tag));
        }
        style->setCurrentIndex(pairChoice(texts, QStringLiteral("lnum"), QStringLiteral("onum")));
        style->setEnabled(supported.contains(QStringLiteral("lnum")) || supported.contains(QStringLiteral("onum")));
        spacing->setCurrentIndex(pairChoice(texts, QStringLiteral("tnum"), QStringLiteral("pnum")));
        spacing->setEnabled(supported.contains(QStringLiteral("tnum")) || supported.contains(QStringLiteral("pnum")));
        for (const auto &[button, tag] : setButtons) {
            button->setChecked(stateOf(texts, tag) == Qt::Checked);
            button->setEnabled(supported.contains(tag));
        }
    };
    QObject::connect(&session, &EditorSession::changed, popover, refresh);
    refresh();
    popover->adjustSize();
    popover->move(anchor->mapToGlobal(QPoint(anchor->width() - popover->width(), anchor->height() + 4)));
    popover->show();
    return popover;
}
}
