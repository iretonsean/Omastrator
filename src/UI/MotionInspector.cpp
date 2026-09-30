#include "UI/MotionInspector.h"
#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include "UI/MotionTimeline.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

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

QString milliseconds(double ms)
{
    return QStringLiteral("%1 ms").arg(qRound(ms));
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
    if (track->kind == QLatin1String("script") || track->kind == QLatin1String("gsap"))
        column->addWidget(label(tr("Motion from a script (not shown here). It can be scrubbed, and the agent edits it."),
                                QStringLiteral("motionInspectorScript"), m_body, true));

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("Starts"), label(Motion::triggerText(track->trigger), QStringLiteral("motionInspectorStarts"), m_body));
    if (!track->easing.isEmpty()) {
        const QString easing = Motion::easingName(track->easing);
        form->addRow(tr("Easing"), label(easing == QLatin1String("Custom") ? track->easing : QStringLiteral("%1  %2").arg(easing, track->easing),
                                         QStringLiteral("motionInspectorEasing"), m_body));
    }
    if (!track->isScroll() && track->kind != QLatin1String("gsap")) {
        form->addRow(tr("Duration"), label(track->bars.size() > 1 ? tr("%1 per element").arg(milliseconds(track->duration)) : milliseconds(track->duration),
                                           QStringLiteral("motionInspectorDuration"), m_body));
        if (track->stagger > 0)
            form->addRow(tr("Stagger"), label(milliseconds(track->stagger), QStringLiteral("motionInspectorStagger"), m_body));
        else if (track->delay > 0)
            form->addRow(tr("Delay"), label(milliseconds(track->delay), QStringLiteral("motionInspectorDelay"), m_body));
    }
    column->addLayout(form);

    // The tokens and the reduced-motion rule are read from the code the motion is in.
    const QList<MotionCode::Block> blocks = m_timeline.codeBlocks();
    bool reduced = false;
    QStringList tokens;
    for (const MotionCode::Block &block : blocks) {
        reduced = reduced || block.reducedMotion;
        for (const auto &token : MotionCode::tokens(block))
            tokens << QStringLiteral("%1  %2").arg(tokenName(token.first), token.second);
    }
    if (!tokens.isEmpty()) {
        column->addWidget(label(tr("Motion tokens, from your design system"), QStringLiteral("motionInspectorTokensTitle"), m_body, true));
        column->addWidget(label(tokens.join(QLatin1Char('\n')), QStringLiteral("motionInspectorTokens"), m_body));
    }
    auto *reducedBox = new QCheckBox(tr("Also write a reduced-motion version (prefers-reduced-motion)"), m_body);
    reducedBox->setObjectName(QStringLiteral("motionInspectorReduced"));
    reducedBox->setChecked(reduced);
    // Editing comes with the writable fields; this shows what the code has.
    reducedBox->setEnabled(false);
    reducedBox->setFocusPolicy(Qt::NoFocus);
    if (!blocks.isEmpty())
        column->addWidget(reducedBox);
    else
        delete reducedBox;

    if (!track->keyframes.isEmpty()) {
        auto *toggle = new QToolButton(m_body);
        toggle->setObjectName(QStringLiteral("motionInspectorKeyframesToggle"));
        toggle->setText(tr("Keyframes"));
        toggle->setCheckable(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setAutoRaise(true);
        toggle->setFocusPolicy(Qt::NoFocus);
        QStringList lines;
        for (const QJsonValue &value : track->keyframes) {
            const QJsonObject frame = value.toObject();
            QStringList props;
            const QJsonObject values = frame["props"].toObject();
            for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                props << QStringLiteral("%1 %2").arg(it.key(), it.value().toString());
            lines << QStringLiteral("%1%  %2").arg(qRound(frame["offset"].toDouble() * 100)).arg(props.join(QStringLiteral(", ")));
        }
        auto *list = label(lines.join(QLatin1Char('\n')), QStringLiteral("motionInspectorKeyframes"), m_body);
        list->setVisible(false);
        connect(toggle, &QToolButton::toggled, list, [toggle, list](bool open) {
            list->setVisible(open);
            toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        });
        column->addWidget(toggle);
        column->addWidget(list);
    }
    column->addStretch(1);
}
