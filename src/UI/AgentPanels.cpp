#include "UI/AgentPanels.h"
#include "Document/EditorSession.h"
#include "IO/SvgImporter.h"
#include "Rendering/VectorRenderer.h"
#include "UI/AgentBridge.h"
#include <QCommandLinkButton>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int thumbnailSide = 112;

QLabel *wrapped(const QString &text, QWidget *parent, const QString &name = QString())
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QFrame *separator(QWidget *parent, const QString &name)
{
    auto *line = new QFrame(parent);
    line->setObjectName(name);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

void clear(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        } else if (QLayout *nested = item->layout()) {
            clear(nested);
            delete nested;
        }
        delete item;
    }
}
}

// Variations ---------------------------------------------------------------------

VariationsPanel::VariationsPanel(AgentBridge &bridge, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_status(new QLabel(this)), m_cancel(new QPushButton(QStringLiteral("Cancel"), this)),
      m_message(wrapped(QString(), this, QStringLiteral("variationsMessage"))), m_refine(new QLineEdit(this)),
      m_refineButton(new QPushButton(QStringLiteral("Refine"), this))
{
    setObjectName(QStringLiteral("variations"));
    setFixedWidth(452);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(16, 14, 16, 14);
    column->setSpacing(10);
    auto *waiting = new QHBoxLayout;
    m_status->setObjectName(QStringLiteral("variationsStatus"));
    m_cancel->setObjectName(QStringLiteral("variationsCancel"));
    waiting->addWidget(m_status, 1);
    waiting->addWidget(m_cancel);
    column->addLayout(waiting);
    column->addWidget(m_message);

    auto *roundsHolder = new QWidget(this);
    m_rounds = new QVBoxLayout(roundsHolder);
    m_rounds->setContentsMargins(0, 0, 0, 0);
    m_rounds->setSpacing(14);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("variationsRounds"));
    scroll->setWidget(roundsHolder);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(420);
    column->addWidget(scroll);

    auto *refine = new QHBoxLayout;
    m_refine->setObjectName(QStringLiteral("refineField"));
    m_refine->setPlaceholderText(QStringLiteral("Refine the chosen variation: rounder, fewer colours…"));
    m_refineButton->setObjectName(QStringLiteral("refineButton"));
    refine->addWidget(m_refine, 1);
    refine->addWidget(m_refineButton);
    column->addLayout(refine);

    connect(m_cancel, &QPushButton::clicked, &m_bridge, &AgentBridge::stopWaiting);
    const auto refineNow = [this] {
        const QString error = m_bridge.refine(m_refine->text());
        m_message->setText(error);
        m_message->setVisible(!error.isEmpty());
        if (error.isEmpty())
            m_refine->clear();
    };
    connect(m_refineButton, &QPushButton::clicked, this, refineNow);
    connect(m_refine, &QLineEdit::returnPressed, this, refineNow);
    connect(m_refine, &QLineEdit::textChanged, this, &VariationsPanel::synchronizeWaiting);
    connect(&m_bridge, &AgentBridge::variationsChanged, this, &VariationsPanel::rebuild);
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &VariationsPanel::synchronizeWaiting);
    rebuild();
}

QImage VariationsPanel::thumbnail(const QString &svg, int side)
{
    try {
        const VectorDocument document = SvgImporter::parse(svg.toUtf8());
        const double scale = side / std::max(document.size.width(), document.size.height());
        return VectorRenderer::render(document, scale, false);
    } catch (const FileError &) {
        return {};
    }
}

void VariationsPanel::synchronizeWaiting()
{
    const bool generating = m_bridge.waiting() && m_bridge.waiting()->task == AgentBridge::Task::generate;
    m_status->setText(generating ? m_bridge.waitingText() : QString());
    m_status->setVisible(generating);
    m_cancel->setVisible(generating);
    m_refineButton->setEnabled(m_bridge.chosen().has_value() && !m_refine->text().trimmed().isEmpty() && !generating);
    m_refine->setEnabled(m_bridge.chosen().has_value());
}

