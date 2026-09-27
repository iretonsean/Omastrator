#include "UI/AgentPanels.h"
#include "Agent/AgentLauncher.h"
#include "Document/EditorSession.h"
#include "IO/SvgImporter.h"
#include "Rendering/VectorRenderer.h"
#include "UI/AgentBridge.h"
#include <QComboBox>
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

// Opens the failed run's log; if it can't, says so in `message`.
QPushButton *showLogButton(AgentBridge &bridge, QWidget *parent, const QString &name, QLabel *message)
{
    auto *button = new QPushButton(QStringLiteral("Show log"), parent);
    button->setObjectName(name);
    QObject::connect(button, &QPushButton::clicked, parent, [&bridge, message] {
        if (const QString failure = bridge.showLog(); !failure.isEmpty() && message)
            message->setText(message->text() + QLatin1Char(' ') + failure);
    });
    return button;
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
      m_message(wrapped(QString(), this, QStringLiteral("variationsMessage"))),
      m_showLog(showLogButton(bridge, this, QStringLiteral("variationsShowLog"), m_message)), m_refine(new QLineEdit(this)),
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
    auto *failure = new QHBoxLayout;
    failure->addWidget(m_message, 1);
    failure->addWidget(m_showLog, 0, Qt::AlignTop);
    column->addLayout(failure);

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
    connect(&m_bridge, &AgentBridge::waitingTick, this, &VariationsPanel::synchronizeWaiting);
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
    m_showLog->setVisible(!message.isEmpty() && !m_bridge.logPath().isEmpty());
    synchronizeWaiting();
}

// Roast ------------------------------------------------------------------------------

