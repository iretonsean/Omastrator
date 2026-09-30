#include "UI/ElementBarActions.h"
#include "UI/HandsFocusBack.h"
#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "Agent/AgentLauncher.h"
#include "UI/AnimateSheet.h"
#include "UI/BrowserViews.h"
#include "UI/ColorPickerSheet.h"
#include "UI/FloatingPanel.h"
#include "UI/LiveFrames.h"
#include "UI/NumberField.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QToolButton>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QLabel>
#include <array>
#include <tuple>
#include <memory>

namespace {
constexpr int controlHeight = NumberField::fieldHeight;

std::function<QString(const QUuid &)> &projectResolver()
{
    static std::function<QString(const QUuid &)> resolver;
    return resolver;
}

// Everything the bar's controls share: the frame's canvas, and how to bring their values up to date.
struct Shared_ {
    EditorCanvas *canvas = nullptr;
    AgentBridge *agent = nullptr;
    std::vector<std::function<void()>> syncers;
    // The app's own colour picker, made on first use.
    std::unique_ptr<FloatingPanel> picker;
    // Padding shown for each side instead of as a pair; kept across the bar's refills.
    bool boxed = false;
    // The Animate sheet under the bar (docs/MOTION.md, section 4), made on first use.
    QPointer<AnimateSheet> sheet;
    QPointer<QWidget> bar;

    FloatingPanel &pickerPanel()
    {
        if (!picker)
            picker = std::make_unique<FloatingPanel>(QStringLiteral("colorPickerPanel"), *canvas);
        return *picker;
    }
    BrowserViewHost *host() const { return canvas->browserViewHost(); }
    QJsonArray selection() const
    {
        const std::optional<QUuid> frame = canvas->editPageFrame();
        return frame && host() ? host()->elementState(*frame).selection : QJsonArray();
    }
    QJsonObject tokens() const
    {
        const std::optional<QUuid> frame = canvas->editPageFrame();
        return frame && host() ? host()->elementState(*frame).tokens : QJsonObject();
    }
    // The frame's own project, empty on a site that isn't the user's.
    QString project() const
    {
        const std::optional<QUuid> frame = canvas->editPageFrame();
        if (frame && projectResolver())
            return projectResolver()(*frame);
        LiveFrames *live = frame ? canvas->session().findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
        return live ? live->snapshot(*frame).project : QString();
    }
    void apply(const QStringList &properties, const QString &value, bool preview) const
    {
        const std::optional<QUuid> frame = canvas->editPageFrame();
        if (!frame || !host())
            return;
        const QString failure = host()->editElements(*frame, properties, value, preview);
        if (!failure.isEmpty())
            emit canvas->notice(failure);
    }
};

QString lengthText(double value)
{
    return QString::number(value, 'g', 6) + QStringLiteral("px");
}

QIcon swatch(const QColor &color, bool mixed)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(128, 128, 128), 1, mixed ? Qt::DashLine : Qt::SolidLine));
    if (color.isValid() && !mixed && color.alpha() > 0)
        painter.setBrush(color);
    else
        painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    return QIcon(pixmap);
}

QString colorText(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(QString::number(color.alphaF(), 'g', 3));
}


class Filler {
public:
    Filler(const std::shared_ptr<Shared_> &shared, QHBoxLayout &row, QWidget *parent) : m_shared(shared), m_row(row), m_parent(parent) {}