void VariationsPanel::rebuild()
{
    clear(m_rounds);
    const auto &rounds = m_bridge.rounds();
    // Newest first; earlier rounds stay below to go back to.
    for (int roundIndex = int(rounds.size()) - 1; roundIndex >= 0; --roundIndex) {
        const AgentBridge::Round &round = rounds[size_t(roundIndex)];
        auto *block = new QWidget;
        auto *blockColumn = new QVBoxLayout(block);
        blockColumn->setContentsMargins(0, 0, 0, 0);
        auto *header = wrapped(QStringLiteral("<b>Round %1</b> · %2").arg(roundIndex + 1).arg(round.instruction.toHtmlEscaped()), block);
        header->setTextFormat(Qt::RichText);
        blockColumn->addWidget(header);
        if (round.variations.empty())
            blockColumn->addWidget(wrapped(QStringLiteral("No variations yet."), block));
        auto *grid = new QGridLayout;
        grid->setSpacing(8);
        for (int index = 0; index < int(round.variations.size()); ++index) {
            const AgentVariation &variation = round.variations[size_t(index)];
            auto *tile = new QToolButton(block);
            tile->setObjectName(QStringLiteral("variation:%1:%2").arg(roundIndex).arg(index));
            tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            tile->setIcon(QPixmap::fromImage(thumbnail(variation.svg, thumbnailSide)));
            tile->setIconSize(QSize(thumbnailSide, thumbnailSide));
            tile->setText(variation.name);
            tile->setToolTip(variation.note.isEmpty() ? variation.name : variation.name + QStringLiteral(": ") + variation.note);
            tile->setAccessibleName(variation.name);
            tile->setAccessibleDescription(variation.note);
            tile->setCheckable(true);
            tile->setChecked(m_bridge.chosen() == std::pair(roundIndex, index));
            tile->setFixedSize(thumbnailSide + 20, thumbnailSide + 36);
            connect(tile, &QToolButton::clicked, this, [this, roundIndex, index] {
                const QString error = m_bridge.insertVariation(roundIndex, index);
                m_message->setText(error);
                m_message->setVisible(!error.isEmpty());
            });
            grid->addWidget(tile, index / 3, index % 3);
        }
        blockColumn->addLayout(grid);
        m_rounds->addWidget(block);
        // Added to a shown panel: a new child waits for show().
        block->show();
    }
    m_rounds->addStretch(1);
    const QString message = m_bridge.panelMessage();
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
    synchronizeWaiting();
}

// Roast ------------------------------------------------------------------------------

RoastPanel::RoastPanel(AgentBridge &bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge), m_column(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("roast"));
    setFixedWidth(420);
    m_column->setContentsMargins(18, 16, 18, 16);
    m_column->setSpacing(10);
    connect(&m_bridge, &AgentBridge::roastChanged, this, &RoastPanel::rebuild);
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &RoastPanel::rebuild);
    rebuild();
}

void RoastPanel::rebuild()
{
    clear(m_column);
    // A fresh body each time: built hidden, then shown whole.
    auto *body = new QWidget(this);
    auto *column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_column->addWidget(body);
    buildBody(*body, *column);
    body->show();
}

void RoastPanel::buildBody(QWidget &body, QVBoxLayout &column)
{
    const bool waiting = m_bridge.waiting() && m_bridge.waiting()->task == AgentBridge::Task::roast;
    if (waiting) {
        auto *row = new QHBoxLayout;
        auto *status = new QLabel(m_bridge.waitingText(), &body);
        status->setObjectName(QStringLiteral("roastStatus"));
        auto *cancel = new QPushButton(QStringLiteral("Cancel"), &body);
        cancel->setObjectName(QStringLiteral("roastCancel"));
        connect(cancel, &QPushButton::clicked, &m_bridge, &AgentBridge::stopWaiting);
        row->addWidget(status, 1);
        row->addWidget(cancel);
        column.addLayout(row);
    }
    auto *message = wrapped(m_bridge.panelMessage(), &body, QStringLiteral("roastMessage"));
    message->setVisible(!m_bridge.panelMessage().isEmpty());
    column.addWidget(message);
    const std::optional<AgentRoast> &roast = m_bridge.roastResult();
    if (!roast) {
        if (!waiting && m_bridge.panelMessage().isEmpty())
            column.addWidget(wrapped(QStringLiteral("No roast yet."), &body));
        return;
    }
    auto *text = wrapped(roast->roast, &body, QStringLiteral("roastText"));
    QFont loud = text->font();
    loud.setPointSizeF(loud.pointSizeF() * 1.1);
    text->setFont(loud);
    column.addWidget(text);
    column.addWidget(separator(&body, QStringLiteral("roastSeparator")));
    auto *heading = new QLabel(QStringLiteral("<b>What would actually help</b>"), &body);
    heading->setObjectName(QStringLiteral("feedbackHeading"));
    column.addWidget(heading);
    for (size_t index = 0; index < roast->feedback.size(); ++index) {
        const AgentFeedback &feedback = roast->feedback[index];
        auto *item = new QCommandLinkButton(feedback.title, feedback.detail, &body);
        item->setObjectName(QStringLiteral("feedback:%1").arg(index));
        item->setEnabled(!feedback.objectIds.empty());
        const std::vector<QUuid> ids = feedback.objectIds;
        connect(item, &QCommandLinkButton::clicked, this, [this, ids] {
            EditorSession *session = m_bridge.session();
            if (!session->hasDocument())
                return;
            std::vector<QUuid> present;
            for (const QUuid &id : ids) {
                if (session->document()->find(id))
                    present.push_back(id);
            }
            session->select(present);
        });
        column.addWidget(item);
    }
    auto *make = new QPushButton(QStringLiteral("Make variations from this feedback"), &body);
    make->setObjectName(QStringLiteral("makeVariations"));
    make->setToolTip(roast->suggestedPrompt);
    make->setEnabled(!waiting);
    connect(make, &QPushButton::clicked, this, [this, message] {
        const QString error = m_bridge.generate(m_bridge.roastResult()->suggestedPrompt, 3, false);
        message->setText(error);
        message->setVisible(!error.isEmpty());
    });
    column.addWidget(make);
}

