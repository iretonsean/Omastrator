#include "UI/DesktopLookPanel.h"
#include <QCloseEvent>
#include "System/DesktopLook.h"
#include "UI/DesignController.h"
#include "UI/NumberField.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
std::function<QColor(const QString &, const QColor &)> &colorResponder()
{
    static std::function<QColor(const QString &, const QColor &)> responder;
    return responder;
}

QString hexOf(const QColor &color)
{
    QString text = color.name(QColor::HexRgb);
    if (color.alpha() != 255)
        text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    return text;
}

const QStringList sectionNames{"windows", "bar", "font", "wallpaper", "colours", "app"};
const QStringList barSections{"left", "center", "right"};

QLabel *wrapped(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}
}

void DesktopLookPanel::closeEvent(QCloseEvent *event)
{
    QJsonObject ignored;
    m_controller.look({{"op", "handles"}, {"on", false}}, ignored);
    m_controller.look({{"op", "discard"}}, ignored);
    QWidget::closeEvent(event);
}

DesktopLookPanel::DesktopLookPanel(DesignController &controller, QWidget *parent) : QWidget(parent, Qt::Window), m_controller(controller)
{
    setWindowTitle(QStringLiteral("Desktop Look"));
    setObjectName(QStringLiteral("desktopLookPanel"));
    resize(460, 560);
    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(windowsPage(), QStringLiteral("Windows"));
    m_tabs->addTab(barPage(), QStringLiteral("Bar"));
    m_tabs->addTab(fontPage(), QStringLiteral("Font"));
    m_tabs->addTab(wallpaperPage(), QStringLiteral("Wallpaper"));
    m_tabs->addTab(coloursPage(), QStringLiteral("Colours"));
    m_tabs->addTab(appPage(), QStringLiteral("App"));
    layout->addWidget(m_tabs, 1);
    // The overlay shows gap handles while the Windows tab is up.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        QJsonObject ignored;
        m_controller.look({{"op", "handles"}, {"on", index == 0}}, ignored);
    });

    m_note = wrapped(QString(), this);
    m_note->setObjectName(QStringLiteral("lookNote"));
    layout->addWidget(m_note);
    auto *actions = new QHBoxLayout;
    auto *discard = new QPushButton(QStringLiteral("Discard Preview"), this);
    auto *save = new QPushButton(QStringLiteral("Save to Desktop…"), this);
    save->setObjectName(QStringLiteral("lookSave"));
    connect(discard, &QPushButton::clicked, this, &DesktopLookPanel::discard);
    connect(save, &QPushButton::clicked, this, &DesktopLookPanel::save);
    actions->addWidget(discard);
    actions->addStretch(1);
    actions->addWidget(save);
    layout->addLayout(actions);
    auto *past = new QHBoxLayout;
    m_history = new QComboBox(this);
    m_history->setObjectName(QStringLiteral("lookHistory"));
    auto *revert = new QPushButton(QStringLiteral("Revert…"), this);
    connect(revert, &QPushButton::clicked, this, &DesktopLookPanel::revertSelected);
    past->addWidget(new QLabel(QStringLiteral("History"), this));
    past->addWidget(m_history, 1);
    past->addWidget(revert);
    layout->addLayout(past);

    m_barTimer.setSingleShot(true);
    m_barTimer.setInterval(150);
    connect(&m_barTimer, &QTimer::timeout, this, &DesktopLookPanel::barOrderChanged);
    connect(&m_controller, &DesignController::lookChanged, this, &DesktopLookPanel::reload);
}

void DesktopLookPanel::setColorResponder(std::function<QColor(const QString &, const QColor &)> responder)
{
    colorResponder() = std::move(responder);
}

QString DesktopLookPanel::run(const QString &method, const QJsonObject &params, QJsonObject *result)
{
    QJsonObject answer;
    m_lastError = method == QLatin1String("restyle") ? m_controller.restyle(params, answer) : m_controller.look(params, answer);
    if (result)
        *result = answer;
    if (!m_lastError.isEmpty())
        m_note->setText(m_lastError);
    return m_lastError;
}