    void fill(const QString &signature)
    {
        m_shared->syncers.clear();
        if (signature.contains(QLatin1Char('t')))
            text();
        color(QStringLiteral("color"), QStringLiteral("Text colour"), QStringLiteral("A"));
        color(QStringLiteral("background-color"), QStringLiteral("Fill colour"), QString());
        if (signature.contains(QLatin1Char('p'))) {
            for (const auto &side : {std::tuple{"Top", "T", "top"}, std::tuple{"Right", "R", "right"}, std::tuple{"Bottom", "B", "bottom"},
                                     std::tuple{"Left", "L", "left"}})
                number(QStringLiteral("elementPadding") + QLatin1String(std::get<0>(side)), QLatin1String(std::get<1>(side)),
                       QStringLiteral("%1 padding").arg(QLatin1String(std::get<0>(side))), {QStringLiteral("padding-") + QLatin1String(std::get<2>(side))},
                       true, 0, 1000, 1);
        } else {
            number(QStringLiteral("elementPaddingX"), QStringLiteral("↔"), QStringLiteral("Horizontal padding"),
                   {QStringLiteral("padding-left"), QStringLiteral("padding-right")}, true, 0, 1000, 1);
            number(QStringLiteral("elementPaddingY"), QStringLiteral("↕"), QStringLiteral("Vertical padding"),
                   {QStringLiteral("padding-top"), QStringLiteral("padding-bottom")}, true, 0, 1000, 1);
        }
        paddingBox(signature.contains(QLatin1Char('p')));
        number(QStringLiteral("elementWidth"), QStringLiteral("W"), QStringLiteral("Width"), {QStringLiteral("width")}, true, 0, 10000, 1);
        number(QStringLiteral("elementHeight"), QStringLiteral("H"), QStringLiteral("Height"), {QStringLiteral("height")}, true, 0, 10000, 1);
        if (signature.contains(QLatin1Char('f'))) {
            number(QStringLiteral("elementFontSize"), QString(), QStringLiteral("Font size"), {QStringLiteral("font-size")}, true, 1, 999, 1);
            number(QStringLiteral("elementFontWeight"), QString(), QStringLiteral("Font weight"), {QStringLiteral("font-weight")}, false, 100, 900, 100);
        }
        number(QStringLiteral("elementRadius"), QStringLiteral("R"), QStringLiteral("Corner radius"), {QStringLiteral("border-radius")}, true, 0, 1000, 1);
        // A site that isn't the user's has no code to change: Build It offers Hand to Agent there.
        const bool yours = signature.contains(QLatin1Char('k'));
        if (yours) {
            ask();
            animate(signature.contains(QLatin1Char('g')));
        }
        more(yours);
    }

private:
    QToolButton *tool(const QString &name, const QString &label)
    {
        auto *made = new QToolButton(m_parent);
        made->setObjectName(name);
        made->setText(label);
        made->setAccessibleName(label);
        made->setAutoRaise(true);
        made->setFocusPolicy(Qt::NoFocus);
        made->setToolButtonStyle(Qt::ToolButtonTextOnly);
        made->setFixedHeight(controlHeight);
        return made;
    }

    void text()
    {
        auto shared = m_shared;
        QToolButton *edit = tool(QStringLiteral("elementEditText"), QStringLiteral("Edit Text"));
        edit->setToolTip(QStringLiteral("Type over this text on the page"));
        QObject::connect(edit, &QToolButton::clicked, edit, [shared] { shared->canvas->editPageText(); });
        m_row.addWidget(edit);
    }

