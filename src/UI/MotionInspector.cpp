#include "UI/MotionInspector.h"
#include "Canvas/EditorCanvas.h"
#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include "System/TokenFiles.h"
#include "UI/CurveEditor.h"
#include "UI/HandsFocusBack.h"
#include "UI/MotionTimeline.h"
#include "UI/NumberField.h"
#include <QCheckBox>
#include <QComboBox>
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
    field->gesture = [scrub, apply](bool starting) {
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
    rebuild();
}

void MotionInspector::rebuild()
{
    delete m_body;
    m_body = new QWidget(this);
    m_outer->addWidget(m_body);
    auto *column = new QVBoxLayout(m_body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    const Motion::Track *track = m_timeline.selectedTrack();
    if (!track) {
        column->addWidget(label(m_timeline.isOpen() ? tr("No motion on this element yet.") : tr("Open the timeline to see a page's motion."),
                                QStringLiteral("motionInspectorEmpty"), m_body, true));
        return;
    }

    auto *name = label(track->label, QStringLiteral("motionInspectorName"), m_body);
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    column->addWidget(name);
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
    form->addRow(tr("Starts"), label(Motion::triggerText(track->trigger), QStringLiteral("motionInspectorStarts"), m_body));

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