void DesktopLookPanel::showSection(const QString &section)
{
    reload();
    const qsizetype index = sectionNames.indexOf(section);
    if (index >= 0)
        m_tabs->setCurrentIndex(int(index));
    show();
    raise();
    activateWindow();
}

NumberField *DesktopLookPanel::numberField(const QString &key, const QString &label, double minimum, double maximum, QWidget *parent)
{
    auto *field = new NumberField(label, QStringLiteral("px"), [this, key](double value) {
        if (!m_filling)
            preview({{key, int(qRound(value))}});
    }, parent);
    field->minimum = minimum;
    field->maximum = maximum;
    field->setObjectName(key);
    m_fields.insert(key, field);
    return field;
}

QPushButton *DesktopLookPanel::swatch(const QString &key, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(key);
    button->setFixedSize(52, 22);
    button->setAccessibleName(key);
    connect(button, &QPushButton::clicked, this, [this, key] { pickColor(key); });
    m_colors.insert(key, button);
    return button;
}

void DesktopLookPanel::setSwatch(QPushButton *button, const QColor &color)
{
    button->setProperty("color", color);
    button->setToolTip(color.isValid() ? hexOf(color) : QStringLiteral("Not set"));
    button->setStyleSheet(color.isValid() ? QStringLiteral("QPushButton { background: %1; border: 1px solid palette(mid); border-radius: 4px; }").arg(color.name(QColor::HexArgb))
                                          : QString());
}

QWidget *DesktopLookPanel::windowsPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->addRow(wrapped(QStringLiteral("Drag a label to scrub. Hyprland shows each change at once; on the overlay, drag a window's edge to set its gap."), page));
    form->addRow(numberField(QStringLiteral("gapsIn"), QStringLiteral("Inner gap"), 0, 200, page));
    form->addRow(numberField(QStringLiteral("gapsOut"), QStringLiteral("Outer gap"), 0, 200, page));
    form->addRow(numberField(QStringLiteral("borderSize"), QStringLiteral("Border"), 0, 40, page));
    form->addRow(numberField(QStringLiteral("rounding"), QStringLiteral("Corner radius"), 0, 100, page));
    form->addRow(QStringLiteral("Active border"), swatch(QStringLiteral("activeBorder"), page));
    form->addRow(QStringLiteral("Inactive border"), swatch(QStringLiteral("inactiveBorder"), page));
    return page;
}

QWidget *DesktopLookPanel::barPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;
    m_barPosition = new QComboBox(page);
    m_barPosition->setObjectName(QStringLiteral("barPosition"));
    m_barPosition->addItems({QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right")});
    connect(m_barPosition, &QComboBox::currentTextChanged, this, [this](const QString &position) {
        if (!m_filling)
            preview({{"barPosition", position}});
    });
    form->addRow(QStringLiteral("Position"), m_barPosition);
    m_barTransparent = new QCheckBox(QStringLiteral("Transparent"), page);
    connect(m_barTransparent, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_filling)
            preview({{"barTransparent", on}});
    });
    form->addRow(QString(), m_barTransparent);
    form->addRow(numberField(QStringLiteral("barHeight"), QStringLiteral("Height"), 12, 120, page));
    form->addRow(QStringLiteral("Background"), swatch(QStringLiteral("barBackground"), page));
    form->addRow(QStringLiteral("Text"), swatch(QStringLiteral("barText"), page));
    layout->addLayout(form);
    layout->addWidget(wrapped(QStringLiteral("Widgets: drag to reorder, or between Left, Center and Right."), page));
    auto *lists = new QHBoxLayout;
    for (int section = 0; section < 3; ++section) {
        auto *column = new QVBoxLayout;
        column->addWidget(new QLabel(QStringList{"Left", "Center", "Right"}[section], page));
        auto *list = new QListWidget(page);
        list->setObjectName(QStringLiteral("bar-") + barSections[section]);
        list->setDragDropMode(QAbstractItemView::DragDrop);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        for (auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
            connect(list->model(), signal, this, [this] {
                if (!m_filling)
                    m_barTimer.start();
            });
        connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
            if (!m_filling)
                m_barTimer.start();
        });
        m_barLists[size_t(section)] = list;
        column->addWidget(list);
        lists->addLayout(column);
    }
    layout->addLayout(lists, 1);
    return page;
}