    void color(const QString &property, const QString &label, const QString &glyph)
    {
        auto shared = m_shared;
        QToolButton *well = tool(QStringLiteral("element:") + property, glyph);
        well->setToolTip(label);
        well->setToolButtonStyle(glyph.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        well->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(well);
        menu->setObjectName(QStringLiteral("element:%1:menu").arg(property));
        well->setMenu(menu);
        QObject::connect(menu, &QMenu::aboutToShow, menu, [shared, menu, property, label] {
            menu->clear();
            for (const QJsonValue &each : shared->tokens().value(QStringLiteral("colors")).toArray()) {
                const QJsonObject token = each.toObject();
                const QString value = token.value(QStringLiteral("value")).toString();
                QAction *action = menu->addAction(swatch(QColor(value), false), token.value(QStringLiteral("name")).toString());
                QObject::connect(action, &QAction::triggered, menu, [shared, property, value] { shared->apply({property}, value, false); });
            }
            if (!menu->isEmpty())
                menu->addSeparator();
            QAction *custom = menu->addAction(QStringLiteral("Custom…"));
            custom->setObjectName(QStringLiteral("elementCustomColor"));
            QObject::connect(custom, &QAction::triggered, menu, [shared, property, label] {
                const ElementBarActions::Common current = ElementBarActions::common(shared->selection(), property);
                const QColor start = ElementBarActions::colorOf(current.value);
                ColorPickerSheet::showIn(shared->pickerPanel(), label, start.isValid() ? start : QColor(Qt::white),
                                         [weak = std::weak_ptr<Shared_>(shared), property](const QColor &picked) {
                                             // The panel is owned by `shared`, so it can't own it back.
                                             if (auto kept = weak.lock())
                                                 kept->apply({property}, colorText(picked), false);
                                         }, true);
            });
        });
        m_shared->syncers.push_back([shared, well, property] {
            const ElementBarActions::Common current = ElementBarActions::common(shared->selection(), property);
            well->setIcon(swatch(ElementBarActions::colorOf(current.value), current.mixed));
            well->setToolTip(current.mixed ? well->accessibleName() + QStringLiteral(" (mixed)") : well->accessibleName());
        });
        well->setAccessibleName(label);
        well->setIconSize(QSize(14, 14));
        m_row.addWidget(well);
    }

    NumberField *number(const QString &name, const QString &label, const QString &described, const QStringList &properties, bool pixels,
                        double minimum, double maximum, double step, bool place = true)
    {
        auto shared = m_shared;
        struct Scrub {
            bool on = false;
            QString last;
        };
        auto scrub = std::make_shared<Scrub>();
        NumberField *field = nullptr;
        field = new NumberField(label, pixels ? QStringLiteral("px") : QString(), [shared, scrub, properties, pixels](double value) {
            const QString text = pixels ? lengthText(value) : QString::number(int(std::lround(value)));
            if (scrub->on) {
                scrub->last = text;
                shared->apply(properties, text, true);
            } else {
                shared->apply(properties, text, false);
            }
        }, m_parent);
        field->gesture = [shared, scrub, properties](bool starting) {
            if (starting) {
                scrub->on = true;
                scrub->last.clear();
                return;
            }
            scrub->on = false;
            if (!scrub->last.isEmpty())
                shared->apply(properties, scrub->last, false);
            scrub->last.clear();
        };
        field->setObjectName(name);
        field->field->setObjectName(name + QStringLiteral("Field"));
        field->field->setAccessibleName(described);
        field->field->setToolTip(described);
        // Tab from the canvas never lands in a field; a click does.
        field->field->setFocusPolicy(Qt::ClickFocus);
        field->field->installEventFilter(new HandsFocusBack(shared->canvas, field));
        field->field->setFixedWidth(label.isEmpty() ? 44 : 48);
        field->setFixedHeight(controlHeight);
        field->step = step;
        field->minimum = minimum;
        field->maximum = maximum;
        m_shared->syncers.push_back([shared, field, properties] {
            std::optional<double> value;
            bool mixed = false;
            QString first;
            for (const QString &property : properties) {
                const ElementBarActions::Common each = ElementBarActions::common(shared->selection(), property);
                mixed = mixed || each.mixed;
                if (first.isEmpty())
                    first = each.value;
                else if (each.value != first)
                    mixed = true;
            }
            value = ElementBarActions::numberOf(first);
            if (mixed)
                field->syncMixed();
            else if (value)
                field->sync(*value);
            else
                field->syncUnset(0, QStringLiteral("auto"));
        });
        if (place)
            m_row.addWidget(field);
        return field;
    }

    // The box: padding for each side, or back to the pair.
    void paddingBox(bool boxed)
    {
        auto shared = m_shared;
        QToolButton *box = tool(QStringLiteral("elementPaddingBox"), QStringLiteral("▣"));
        box->setCheckable(true);
        box->setChecked(boxed);
        box->setAccessibleName(QStringLiteral("Padding on each side"));
        box->setToolTip(QStringLiteral("Padding on each side"));
        QObject::connect(box, &QToolButton::toggled, box, [shared](bool on) {
            shared->boxed = on;
            // The bar's signature carries it (the letter p), so the bar refills with the other fields.
            shared->canvas->noteEditPageHostChanged();
        });
        m_row.addWidget(box);
    }

    // A titled row of fields in the ⋯ menu.
    void fieldsRow(QMenu *menu, const QString &title, const std::vector<std::array<QString, 4>> &fields)
    {
        auto *holder = new QWidget(menu);
        auto *layout = new QHBoxLayout(holder);
        layout->setContentsMargins(10, 2, 10, 2);
        auto *heading = new QLabel(title, holder);
        heading->setMinimumWidth(96);
        layout->addWidget(heading);
        for (const auto &[name, label, described, property] : fields) {
            NumberField *field = number(name, label, described, property.split(QLatin1Char(',')), true, property.startsWith(QLatin1String("margin")) ? -1000 : 0, 1000, 1, false);
            field->setParent(holder);
            layout->addWidget(field);
        }
        auto *action = new QWidgetAction(menu);
        action->setDefaultWidget(holder);
        menu->addAction(action);
    }

    void ask()
    {
        auto shared = m_shared;
        QToolButton *button = tool(QStringLiteral("elementAsk"), QStringLiteral("Ask…"));
        button->setToolTip(QStringLiteral("Ask the agent to change these in the code"));
        QObject::connect(button, &QToolButton::clicked, button, [shared] {
            if (!shared->agent)
                return;
            bool accepted = false;
            const QString prompt = QInputDialog::getText(shared->canvas, QStringLiteral("Ask AI"), QStringLiteral("What should change?"), QLineEdit::Normal,
                                                         QString(), &accepted);
            if (!accepted || prompt.trimmed().isEmpty())
                return;
            const QString project = shared->project();
            if (project.isEmpty()) {
                emit shared->canvas->notice(QStringLiteral("This page isn't one of your sites, so there's no code to change."));
                return;
            }
            const QString failure = shared->agent->liveAsk(prompt.trimmed(), shared->selection(), nullptr, project);
            if (!failure.isEmpty())
                emit shared->canvas->notice(failure);
        });
        m_row.addWidget(button);
    }

    // Animate opens the sheet under the bar; several picked elements are one group.
    void animate(bool together)
    {
        auto shared = m_shared;
        shared->bar = m_parent;
        QToolButton *button = tool(QStringLiteral("elementAnimate"), together ? QStringLiteral("Animate together") : QStringLiteral("Animate"));
        button->setToolTip(together ? QStringLiteral("Ask the agent for motion that moves these elements as one group")
                                    : QStringLiteral("Ask the agent to write motion for this element as real CSS"));
        QObject::connect(button, &QToolButton::clicked, button, [shared] { openAnimateSheet(shared); });
        m_row.addWidget(button);
    }

    static void openAnimateSheet(const std::shared_ptr<Shared_> &shared)
    {
        EditorCanvas *canvas = shared->canvas;
        if (!shared->sheet) {
            auto *sheet = new AnimateSheet(canvas);
            shared->sheet = sheet;
            QObject::connect(sheet, &AnimateSheet::cancelled, sheet, &AnimateSheet::close);
            QObject::connect(sheet, &AnimateSheet::generate, sheet, [shared, sheet](const QString &instruction, bool reduced) {
                const std::optional<QUuid> frame = shared->canvas->editPageFrame();
                if (!frame)
                    return;
                const QString failure = BrowserViews::of(shared->canvas->session())->animate(*frame, instruction, reduced);
                if (!failure.isEmpty()) {
                    emit shared->canvas->notice(failure);
                    return;
                }
                // Writing…: the sheet says so with Stop until the motion is ready to preview, or the run ends.
                sheet->setRunning(true);
                sheet->setSteps({QStringLiteral("✓ Asked your agent"), QStringLiteral("… Writing the motion"), QStringLiteral("· Checking it"), QStringLiteral("· Starting the preview")});
            });
            QObject::connect(sheet, &AnimateSheet::stopped, sheet, [shared, sheet] {
                // Stop, and Esc while it writes: the run ends, its worktree goes, and the sheet stays for another try.
                // Before the agent is launched there is nothing to stop but the ask itself.
                if (const std::optional<QUuid> frame = shared->canvas->editPageFrame())
                    BrowserViews::of(shared->canvas->session())->stopAnimate(*frame);
                if (shared->agent)
                    shared->agent->stopWaiting();
                sheet->setRunning(false);
            });
            // The ask ended without the agent taking it up: the sheet stops saying "Writing…" and stays for another try.
            QObject::connect(BrowserViews::of(shared->canvas->session()), &BrowserViews::animateEnded, sheet, [sheet](const QUuid &) { sheet->setRunning(false); });
            if (shared->agent) {
                // The run ended (a preview is ready, or nothing was written): the sheet is done.
                const auto ended = [shared, sheet] {
                    if (sheet->isRunning() && shared->agent && shared->agent->animatingFrame().isNull()) {
                        sheet->setRunning(false);
                        sheet->close();
                    }
                };
                QObject::connect(shared->agent, &AgentBridge::waitingChanged, sheet, ended);
                QObject::connect(shared->agent, &AgentBridge::previewChanged, sheet, ended);
            }
        }
        QString error;
        const QString agent = AgentLauncher::defaultAgent(&error);
        shared->sheet->open(agent.isEmpty() ? QString() : AgentBridge::displayName(agent), int(shared->selection().size()));
        // Under the bar, kept inside the canvas.
        QRect where(shared->sheet->size().isValid() ? shared->sheet->geometry().topLeft() : QPoint(), shared->sheet->size());
        if (shared->bar) {
            where.moveTopLeft(QPoint(shared->bar->x(), shared->bar->geometry().bottom() + 6));
            if (where.bottom() > canvas->height() - 8)
                where.moveTop(std::max(8, shared->bar->y() - where.height() - 6));
            where.moveLeft(std::clamp(where.left(), 8, std::max(8, canvas->width() - where.width() - 8)));
        }
        shared->sheet->move(where.topLeft());
        shared->sheet->raise();
    }

    void more(bool yours)
    {
        auto shared = m_shared;
        QToolButton *button = tool(QStringLiteral("elementMore"), QStringLiteral("⋯"));
        button->setAccessibleName(QStringLiteral("More"));
        button->setToolTip(QStringLiteral("More"));
        button->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(button);
        menu->setObjectName(QStringLiteral("elementMoreMenu"));
        QAction *copy = menu->addAction(QStringLiteral("Copy Selector"));
        copy->setObjectName(QStringLiteral("elementCopySelector"));
        QObject::connect(copy, &QAction::triggered, menu, [shared] {
            const QJsonArray picked = shared->selection();
            if (!picked.isEmpty())
                QGuiApplication::clipboard()->setText(picked.first().toObject().value(QStringLiteral("selector")).toString());
        });
        fieldsRow(menu, QStringLiteral("Margin"),
                  {{QStringLiteral("elementMarginX"), QStringLiteral("↔"), QStringLiteral("Horizontal margin"), QStringLiteral("margin-left,margin-right")},
                   {QStringLiteral("elementMarginY"), QStringLiteral("↕"), QStringLiteral("Vertical margin"), QStringLiteral("margin-top,margin-bottom")}});
        fieldsRow(menu, QStringLiteral("Corner radius"),
                  {{QStringLiteral("elementRadiusTopLeft"), QStringLiteral("↖"), QStringLiteral("Top left radius"), QStringLiteral("border-top-left-radius")},
                   {QStringLiteral("elementRadiusTopRight"), QStringLiteral("↗"), QStringLiteral("Top right radius"), QStringLiteral("border-top-right-radius")},
                   {QStringLiteral("elementRadiusBottomRight"), QStringLiteral("↘"), QStringLiteral("Bottom right radius"), QStringLiteral("border-bottom-right-radius")},
                   {QStringLiteral("elementRadiusBottomLeft"), QStringLiteral("↙"), QStringLiteral("Bottom left radius"), QStringLiteral("border-bottom-left-radius")}});
        // A site that isn't the user's keeps its edits on this machine, per site.
        if (!yours) {
            QAction *keep = menu->addAction(QStringLiteral("Keep Edits…"));
            keep->setObjectName(QStringLiteral("elementKeepEdits"));
            QObject::connect(keep, &QAction::triggered, menu, [shared] {
                const std::optional<QUuid> frame = shared->canvas->editPageFrame();
                if (frame && shared->host())
                    shared->host()->act(*frame, BrowserViewHost::Action::keepEdits);
            });
        }
        button->setMenu(menu);
        m_row.addWidget(button);
    }

    std::shared_ptr<Shared_> m_shared;
    QHBoxLayout &m_row;
    QWidget *m_parent;
};
}

