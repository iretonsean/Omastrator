#include "UI/MotionInspector.h"
#include "Canvas/EditorCanvas.h"
#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include "System/TokenFiles.h"
#include "UI/CurveEditor.h"
#include "UI/HandsFocusBack.h"
#include "UI/MotionTimeline.h"
#include "UI/NumberField.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <memory>
#include <optional>

namespace {
QLabel *label(const QString &text, const QString &name, QWidget *parent, bool dim = false)
{
    auto *made = new QLabel(text, parent);
    made->setObjectName(name);
    made->setWordWrap(true);
    made->setTextInteractionFlags(Qt::TextSelectableByMouse);
    made->setFocusPolicy(Qt::NoFocus);
    if (dim) {
        QPalette palette = made->palette();
        QColor color = palette.color(QPalette::WindowText);
        color.setAlphaF(0.6f);
        palette.setColor(QPalette::WindowText, color);
        made->setPalette(palette);
    }
    return made;
}

// "--duration-reveal" as "duration/reveal".
QString tokenName(const QString &property)
{
    QString name = property.mid(2);
    const qsizetype dash = name.indexOf(QLatin1Char('-'));
    if (dash > 0)
        name[dash] = QLatin1Char('/');
    return name;
}

QString milliseconds(double ms)
{
    return QStringLiteral("%1ms").arg(qRound(ms));
}

// A field in ms that shows a scrub on the page as it goes and records one edit when it ends, or at Enter.
NumberField *timeField(const QString &name, double value, MotionTimeline &timeline, QWidget *parent, std::function<void(double, bool)> apply)
{
    struct Scrub {
        bool on = false;
        std::optional<double> last;
    };
    auto scrub = std::make_shared<Scrub>();
    auto *field = new NumberField(QString(), QStringLiteral("ms"), [scrub, apply](double ms) {
        if (scrub->on) {
            scrub->last = ms;
            apply(ms, true);
        } else {
            apply(ms, false);
        }
    }, parent);
    field->gesture = [field, scrub, apply](bool starting) {
        // The inspector reads this to leave the field alone while it is being dragged.
        field->setProperty("scrubbing", starting);
        if (starting) {
            scrub->on = true;
            scrub->last.reset();
            return;
        }
        scrub->on = false;
        if (scrub->last)
            apply(*scrub->last, false);
        scrub->last.reset();
    };
    field->setObjectName(name);
    field->field->setObjectName(name + QStringLiteral("Field"));
    field->field->setAccessibleName(name);
    // A click gives it the keys; Enter and Esc hand them back to the canvas.
    field->field->setFocusPolicy(Qt::ClickFocus);
    field->field->installEventFilter(new HandsFocusBack(&timeline.canvas(), field));
    field->setFixedHeight(NumberField::fieldHeight);
    field->minimum = 0;
    field->maximum = 60000;
    field->step = 1;
    field->sync(value);
    return field;
}

// A line of text that commits at Enter, for an easing or a keyframe's value.
QLineEdit *textField(const QString &name, const QString &value, MotionTimeline &timeline, QWidget *parent, std::function<void(const QString &)> commit)
{
    auto *edit = new QLineEdit(value, parent);
    edit->setObjectName(name);
    edit->setAccessibleName(name);
    edit->setFocusPolicy(Qt::ClickFocus);
    edit->setFixedHeight(NumberField::fieldHeight);
    edit->installEventFilter(new HandsFocusBack(&timeline.canvas(), edit));
    QObject::connect(edit, &QLineEdit::returnPressed, edit, [edit, commit] { commit(edit->text().trimmed()); });
    return edit;
}
}

MotionInspector::MotionInspector(MotionTimeline &timeline, QWidget *parent) : QWidget(parent), m_timeline(timeline), m_outer(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("motionInspector"));
    setAccessibleName(QStringLiteral("Motion"));
    setFixedWidth(320);
    m_outer->setContentsMargins(16, 14, 16, 14);
    connect(&m_timeline, &MotionTimeline::changed, this, &MotionInspector::rebuild);
    connect(&m_timeline, &MotionTimeline::closed, this, &MotionInspector::rebuild);
    // A tab switch or a closed document deletes the timeline while this panel is up: nothing here reads it afterwards.
    connect(&m_timeline, &QObject::destroyed, this, [this] {
        m_gone = true;
        hide();
        deleteLater();
    });
    rebuild();
}