QWidget *DesktopLookPanel::fontPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_font = new QComboBox(page);
    m_font->setObjectName(QStringLiteral("font"));
    m_font->setEditable(true);
    connect(m_font, &QComboBox::activated, this, [this](int) {
        if (!m_filling && !m_font->currentText().trimmed().isEmpty())
            preview({{"font", m_font->currentText().trimmed()}});
    });
    form->addRow(QStringLiteral("Font"), m_font);
    form->addRow(numberField(QStringLiteral("textSize"), QStringLiteral("Text size"), 6, 48, page));
    form->addRow(wrapped(QStringLiteral("The font is fontconfig's monospace: the bar, terminals and Qt apps. It changes when saved, through omarchy font set."), page));
    return page;
}

QWidget *DesktopLookPanel::wallpaperPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_wallpaper = wrapped(QString(), page);
    layout->addWidget(m_wallpaper);
    auto *choose = new QPushButton(QStringLiteral("Choose Image…"), page);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString image = QFileDialog::getOpenFileName(this, QStringLiteral("Wallpaper"), QFileInfo(m_look["wallpaper"].toString()).absolutePath(),
                                                           QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
        if (!image.isEmpty())
            preview({{"wallpaper", image}});
    });
    auto *artboard = new QPushButton(QStringLiteral("Use Current Artboard"), page);
    connect(artboard, &QPushButton::clicked, this, [this] { run(QStringLiteral("look"), {{"op", "wallpaperFromArtboard"}}); });
    layout->addWidget(choose);
    layout->addWidget(artboard);
    layout->addStretch(1);
    return page;
}

QWidget *DesktopLookPanel::coloursPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(wrapped(QStringLiteral("The current Omarchy theme's colours. The bar and panels show a change at once; the rest follows when saved."), page));
    m_paletteHost = new QWidget(page);
    m_palette = new QGridLayout(m_paletteHost);
    layout->addWidget(m_paletteHost);
    layout->addStretch(1);
    return page;
}

QWidget *DesktopLookPanel::appPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    m_appInfo = wrapped(QStringLiteral("Point at an app's window in design mode and choose Restyle App."), page);
    m_appInfo->setObjectName(QStringLiteral("appInfo"));
    form->addRow(m_appInfo);
    for (const auto &[key, label] : {std::pair{QStringLiteral("app.accent"), QStringLiteral("Accent")},
                                     std::pair{QStringLiteral("app.background"), QStringLiteral("Background")},
                                     std::pair{QStringLiteral("app.foreground"), QStringLiteral("Text")}})
        form->addRow(label, swatch(key, page));
    m_appFont = new QComboBox(page);
    m_appFont->setEditable(true);
    connect(m_appFont, &QComboBox::currentTextChanged, this, [this](const QString &family) {
        if (!m_filling)
            m_style["font"] = family.trimmed();
    });
    form->addRow(QStringLiteral("Font"), m_appFont);
    for (const auto &[key, label, top] : {std::tuple{QStringLiteral("fontSize"), QStringLiteral("Size"), 72.0},
                                          std::tuple{QStringLiteral("radius"), QStringLiteral("Corner radius"), 64.0}}) {
        auto *field = new NumberField(label, key == QLatin1String("radius") ? QStringLiteral("px") : QStringLiteral("pt"),
                                      [this, key = key](double value) { m_style[key] = value; }, page);
        field->minimum = 0;
        field->maximum = top;
        m_fields.insert(QStringLiteral("app.") + key, field);
        form->addRow(field);
    }
    auto *buttons = new QHBoxLayout;
    auto *previewButton = new QPushButton(QStringLiteral("Preview in a Second Window"), page);
    auto *saveButton = new QPushButton(QStringLiteral("Save…"), page);
    connect(previewButton, &QPushButton::clicked, this, &DesktopLookPanel::previewApp);
    connect(saveButton, &QPushButton::clicked, this, &DesktopLookPanel::saveApp);
    buttons->addWidget(previewButton);
    buttons->addWidget(saveButton);
    form->addRow(buttons);
    return page;
}