// Proposal bar -----------------------------------------------------------------------

ProposalBar::ProposalBar(AgentBridge &bridge, EditorSession &session, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_session(session), m_text(new QLabel(this)), m_summary(new QLabel(this)),
      m_keep(new QPushButton(QStringLiteral("Keep"), this)), m_discard(new QPushButton(QStringLiteral("Discard"), this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this))
{
    setObjectName(QStringLiteral("proposalBar"));
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::AlternateBase);
    m_text->setObjectName(QStringLiteral("proposalText"));
    m_summary->setObjectName(QStringLiteral("proposalSummary"));
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_keep->setObjectName(QStringLiteral("proposalKeep"));
    m_discard->setObjectName(QStringLiteral("proposalDiscard"));
    m_cancel->setObjectName(QStringLiteral("proposalCancel"));
    m_keep->setDefault(true);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 6, 10, 6);
    row->setSpacing(10);
    row->addWidget(m_text);
    row->addWidget(m_summary, 1);
    row->addWidget(m_cancel);
    row->addWidget(m_discard);
    row->addWidget(m_keep);
    connect(m_keep, &QPushButton::clicked, &m_bridge, &AgentBridge::keepProposal);
    connect(m_discard, &QPushButton::clicked, &m_bridge, &AgentBridge::discardProposal);
    connect(m_cancel, &QPushButton::clicked, &m_bridge, &AgentBridge::stopWaiting);
    connect(&m_bridge, &AgentBridge::proposalChanged, this, &ProposalBar::synchronize);
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &ProposalBar::synchronize);
    connect(&m_session, &EditorSession::changed, this, &ProposalBar::synchronize);
    synchronize();
}

void ProposalBar::synchronize()
{
    const bool proposal = m_bridge.hasProposalIn(m_session);
    const auto &waiting = m_bridge.waiting();
    const bool working = waiting && (waiting->task == AgentBridge::Task::edit || waiting->task == AgentBridge::Task::vectorize);
    if (proposal)
        m_text->setText(QStringLiteral("<b>%1</b>: Enter keeps it, Esc discards it.").arg(m_bridge.proposalTitle().toHtmlEscaped()));
    else if (working)
        m_text->setText(m_bridge.waitingText());
    m_summary->setText(proposal ? m_bridge.proposalSummary() : QString());
    m_summary->setToolTip(m_summary->text());
    m_keep->setVisible(proposal);
    m_discard->setVisible(proposal);
    // Still working with a proposal on show: the agent may add to it.
    m_cancel->setVisible(working);
    setVisible(proposal || working);
}

void ProposalBar::keyPressEvent(QKeyEvent *event)
{
    if (m_bridge.hasProposalIn(m_session) && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        m_bridge.keepProposal();
        return;
    }
    if (m_bridge.hasProposalIn(m_session) && event->key() == Qt::Key_Escape) {
        m_bridge.discardProposal();
        return;
    }
    QWidget::keyPressEvent(event);
}