RoastPanel::RoastPanel(AgentBridge &bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge), m_column(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("roast"));
    setFixedWidth(400);
    setMinimumHeight(360);
    setFocusPolicy(Qt::StrongFocus);
    m_column->setContentsMargins(18, 16, 18, 16);
    m_column->setSpacing(10);
    connect(&m_bridge, &AgentBridge::roastChanged, this, [this] {
        m_page = 0;
        rebuild();
    });
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &RoastPanel::rebuild);
    // Only the elapsed time changes: a rebuild would close an open heat menu.
    connect(&m_bridge, &AgentBridge::waitingTick, this, [this] {
        if (m_status)
            m_status->setText(m_bridge.waitingText());
    });
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
        m_status = status;
        auto *cancel = new QPushButton(QStringLiteral("Cancel"), &body);
        cancel->setObjectName(QStringLiteral("roastCancel"));
        connect(cancel, &QPushButton::clicked, &m_bridge, &AgentBridge::stopWaiting);
        row->addWidget(status, 1);
        row->addWidget(cancel);
        column.addLayout(row);
    }
    // How hard it hits: remembered, and one click to roast again at a new heat.
    auto *heatRow = new QHBoxLayout;
    auto *heatLabel = new QLabel(QStringLiteral("Heat"), &body);
    auto *heat = new QComboBox(&body);
    heat->setObjectName(QStringLiteral("roastHeat"));
    heat->setAccessibleName(QStringLiteral("Roast heat"));
    for (AgentLauncher::RoastHeat level : AgentLauncher::allRoastHeats)
        heat->addItem(AgentLauncher::title(level), int(level));
    heat->setCurrentIndex(int(AgentLauncher::savedRoastHeat()));
    heat->setToolTip(QStringLiteral("Friendly teases. Spicy bites. Savage is a Comedy Central roast. Unhinged has no brakes."));
    connect(heat, &QComboBox::activated, this, [heat] {
        AgentLauncher::saveRoastHeat(AgentLauncher::RoastHeat(heat->currentData().toInt()));
    });
    auto *again = new QPushButton(QStringLiteral("Roast Again"), &body);
    again->setObjectName(QStringLiteral("roastAgain"));
    again->setEnabled(!waiting);
    connect(again, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(&m_bridge, [this] { m_bridge.roast(); }, Qt::QueuedConnection);
    });
    heatRow->addWidget(heatLabel);
    heatRow->addWidget(heat, 1);
    heatRow->addWidget(again);
    column.addLayout(heatRow);
    auto *message = wrapped(m_bridge.panelMessage(), &body, QStringLiteral("roastMessage"));
    message->setVisible(!m_bridge.panelMessage().isEmpty());
    if (!m_bridge.panelMessage().isEmpty() && !m_bridge.logPath().isEmpty()) {
        auto *row = new QHBoxLayout;
        row->addWidget(message, 1);
        row->addWidget(showLogButton(m_bridge, &body, QStringLiteral("roastShowLog"), message), 0, Qt::AlignTop);
        column.addLayout(row);
    } else {
        column.addWidget(message);
    }
    const std::optional<AgentRoast> &roast = m_bridge.roastResult();
    if (!roast) {
        if (!waiting && m_bridge.panelMessage().isEmpty())
            column.addWidget(wrapped(QStringLiteral("No roast yet."), &body));
        return;
    }
    // Three short pages instead of one long read: the roast, the fixes, what next.
    static const char *titles[] = {"The roast", "The fixes", "What next"};
    constexpr int pages = 3;
    m_page = std::clamp(m_page, 0, pages - 1);
    auto *pageTitle = new QLabel(QStringLiteral("<b>%1</b>").arg(QString::fromLatin1(titles[m_page])), &body);
    pageTitle->setObjectName(QStringLiteral("roastPageTitle"));
    column.addWidget(pageTitle);
    if (m_page == 0) {
        auto *text = new QWidget(&body);
        text->setObjectName(QStringLiteral("roastText"));
        auto *lines = new QVBoxLayout(text);
        lines->setContentsMargins(0, 0, 0, 0);
        lines->setSpacing(12);
        // One burn a line, each given room to land.
        for (const QString &line : roast->roast.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            auto *label = wrapped(line.trimmed(), text);
            QFont loud = label->font();
            loud.setPointSizeF(loud.pointSizeF() * 1.25);
            loud.setWeight(QFont::DemiBold);
            label->setFont(loud);
            lines->addWidget(label);
        }
        column.addWidget(text);
    } else if (m_page == 1) {
        for (size_t index = 0; index < roast->feedback.size(); ++index) {
            const AgentFeedback &feedback = roast->feedback[index];
            auto *item = new QCommandLinkButton(feedback.title, feedback.detail, &body);
            item->setObjectName(QStringLiteral("feedback:%1").arg(index));
            item->setEnabled(!feedback.objectIds.empty());
            item->setToolTip(feedback.objectIds.empty() ? QString() : QStringLiteral("Select the objects this is about"));
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
    } else {
        auto *brief = wrapped(QStringLiteral("“%1”").arg(roast->suggestedPrompt.toHtmlEscaped()), &body, QStringLiteral("roastBrief"));
        column.addWidget(brief);
        auto *make = new QPushButton(QStringLiteral("Make variations from this feedback"), &body);
        make->setObjectName(QStringLiteral("makeVariations"));
        make->setEnabled(!waiting);
        connect(make, &QPushButton::clicked, this, [this, message] {
            const QString error = m_bridge.generate(m_bridge.roastResult()->suggestedPrompt, 3, false);
            message->setText(error);
            message->setVisible(!error.isEmpty());
        });
        column.addWidget(make);
    }
    column.addStretch(1);
    column.addWidget(separator(&body, QStringLiteral("roastSeparator")));
    auto *nav = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("Back"), &body);
    back->setObjectName(QStringLiteral("roastBack"));
    back->setEnabled(m_page > 0);
    auto *where = new QLabel(QStringLiteral("%1 of %2").arg(m_page + 1).arg(pages), &body);
    where->setObjectName(QStringLiteral("roastPage"));
    where->setAlignment(Qt::AlignCenter);
    auto *next = new QPushButton(m_page + 1 < pages ? QString::fromLatin1(titles[m_page + 1]) : QStringLiteral("Next"), &body);
    next->setObjectName(QStringLiteral("roastNext"));
    next->setEnabled(m_page + 1 < pages);
    next->setDefault(true);
    connect(back, &QPushButton::clicked, this, [this] { showPage(m_page - 1); });
    connect(next, &QPushButton::clicked, this, [this] { showPage(m_page + 1); });
    nav->addWidget(back);
    nav->addWidget(where, 1);
    nav->addWidget(next);
    column.addLayout(nav);
}