void DesktopLookPanel::reload()
{
    QJsonObject look;
    if (!run(QStringLiteral("look"), {{"op", "get"}}, &look).isEmpty())
        return;
    m_look = look;
    const QJsonObject pending = look["pending"].toObject();
    const auto value = [&](const QString &key) { return pending.contains(key) ? pending[key] : look[key]; };
    m_filling = true;
    for (const char *key : {"gapsIn", "gapsOut", "borderSize", "rounding", "barHeight", "textSize"})
        m_fields.value(key)->sync(value(key).toDouble());
    for (const char *key : {"activeBorder", "inactiveBorder", "barBackground", "barText"})
        setSwatch(m_colors.value(key), DesktopLook::parseColor(value(key).toString()));
    m_barPosition->setCurrentText(value(QStringLiteral("barPosition")).toString());
    m_barTransparent->setChecked(value(QStringLiteral("barTransparent")).toBool());
    QJsonObject layout = look["barLayout"].toObject();
    if (pending.contains(QLatin1String("barLayout")))
        layout = DesktopLook::reorderLayout(layout, pending["barLayout"].toObject());
    for (int section = 0; section < 3; ++section) {
        QListWidget *list = m_barLists[size_t(section)];
        list->clear();
        for (const QJsonValue &widget : layout[barSections[section]].toArray())
            list->addItem(widget.isString() ? widget.toString() : widget.toObject()["id"].toString());
    }
    if (m_font->count() == 0) {
        QJsonObject fonts;
        run(QStringLiteral("look"), {{"op", "fonts"}}, &fonts);
        for (const QJsonValue &family : fonts["fonts"].toArray())
            m_font->addItem(family.toString());
    }
    m_font->setCurrentText(value(QStringLiteral("font")).toString());
    const QString wallpaper = value(QStringLiteral("wallpaper")).toString();
    m_wallpaper->setText(wallpaper.isEmpty() ? QStringLiteral("No wallpaper is set.") : QStringLiteral("Now: %1").arg(wallpaper));
    // The theme's colours, as swatches.
    for (QPushButton *button : m_paletteHost->findChildren<QPushButton *>()) {
        m_colors.remove(button->objectName());
        button->deleteLater();
    }
    for (QLabel *label : m_paletteHost->findChildren<QLabel *>())
        label->deleteLater();
    const QJsonObject colours = look["colors"].toObject();
    const QJsonObject pendingColours = pending["colors"].toObject();
    int row = 0;
    for (const QJsonValue &name : look["colorOrder"].toArray()) {
        const QString key = name.toString();
        auto *label = new QLabel(key, m_paletteHost);
        QPushButton *button = swatch(QStringLiteral("colors.") + key, m_paletteHost);
        setSwatch(button, DesktopLook::parseColor(pendingColours.contains(key) ? pendingColours[key].toString() : colours[key].toString()));
        m_palette->addWidget(label, row / 2, (row % 2) * 2);
        m_palette->addWidget(button, row / 2, (row % 2) * 2 + 1);
        ++row;
    }
    m_history->clear();
    for (const QJsonValue &entry : look["history"].toArray()) {
        const QJsonObject backup = entry.toObject();
        const QString when = QLocale::system().toString(QDateTime::fromString(backup["when"].toString(), Qt::ISODate), QLocale::ShortFormat);
        m_history->addItem(QStringLiteral("%1, %2%3").arg(backup["title"].toString(), when, backup["reverted"].toBool() ? QStringLiteral(" (reverted)") : QString()),
                           backup["id"].toString());
    }
    // The app to restyle, if one was pointed at.
    if (m_controller.styleApp()) {
        QJsonObject app;
        if (run(QStringLiteral("restyle"), {{"op", "get"}}, &app).isEmpty()) {
            m_appInfo->setText(QStringLiteral("%1, a %2 app%3. %4")
                                   .arg(app["app"].toString(), app["toolkit"].toString(),
                                        app["widget"].toString().isEmpty() ? QString() : QStringLiteral(", pointed at a %1").arg(app["widget"].toString()),
                                        app["honesty"].toString()));
            for (const auto &[key, from] : {std::pair{"app.background", "background"}, std::pair{"app.foreground", "foreground"}})
                if (!m_style.contains(QLatin1String(from)))
                    setSwatch(m_colors.value(QLatin1String(key)), QColor(app[QLatin1String(from)].toString()));
            if (!m_style.contains(QLatin1String("font")))
                m_appFont->setCurrentText(app["font"].toString());
        }
    } else if (!m_controller.restyleError().isEmpty()) {
        m_appInfo->setText(m_controller.restyleError());
    }
    m_note->setText(look["note"].toString());
    m_filling = false;
}

