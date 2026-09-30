#include "UI/AnimateSheet.h"
#include <QAction>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

QStringList AnimateSheet::suggestions()
{
    return {QStringLiteral("Reveal word by word on load"), QStringLiteral("Fade up as it scrolls in"), QStringLiteral("Lift on hover"),
            QStringLiteral("Stagger the cards")};
}

AnimateSheet::AnimateSheet(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("animateSheet"));
    setAccessibleName(QStringLiteral("Animate"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setFixedWidth(420);
    hide();
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(8);

    m_field = new QPlainTextEdit(this);
    m_field->setObjectName(QStringLiteral("animateField"));
    m_field->setPlaceholderText(tr("Describe the motion"));
    m_field->setAccessibleName(tr("Describe the motion"));
    m_field->setFixedHeight(64);
    m_field->installEventFilter(this);
    column->addWidget(m_field);

    m_chips = new QWidget(this);
    auto *chips = new QHBoxLayout(m_chips);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    auto *flow = new QVBoxLayout;
    flow->setContentsMargins(0, 0, 0, 0);
    flow->setSpacing(4);
    QHBoxLayout *line = nullptr;
    int width = 0;
    for (const QString &suggestion : suggestions()) {
        auto *chip = new QPushButton(suggestion, m_chips);
        chip->setObjectName(QStringLiteral("animateChip"));
        chip->setFlat(true);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::NoFocus);
        const int need = chip->sizeHint().width() + 6;
        if (!line || width + need > 390) {
            line = new QHBoxLayout;
            line->setContentsMargins(0, 0, 0, 0);
            line->setSpacing(6);
            flow->addLayout(line);
            width = 0;
        }
        width += need;
        line->addWidget(chip);
        // A chip fills the field, and the designer edits from there.
        connect(chip, &QPushButton::clicked, this, [this, suggestion] {
            m_field->setPlainText(suggestion);
            m_field->setFocus();
            m_field->moveCursor(QTextCursor::End);
        });
    }
    if (line)
        line->addStretch(1);
    delete chips;
    m_chips->setLayout(flow);
    column->addWidget(m_chips);

    auto *actions = new QHBoxLayout;
    m_agent = new QLabel(this);
    m_agent->setObjectName(QStringLiteral("animateAgent"));
    m_more = new QToolButton(this);
    m_more->setObjectName(QStringLiteral("animateMore"));
    m_more->setText(QStringLiteral("⋯"));
    m_more->setToolTip(tr("More"));
    m_more->setAccessibleName(tr("More"));
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setFocusPolicy(Qt::NoFocus);
    auto *menu = new QMenu(m_more);
    QAction *reduced = menu->addAction(tr("Write a reduced-motion version"));
    reduced->setObjectName(QStringLiteral("animateReduced"));
    reduced->setCheckable(true);
    reduced->setChecked(true);
    connect(reduced, &QAction::toggled, this, [this](bool on) { m_reduced = on; });
    m_more->setMenu(menu);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setObjectName(QStringLiteral("animateCancel"));
    m_generate = new QPushButton(tr("Generate"), this);
    m_generate->setObjectName(QStringLiteral("animateGenerate"));
    m_generate->setDefault(true);
    actions->addWidget(m_agent);
    actions->addStretch(1);
    actions->addWidget(m_more);
    actions->addWidget(m_cancel);
    actions->addWidget(m_generate);
    column->addLayout(actions);

    m_steps = new QLabel(this);
    m_steps->setObjectName(QStringLiteral("animateSteps"));
    m_steps->setWordWrap(true);
    m_steps->hide();
    column->addWidget(m_steps);
    m_note = new QLabel(this);
    m_note->setObjectName(QStringLiteral("animateNote"));
    m_note->setWordWrap(true);
    QPalette dim = m_note->palette();
    QColor color = dim.color(QPalette::WindowText);
    color.setAlphaF(0.6f);
    dim.setColor(QPalette::WindowText, color);
    m_note->setPalette(dim);
    column->addWidget(m_note);

    connect(m_cancel, &QPushButton::clicked, this, [this] { m_running ? emit stopped() : emit cancelled(); });
    connect(m_generate, &QPushButton::clicked, this, [this] {
        if (m_running)
            return;
        emit generate(text(), m_reduced);
    });
    refresh();
}

void AnimateSheet::open(const QString &agent, int elements)
{
    m_name = agent;
    m_elements = std::max(1, elements);
    if (!m_running)
        m_field->clear();
    refresh();
    adjustSize();
    show();
    raise();
    m_field->setFocus();
}

void AnimateSheet::close()
{
    if (!m_running)
        m_field->clear();
    hide();
}

QString AnimateSheet::text() const
{
    return m_field->toPlainText().trimmed();
}

void AnimateSheet::setRunning(bool running)
{
    m_running = running;
    refresh();
}

void AnimateSheet::setSteps(const QStringList &lines)
{
    m_steps->setText(lines.join(QLatin1Char('\n')));
    m_steps->setVisible(m_running && !lines.isEmpty());
    adjustSize();
}

void AnimateSheet::refresh()
{
    const QString agent = m_name.isEmpty() ? tr("your agent") : m_name;
    m_agent->setText(tr("Your default agent: %1").arg(agent));
    m_generate->setText(m_running ? tr("Writing…") : tr("Generate"));
    m_generate->setEnabled(!m_running);
    m_cancel->setText(m_running ? tr("Stop") : tr("Cancel"));
    m_field->setReadOnly(m_running);
    m_chips->setVisible(!m_running);
    m_more->setEnabled(!m_running);
    m_note->setText(tr("%1 writes the motion as real CSS in your project, on a branch. You preview it here before anything is saved.").arg(agent));
    if (!m_running)
        m_steps->hide();
}

void AnimateSheet::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_running ? emit stopped() : emit cancelled();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool AnimateSheet::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            m_running ? emit stopped() : emit cancelled();
            return true;
        }
        // Enter writes; Shift+Enter is a new line.
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            if (!m_running)
                emit generate(text(), m_reduced);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}