void RoastPanel::showPage(int page)
{
    m_page = std::clamp(page, 0, 2);
    // Deferred: the clicked button belongs to the body being replaced.
    QMetaObject::invokeMethod(this, &RoastPanel::rebuild, Qt::QueuedConnection);
}

void RoastPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Left) {
        showPage(m_page + (event->key() == Qt::Key_Right ? 1 : -1));
        return;
    }
    QWidget::keyPressEvent(event);
}

// Proposal bar -----------------------------------------------------------------------

ProposalBar::ProposalBar(AgentBridge &bridge, EditorSession &session, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_session(session), m_text(new QLabel(this)), m_summary(new QLabel(this)),
      m_keep(new QPushButton(QStringLiteral("Keep"), this)), m_discard(new QPushButton(QStringLiteral("Discard"), this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this)),
      m_showLog(showLogButton(bridge, this, QStringLiteral("proposalShowLog"), nullptr)),
      m_dismiss(new QPushButton(QStringLiteral("Dismiss"), this))
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
    m_dismiss->setObjectName(QStringLiteral("proposalDismiss"));
    m_keep->setDefault(true);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 6, 10, 6);
    row->setSpacing(10);
    row->addWidget(m_text);
    row->addWidget(m_summary, 1);
    row->addWidget(m_cancel);
    row->addWidget(m_showLog);
    row->addWidget(m_dismiss);
    row->addWidget(m_discard);
    row->addWidget(m_keep);
    connect(m_keep, &QPushButton::clicked, &m_bridge, &AgentBridge::keepProposal);
    connect(m_discard, &QPushButton::clicked, &m_bridge, &AgentBridge::discardProposal);
    connect(m_cancel, &QPushButton::clicked, &m_bridge, &AgentBridge::stopWaiting);
    connect(m_dismiss, &QPushButton::clicked, &m_bridge, &AgentBridge::dismissBarMessage);
    connect(&m_bridge, &AgentBridge::waitingTick, this, &ProposalBar::synchronize);
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
    // An edit or trace that stopped without an answer, on the canvas it was meant for.
    const bool failed = !working && !m_bridge.barMessage().isEmpty() && (proposal || &m_session == m_bridge.session());
    if (proposal)
        m_text->setText(QStringLiteral("<b>%1</b>: Enter keeps it, Esc discards it.").arg(m_bridge.proposalTitle().toHtmlEscaped()));
    else if (working)
        m_text->setText(m_bridge.waitingText());
    else if (failed)
        m_text->setText(m_bridge.barMessage().toHtmlEscaped());
    // With a proposal on show, the progress and the failure go where its summary would.
    if (proposal && working)
        m_summary->setText(m_bridge.waitingText());
    else if (proposal && failed)
        m_summary->setText(m_bridge.barMessage());
    else
        m_summary->setText(proposal ? m_bridge.proposalSummary() : QString());
    m_summary->setToolTip(m_summary->text());
    m_keep->setVisible(proposal);
    m_discard->setVisible(proposal);
    // Still working with a proposal on show: the agent may add to it.
    m_cancel->setVisible(working);
    m_showLog->setVisible(failed && !m_bridge.logPath().isEmpty());
    m_dismiss->setVisible(failed && !proposal);
    setVisible(proposal || working || failed);
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