std::optional<double> ElementBarActions::numberOf(const QString &css)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(-?\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = pattern.match(css);
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1).toDouble();
}

QColor ElementBarActions::colorOf(const QString &css)
{
    static const QRegularExpression rgb(QStringLiteral("^rgba?\\(\\s*(\\d+)[,\\s]+(\\d+)[,\\s]+(\\d+)(?:[,\\s/]+([\\d.]+%?))?\\s*\\)$"));
    const QRegularExpressionMatch match = rgb.match(css.trimmed());
    if (match.hasMatch()) {
        double alpha = 1;
        if (!match.captured(4).isEmpty())
            alpha = match.captured(4).endsWith(QLatin1Char('%')) ? match.captured(4).chopped(1).toDouble() / 100 : match.captured(4).toDouble();
        QColor color(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        color.setAlphaF(std::clamp(alpha, 0.0, 1.0));
        return color;
    }
    return QColor(css.trimmed());
}

ElementBarActions::Common ElementBarActions::common(const QJsonArray &selection, const QString &property)
{
    Common result;
    bool first = true;
    for (const QJsonValue &each : selection) {
        const QString value = each.toObject().value(QStringLiteral("styles")).toObject().value(property).toString();
        if (first) {
            result.value = value;
            first = false;
        } else if (value != result.value) {
            result.mixed = true;
        }
    }
    return result;
}

QString ElementBarActions::signature(const QJsonArray &selection)
{
    if (selection.isEmpty())
        return {};
    QString result = QStringLiteral("bar");
    if (selection.size() == 1 && selection.first().toObject().value(QStringLiteral("textOnly")).toBool())
        result += QLatin1Char('t');
    for (const QJsonValue &each : selection) {
        if (!each.toObject().value(QStringLiteral("text")).toString().isEmpty()) {
            result += QLatin1Char('f');
            break;
        }
    }
    return result;
}

void ElementBarActions::setProjectResolver(std::function<QString(const QUuid &frame)> resolver)
{
    projectResolver() = std::move(resolver);
}

ElementBar *ElementBarActions::attach(AgentBridge *agent, EditorCanvas &canvas)
{
    auto shared = std::make_shared<Shared_>();
    shared->canvas = &canvas;
    shared->agent = agent;
    auto *bar = new ElementBar(canvas);
    bar->setFiller([shared] {
                       QString signature = ElementBarActions::signature(shared->selection());
                       const bool yours = !signature.isEmpty() && !shared->project().isEmpty();
                       if (yours)
                           signature += QLatin1Char('k');
                       // Several elements are animated together, as one group (only where there is an Animate button to say so).
                       if (yours && shared->selection().size() > 1)
                           signature += QLatin1Char('g');
                       if (!signature.isEmpty() && shared->boxed)
                           signature += QLatin1Char('p');
                       return signature;
                   },
                   [shared, bar](QHBoxLayout &row) { Filler(shared, row, bar).fill(bar->shownSignature()); },
                   [shared] {
                       for (const auto &sync : shared->syncers)
                           sync();
                   });
    return bar;
}
