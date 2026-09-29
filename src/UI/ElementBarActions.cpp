#include "UI/ElementBarActions.h"
#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "ContentView.h"
#include "UI/AgentBridge.h"
#include "UI/LiveFrames.h"
#include "UI/NumberField.h"
#include <QColorDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QToolButton>
#include <memory>

namespace {
constexpr int controlHeight = NumberField::fieldHeight;

// Everything the bar's controls share: the frame's canvas, and how to bring their values up to date.
struct Shared_ {
    EditorCanvas *canvas = nullptr;
    AgentBridge *agent = nullptr;
    std::vector<std::function<void()>> syncers;

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
        number(QStringLiteral("elementPaddingX"), QStringLiteral("↔"), QStringLiteral("Horizontal padding"),
               {QStringLiteral("padding-left"), QStringLiteral("padding-right")}, true, 0, 1000, 1);
        number(QStringLiteral("elementPaddingY"), QStringLiteral("↕"), QStringLiteral("Vertical padding"),
               {QStringLiteral("padding-top"), QStringLiteral("padding-bottom")}, true, 0, 1000, 1);
        number(QStringLiteral("elementWidth"), QStringLiteral("W"), QStringLiteral("Width"), {QStringLiteral("width")}, true, 0, 10000, 1);
        number(QStringLiteral("elementHeight"), QStringLiteral("H"), QStringLiteral("Height"), {QStringLiteral("height")}, true, 0, 10000, 1);
        if (signature.contains(QLatin1Char('f'))) {
            number(QStringLiteral("elementFontSize"), QString(), QStringLiteral("Font size"), {QStringLiteral("font-size")}, true, 1, 999, 1);
            number(QStringLiteral("elementFontWeight"), QString(), QStringLiteral("Font weight"), {QStringLiteral("font-weight")}, false, 100, 900, 100);
        }
        number(QStringLiteral("elementRadius"), QStringLiteral("R"), QStringLiteral("Corner radius"), {QStringLiteral("border-radius")}, true, 0, 1000, 1);
        ask();
        more();
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
            QObject::connect(custom, &QAction::triggered, menu, [shared, property, label, menu] {
                const ElementBarActions::Common current = ElementBarActions::common(shared->selection(), property);
                const QColor picked = QColorDialog::getColor(ElementBarActions::colorOf(current.value), menu->parentWidget(), label,
                                                             QColorDialog::ShowAlphaChannel);
                if (picked.isValid())
                    shared->apply({property}, colorText(picked), false);
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

    void number(const QString &name, const QString &label, const QString &described, const QStringList &properties, bool pixels, double minimum,
                double maximum, double step)
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
        m_row.addWidget(field);
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
            QString project;
            if (const std::optional<QUuid> frame = shared->canvas->editPageFrame()) {
                if (LiveFrames *live = shared->canvas->session().findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly))
                    project = live->snapshot(*frame).project;
            }
            const QString failure = shared->agent->liveAsk(prompt.trimmed(), shared->selection(), nullptr, project);
            if (!failure.isEmpty())
                emit shared->canvas->notice(failure);
        });
        m_row.addWidget(button);
    }

    void more()
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

ElementBar *ElementBarActions::attach(AgentBridge *agent, EditorCanvas &canvas)
{
    auto shared = std::make_shared<Shared_>();
    shared->canvas = &canvas;
    shared->agent = agent;
    auto *bar = new ElementBar(canvas);
    bar->setFiller([shared] { return ElementBarActions::signature(shared->selection()); },
                   [shared, bar](QHBoxLayout &row) { Filler(shared, row, bar).fill(bar->shownSignature()); },
                   [shared] {
                       for (const auto &sync : shared->syncers)
                           sync();
                   });
    return bar;
}