bool MotionInspector::busy() const
{
    if (!m_body)
        return false;
    for (const NumberField *field : m_body->findChildren<NumberField *>())
        if (field->property("scrubbing").toBool())
            return true;
    for (const CurveEditor *curve : m_body->findChildren<CurveEditor *>())
        if (curve->isDragging())
            return true;
    for (const QLineEdit *edit : m_body->findChildren<QLineEdit *>())
        if (edit->hasFocus() && edit->isModified())
            return true;
    return false;
}

void MotionInspector::rebuild()
{
    if (m_gone)
        return;
    // A late reading from the page must not delete the field under the designer's drag, the curve under its handle, or a line of
    // text being typed: one retry waits, whatever the number of readings meanwhile.
    if (busy()) {
        if (!m_rebuildPending) {
            m_rebuildPending = true;
            QTimer::singleShot(50, this, [this] {
                m_rebuildPending = false;
                rebuild();
            });
        }
        return;
    }
    delete m_body;
    m_body = new QWidget(this);
    m_outer->addWidget(m_body);
    auto *column = new QVBoxLayout(m_body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    const Motion::Track *track = m_timeline.selectedTrack();
    // Motion the agent wrote, previewed: what breaks its contract, and the way out.
    if (m_timeline.isOpen() && m_timeline.previewing()) {
        const QString notice = m_timeline.previewNotice();
        if (!notice.isEmpty())
            column->addWidget(label(notice, QStringLiteral("motionInspectorNotice"), m_body));
        auto *discard = new QToolButton(m_body);
        discard->setObjectName(QStringLiteral("motionInspectorDiscard"));
        discard->setText(tr("Discard the preview"));
        discard->setToolButtonStyle(Qt::ToolButtonTextOnly);
        discard->setFocusPolicy(Qt::NoFocus);
        connect(discard, &QToolButton::clicked, discard, [this] { m_timeline.discardPreview(); });
        column->addWidget(discard, 0, Qt::AlignLeft);
    }
    // Reduced motion is previewed for the whole page, so its switch stays where it is when the rows go.
    if (m_timeline.isOpen()) {
        auto *preview = new QToolButton(m_body);
        preview->setObjectName(QStringLiteral("motionInspectorPreviewReduced"));
        preview->setText(tr("Preview reduced"));
        preview->setToolTip(tr("Play the page as for someone who asked for less motion"));
        preview->setCheckable(true);
        preview->setChecked(m_timeline.previewReduced());
        preview->setToolButtonStyle(Qt::ToolButtonTextOnly);
        preview->setFocusPolicy(Qt::NoFocus);
        connect(preview, &QToolButton::clicked, preview, [this](bool on) { m_timeline.setPreviewReduced(on); });
        column->addWidget(preview, 0, Qt::AlignLeft);
    }
    if (!track) {
        column->addWidget(label(!m_timeline.isOpen() ? tr("Open the timeline to see a page's motion.")
                                : m_timeline.previewReduced() ? tr("No motion when reduced.")
                                                              : tr("No motion on this element yet."),
                                QStringLiteral("motionInspectorEmpty"), m_body, true));
        column->addStretch(1);
        return;
    }

    // A row of several elements is a group; one of its elements can be picked on its own.
    const int picked = m_timeline.selectedBar();
    const QString groupTitle = Motion::groupName(*track);
    auto *name = label(picked >= 0 && picked < track->bars.size() ? track->bars[picked].label : groupTitle.isEmpty() ? track->label : groupTitle,
                       QStringLiteral("motionInspectorName"), m_body);
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    column->addWidget(name);
    if (picked >= 0 && picked < track->bars.size()) {
        // One card of the group: its own delay on top of the group's, which the others keep.
        const Motion::Bar &bar = track->bars[picked];
        // Only a css-animation whose delay reads --delay-extra takes one: anywhere else the field would change nothing on the page,
        // and Save would add a declaration nothing reads.
        if (track->kind != QLatin1String("css-animation") || !m_timeline.bindings().extraDelay) {
            column->addWidget(label(tr("This motion's delay doesn't read --delay-extra, so it takes no extra delay. Ask the agent to add it, or edit the code."),
                                    QStringLiteral("motionInspectorNoExtra"), m_body, true));
            column->addWidget(label(tr("Its start is %1 in the group.").arg(bar.index >= 0 ? tr("number %1").arg(bar.index + 1) : tr("unnumbered")),
                                    QStringLiteral("motionInspectorPlace"), m_body, true));
            column->addStretch(1);
            return;
        }
        auto *extra = timeField(QStringLiteral("motionInspectorExtra"), bar.extra, m_timeline, m_body, [this](double ms, bool preview) {
            if (!preview)
                m_timeline.setExtraDelay(ms);
        });
        extra->setToolTip(tr("Extra delay for this one; the others keep the group's timing"));
        auto *extraForm = new QFormLayout;
        extraForm->setContentsMargins(0, 0, 0, 0);
        extraForm->addRow(tr("Extra delay"), extra);
        column->addLayout(extraForm);
        auto *back = new QToolButton(m_body);
        back->setObjectName(QStringLiteral("motionInspectorUseGroupTiming"));
        back->setText(tr("Use the group's timing"));
        back->setToolButtonStyle(Qt::ToolButtonTextOnly);
        back->setFocusPolicy(Qt::NoFocus);
        back->setEnabled(bar.extra != 0);
        connect(back, &QToolButton::clicked, back, [this] { m_timeline.useGroupTiming(); });
        column->addWidget(back, 0, Qt::AlignLeft);
        column->addWidget(label(tr("Its start is %1 in the group, and the rest of its timing is the group's.").arg(bar.index >= 0 ? tr("number %1").arg(bar.index + 1) : tr("unnumbered")),
                                QStringLiteral("motionInspectorPlace"), m_body, true));
        column->addStretch(1);
        return;
    }
    const bool script = track->kind == QLatin1String("script") || track->kind == QLatin1String("gsap");
    if (script)
        column->addWidget(label(tr("Motion from a script (not shown here). It can be scrubbed, and the agent edits it."),
                                QStringLiteral("motionInspectorScript"), m_body, true));

    const MotionCode::Bindings bound = m_timeline.bindings();
    const bool animation = track->kind == QLatin1String("css-animation");
    // What tunes a row: a token when the code holds the value in one, else a change the agent writes.
    const bool timed = !track->isScroll() && !script;
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft);
    auto *starts = new QWidget(m_body);
    starts->setObjectName(QStringLiteral("motionInspectorStarts"));
    auto *startsRow = new QHBoxLayout(starts);
    startsRow->setContentsMargins(0, 0, 0, 0);
    startsRow->setSpacing(2);
    auto *group = new QButtonGroup(starts);
    group->setExclusive(true);
    for (const auto &kind : {std::pair<QString, QString>{QStringLiteral("load"), tr("Load")}, {QStringLiteral("scroll"), tr("Scroll")},
                             {QStringLiteral("hover"), tr("Hover")}, {QStringLiteral("click"), tr("Click")}}) {
        auto *button = new QToolButton(starts);
        button->setObjectName(QStringLiteral("motionInspectorStarts:") + kind.first);
        button->setText(kind.second);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::NoFocus);
        button->setChecked(track->trigger == kind.first);
        // Only motion in CSS animations can change what starts it; the agent writes the rest.
        button->setEnabled(animation);
        group->addButton(button);
        startsRow->addWidget(button);
        const QString trigger = kind.first;
        connect(button, &QToolButton::clicked, button, [this, trigger] { m_timeline.setStarts(trigger); });
    }
    startsRow->addStretch(1);
    starts->setToolTip(animation ? tr("What starts this motion. Load and Scroll show here; your agent writes any change when you save.")
                                 : tr("Motion your agent wrote or the page made itself: ask your agent to change what starts it."));
    form->addRow(tr("Starts"), starts);

    // A group: the order its elements start in, and what they start from.
    if (!groupTitle.isEmpty() && timed) {
        auto *orders = new QWidget(m_body);
        orders->setObjectName(QStringLiteral("motionInspectorOrder"));
        auto *ordersRow = new QHBoxLayout(orders);
        ordersRow->setContentsMargins(0, 0, 0, 0);
        ordersRow->setSpacing(2);
        for (const auto &mode : {std::pair<QString, Motion::Order>{QStringLiteral("picked"), Motion::Order::picked},
                                 {QStringLiteral("leftToRight"), Motion::Order::leftToRight},
                                 {QStringLiteral("centreOut"), Motion::Order::centreOut},
                                 {QStringLiteral("shuffle"), Motion::Order::shuffle}}) {
            auto *button = new QToolButton(orders);
            button->setObjectName(QStringLiteral("motionInspectorOrder:") + mode.first);
            button->setText(Motion::orderName(mode.second));
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFocusPolicy(Qt::NoFocus);
            // The order is each element's `--i`, which the row's delay has to read.
            button->setEnabled(bound.indexed);
            const Motion::Order order = mode.second;
            connect(button, &QToolButton::clicked, button, [this, order] {
                const QString failure = m_timeline.setOrder(order);
                if (!failure.isEmpty())
                    emit m_timeline.notice(failure);
            });
            ordersRow->addWidget(button);
        }
        ordersRow->addStretch(1);
        orders->setToolTip(bound.indexed ? tr("The order the elements start in") : tr("This motion doesn't number its elements (--i). Ask your agent to regroup it."));
        form->addRow(tr("Order"), orders);
        if (animation && !track->keyframes.isEmpty()) {
            auto *effect = new QComboBox(m_body);
            effect->setObjectName(QStringLiteral("motionInspectorEffect"));
            effect->setFocusPolicy(Qt::NoFocus);
            effect->addItem(tr("Rise"), QStringLiteral("rise"));
            effect->addItem(tr("Grow"), QStringLiteral("grow"));
            effect->addItem(tr("Flip"), QStringLiteral("flip"));
            const QString now = m_timeline.effectOf();
            if (now == QLatin1String("custom"))
                effect->addItem(tr("Custom"), QStringLiteral("custom"));
            effect->setCurrentIndex(std::max(0, effect->findData(now)));
            connect(effect, &QComboBox::activated, effect, [this, effect](int index) {
            // Picking the item that is already shown changes nothing, and must not rewrite a keyframe.
            const QString picked = effect->itemData(index).toString();
            if (picked != m_timeline.effectOf())
                m_timeline.setEffect(picked);
        });
            form->addRow(tr("Effect"), effect);
        }
    }

    if (!track->easing.isEmpty() && timed) {
        const QString css = track->easing;
        const auto apply = [this, bound, animation](const QString &value, bool preview) {
            if (!bound.easing.isEmpty())
                m_timeline.setToken(bound.easing, value, preview);
            else if (!preview && animation)
                m_timeline.setTiming(QStringLiteral("animation-timing-function"), value);
        };
        auto *presets = new QComboBox(m_body);
        presets->setObjectName(QStringLiteral("motionInspectorEasingPreset"));
        presets->setFocusPolicy(Qt::NoFocus);
        presets->setAccessibleName(tr("Easing"));
        struct Preset {
            QString name;
            QString css;
        };
        QList<Preset> list{{tr("Soft out"), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)")}, {tr("Ease out quart"), QStringLiteral("cubic-bezier(0.25, 1, 0.5, 1)")},
                           {tr("Spring"), QStringLiteral("cubic-bezier(0.34, 1.56, 0.64, 1)")}, {tr("Linear"), QStringLiteral("linear")},
                           {tr("Ease out"), QStringLiteral("ease-out")}};
        // The project's own easings, of kind ease/*, follow the presets.
        for (const MotionCode::Block &block : m_timeline.codeBlocks())
            for (const auto &token : MotionCode::tokens(block))
                if (token.first.startsWith(QLatin1String("--ease-")))
                    list.append({tokenName(token.first), m_timeline.pendingValue(token.first, token.second)});
        int current = -1;
        for (int i = 0; i < list.size(); ++i) {
            presets->addItem(list[i].name, list[i].css);
            if (list[i].css.simplified() == css.simplified() && current < 0)
                current = i;
        }
        if (current < 0) {
            presets->addItem(tr("Custom"), css);
            current = presets->count() - 1;
        }
        presets->setCurrentIndex(current);
        connect(presets, &QComboBox::activated, presets, [presets, apply](int index) { apply(presets->itemData(index).toString(), false); });

        auto *easing = new QWidget(m_body);
        auto *easingColumn = new QVBoxLayout(easing);
        easingColumn->setContentsMargins(0, 0, 0, 0);
        easingColumn->setSpacing(6);
        easingColumn->addWidget(presets);
        auto *curve = new CurveEditor(easing);
        if (const auto points = TokenFiles::cubicBezier(css))
            curve->setCurve(*points);
        else
            curve->setEnabled(false);
        connect(curve, &CurveEditor::previewed, curve, [apply](const QString &value) { apply(value, true); });
        connect(curve, &CurveEditor::committed, curve, [apply](const QString &value) { apply(value, false); });
        easingColumn->addWidget(curve, 0, Qt::AlignLeft);
        easingColumn->addWidget(textField(QStringLiteral("motionInspectorEasingText"), css, m_timeline, easing, [apply](const QString &value) {
            if (TokenFiles::isEasing(value))
                apply(value, false);
        }));
        form->addRow(tr("Easing"), easing);
    }
    if (timed && track->kind != QLatin1String("gsap")) {
        const bool tokenised = !bound.duration.isEmpty();
        auto *duration = timeField(QStringLiteral("motionInspectorDuration"), track->duration, m_timeline, m_body,
                                   [this, bound, animation](double ms, bool preview) {
                                       if (!bound.duration.isEmpty())
                                           m_timeline.setToken(bound.duration, milliseconds(ms), preview);
                                       else if (!preview && animation)
                                           m_timeline.setTiming(QStringLiteral("animation-duration"), milliseconds(ms));
                                   });
        duration->setToolTip(tokenised ? tr("The token %1: changing it changes every use.").arg(tokenName(bound.duration))
                                       : tr("Not a motion token here: the change is handed to the agent when you save."));
        form->addRow(track->bars.size() > 1 ? tr("Duration (each)") : tr("Duration"), duration);
        if (track->stagger > 0 || !bound.stagger.isEmpty()) {
            auto *stagger = timeField(QStringLiteral("motionInspectorStagger"), track->stagger, m_timeline, m_body,
                                      [this, bound](double ms, bool preview) {
                                          if (!bound.stagger.isEmpty())
                                              m_timeline.setToken(bound.stagger, milliseconds(ms), preview);
                                      });
            stagger->setEnabled(!bound.stagger.isEmpty());
            stagger->setToolTip(bound.stagger.isEmpty() ? tr("This stagger is not a motion token. Ask to change it.") : tr("The token %1.").arg(tokenName(bound.stagger)));
            form->addRow(tr("Stagger"), stagger);
        }
    }
    column->addLayout(form);

    // The tokens are read from the code the motion is in; a pending edit shows as its new value.
    const QList<MotionCode::Block> blocks = m_timeline.codeBlocks();
    QList<QPair<QString, QString>> tokens;
    for (const MotionCode::Block &block : blocks)
        for (const auto &token : MotionCode::tokens(block))
            tokens.append(token);
    if (!tokens.isEmpty()) {
        column->addWidget(label(tr("Motion tokens, from your design system"), QStringLiteral("motionInspectorTokensTitle"), m_body, true));
        auto *list = new QFormLayout;
        list->setContentsMargins(0, 0, 0, 0);
        for (const auto &token : tokens) {
            const QString property = token.first;
            const QString shown = m_timeline.pendingValue(property, token.second);
            const QString row = QStringLiteral("motionInspectorToken:") + property;
            if (property.startsWith(QLatin1String("--ease-"))) {
                list->addRow(tokenName(property), textField(row, shown, m_timeline, m_body, [this, property](const QString &value) {
                    if (TokenFiles::isEasing(value))
                        m_timeline.setToken(property, value, false);
                }));
            } else {
                const double ms = TokenFiles::parseTime(shown).value_or(0);
                list->addRow(tokenName(property), timeField(row, ms, m_timeline, m_body, [this, property](double value, bool preview) {
                    m_timeline.setToken(property, milliseconds(value), preview);
                }));
            }
        }
        column->addLayout(list);
    }

    // The site's own motion with no rule for visitors who asked for less: the agent can add one.
    if (!m_timeline.timeline().reducedRule && timed) {
        column->addWidget(label(tr("This motion plays for people who asked for less motion."), QStringLiteral("motionInspectorReducedWarning"), m_body));
        auto *ask = new QToolButton(m_body);
        ask->setObjectName(QStringLiteral("motionInspectorAskReduced"));
        ask->setText(tr("Ask…"));
        ask->setToolTip(tr("Ask your agent to add a reduced-motion rule"));
        ask->setToolButtonStyle(Qt::ToolButtonTextOnly);
        ask->setFocusPolicy(Qt::NoFocus);
        connect(ask, &QToolButton::clicked, ask, [this] {
            const QString failure = m_timeline.askAgent(tr("Add a prefers-reduced-motion rule for this motion, so it doesn't play for people who asked for less motion. "
                                                            "Stop the movement and keep any colour or opacity change."));
            if (!failure.isEmpty())
                emit m_timeline.notice(failure);
        });
        column->addWidget(ask, 0, Qt::AlignLeft);
    }

    auto *reduced = new QCheckBox(tr("Also write a reduced-motion version (prefers-reduced-motion)"), m_body);
    reduced->setObjectName(QStringLiteral("motionInspectorReduced"));
    reduced->setFocusPolicy(Qt::NoFocus);
    if (!blocks.isEmpty()) {
        reduced->setChecked(m_timeline.reducedMotionOn());
        connect(reduced, &QCheckBox::clicked, reduced, [this](bool on) { m_timeline.setReducedMotion(on); });
        column->addWidget(reduced);
    } else {
        delete reduced;
    }

    if (!track->keyframes.isEmpty()) {
        auto *toggle = new QToolButton(m_body);
        toggle->setObjectName(QStringLiteral("motionInspectorKeyframesToggle"));
        toggle->setText(tr("Keyframes"));
        toggle->setCheckable(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setAutoRaise(true);
        toggle->setFocusPolicy(Qt::NoFocus);
        auto *frames = new QWidget(m_body);
        frames->setObjectName(QStringLiteral("motionInspectorKeyframes"));
        auto *rows = new QFormLayout(frames);
        rows->setContentsMargins(0, 0, 0, 0);
        for (const QJsonValue &value : track->keyframes) {
            const QJsonObject frame = value.toObject();
            const double offset = frame["offset"].toDouble();
            // How the code names it: from, to, or a percentage.
            const QString at = offset == 0 ? QStringLiteral("from") : offset == 1 ? QStringLiteral("to") : QStringLiteral("%1%").arg(qRound(offset * 100));
            const QJsonObject props = frame["props"].toObject();
            for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
                const QString property = it.key();
                if (animation) {
                    rows->addRow(QStringLiteral("%1  %2").arg(at, property),
                                 textField(QStringLiteral("motionInspectorKeyframe:%1:%2").arg(at, property), it.value().toString(), m_timeline, frames,
                                           [this, at, property](const QString &text) {
                                               if (!text.isEmpty())
                                                   m_timeline.setKeyframe(at, property, text);
                                           }));
                } else {
                    rows->addRow(QStringLiteral("%1  %2").arg(at, property), label(it.value().toString(), QStringLiteral("motionInspectorKeyframe:%1:%2").arg(at, property), frames));
                }
            }
        }
        frames->setVisible(false);
        connect(toggle, &QToolButton::toggled, frames, [toggle, frames](bool open) {
            frames->setVisible(open);
            toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        column->addWidget(toggle);
        column->addWidget(frames);
    }
    column->addStretch(1);
}