void DesktopLookPanel::preview(const QJsonObject &edits)
{
    QJsonObject result;
    if (run(QStringLiteral("look"), {{"op", "preview"}, {"edits", edits}}, &result).isEmpty())
        m_note->setText(result["note"].toString());
}

void DesktopLookPanel::pickColor(const QString &key)
{
    QPushButton *button = m_colors.value(key);
    if (!button)
        return;
    const QColor initial = button->property("color").value<QColor>();
    const auto apply = [this, key, button](const QColor &colour) {
        if (!colour.isValid())
            return;
        setSwatch(button, colour);
        if (key.startsWith(QLatin1String("app.")))
            m_style[key.mid(4)] = colour.name(QColor::HexRgb);
        else if (key.startsWith(QLatin1String("colors.")))
            preview({{"colors", QJsonObject{{key.mid(7), hexOf(colour)}}}});
        else
            preview({{key, hexOf(colour)}});
    };
    if (colorResponder()) {
        apply(colorResponder()(key, initial));
        return;
    }
    // Picking previews live; Cancel puts the colour back.
    QColorDialog dialog(initial.isValid() ? initial : QColor(Qt::gray), this);
    dialog.setOption(QColorDialog::ShowAlphaChannel, !key.startsWith(QLatin1String("app.")));
    dialog.setOption(QColorDialog::DontUseNativeDialog);
    if (!key.startsWith(QLatin1String("app.")))
        connect(&dialog, &QColorDialog::currentColorChanged, this, apply);
    if (dialog.exec() == QDialog::Accepted)
        apply(dialog.selectedColor());
    else if (initial.isValid() && dialog.currentColor() != initial && !key.startsWith(QLatin1String("app.")))
        apply(initial);
}

void DesktopLookPanel::barOrderChanged()
{
    QJsonObject order;
    for (int section = 0; section < 3; ++section) {
        QJsonArray ids;
        QListWidget *list = m_barLists[size_t(section)];
        for (int row = 0; row < list->count(); ++row)
            ids.append(list->item(row)->text());
        order[barSections[section]] = ids;
    }
    preview({{"barLayout", order}});
}

void DesktopLookPanel::save()
{
    run(QStringLiteral("look"), {{"op", "save"}});
}

void DesktopLookPanel::discard()
{
    run(QStringLiteral("look"), {{"op", "discard"}});
    reload();
}

void DesktopLookPanel::revertSelected()
{
    if (m_history->currentIndex() < 0) {
        m_note->setText(QStringLiteral("Nothing has been saved to the desktop yet, so there's nothing to revert."));
        return;
    }
    run(QStringLiteral("look"), {{"op", "revert"}, {"id", m_history->currentData().toString()}});
}

void DesktopLookPanel::previewApp()
{
    QJsonObject result;
    if (run(QStringLiteral("restyle"), {{"op", "preview"}, {"style", m_style}}, &result).isEmpty())
        m_note->setText(result["note"].toString());
}

void DesktopLookPanel::saveApp()
{
    run(QStringLiteral("restyle"), {{"op", "save"}, {"style", m_style}});
}
