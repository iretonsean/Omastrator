#include "UI/LiveReviewPanel.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QVBoxLayout>

namespace {
// Added lines in green, removed in red, as diffs read everywhere.
class DiffHighlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override
    {
        QTextCharFormat format;
        if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---")) || text.startsWith(QLatin1String("@@")))
            format.setFontWeight(QFont::Bold);
        else if (text.startsWith(QLatin1Char('+')))
            format.setForeground(QColor(0x3f, 0xb9, 0x50));
        else if (text.startsWith(QLatin1Char('-')))
            format.setForeground(QColor(0xf8, 0x51, 0x49));
        else
            return;
        setFormat(0, int(text.size()), format);
    }
};

QPushButton *button(const QString &text, const QString &name, QWidget *parent)
{
    auto *made = new QPushButton(text, parent);
    made->setObjectName(name);
    return made;
}
}

LiveReviewPanel::LiveReviewPanel(AgentBridge &bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge), m_outer(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("liveReview"));
    setFixedWidth(560);
    m_outer->setContentsMargins(18, 16, 18, 16);
    connect(&m_bridge, &AgentBridge::liveReviewChanged, this, &LiveReviewPanel::rebuild);
    connect(&m_bridge.liveSession(), &LiveSession::changed, this, &LiveReviewPanel::rebuild);
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &LiveReviewPanel::rebuild);
    rebuild();
}

void LiveReviewPanel::report(const QString &failure)
{
    if (auto *line = m_body ? m_body->findChild<QLabel *>(QStringLiteral("liveReviewMessage")) : nullptr) {
        line->setText(failure);
        line->setVisible(!failure.isEmpty());
    }
}

void LiveReviewPanel::rebuild()
{
    // A fresh body each time; the old one goes with everything in it.
    if (m_body)
        m_body->deleteLater();
    m_body = new QWidget(this);
    m_outer->addWidget(m_body);
    auto *m_column = new QVBoxLayout(m_body);
    m_column->setContentsMargins(0, 0, 0, 0);
    m_column->setSpacing(10);
    QWidget *const self = m_body;
    LiveSession &live = m_bridge.liveSession();
    auto *where = new QLabel(live.project().isEmpty() ? QStringLiteral("A mock-up: changes stay in the browser.")
                                                      : QStringLiteral("Writing to %1").arg(live.project()),
                             self);
    where->setWordWrap(true);
    m_column->addWidget(where);
    auto *message = new QLabel(m_bridge.liveMessage(), self);
    message->setObjectName(QStringLiteral("liveReviewMessage"));
    message->setWordWrap(true);
    message->setVisible(!m_bridge.liveMessage().isEmpty());
    m_column->addWidget(message);
    if (m_bridge.canConfirm()) {
        QPushButton *anyway = button(QStringLiteral("Go Ahead Anyway"), QStringLiteral("liveConfirm"), self);
        m_column->addWidget(anyway, 0, Qt::AlignLeft);
        connect(anyway, &QPushButton::clicked, this, [this] { report(m_bridge.confirmPending()); });
    }
    if (m_bridge.waiting() && m_bridge.waiting()->task == AgentBridge::Task::live) {
        auto *waiting = new QLabel(m_bridge.waitingText(), self);
        waiting->setObjectName(QStringLiteral("liveReviewWaiting"));
        m_column->addWidget(waiting);
    }
    for (const WriteBack::Review &review : m_bridge.liveReviews()) {
        auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(review.title.toHtmlEscaped()), self);
        m_column->addWidget(title);
        auto *summary = new QLabel(review.summary, self);
        summary->setWordWrap(true);
        m_column->addWidget(summary);
        auto *diff = new QPlainTextEdit(review.diff(), self);
        diff->setObjectName(QStringLiteral("liveReviewDiff"));
        diff->setReadOnly(true);
        diff->setLineWrapMode(QPlainTextEdit::NoWrap);
        diff->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        diff->setFixedHeight(200);
        new DiffHighlighter(diff->document());
        m_column->addWidget(diff);
        auto *row = new QHBoxLayout;
        row->addStretch();
        QPushButton *discard = button(QStringLiteral("Discard"), QStringLiteral("liveDiscard"), self);
        QPushButton *keep = button(QStringLiteral("Keep"), QStringLiteral("liveKeep"), self);
        keep->setDefault(true);
        row->addWidget(discard);
        row->addWidget(keep);
        m_column->addLayout(row);
        connect(keep, &QPushButton::clicked, this, [this, id = review.id] { report(m_bridge.keepReview(id)); });
        connect(discard, &QPushButton::clicked, this, [this, id = review.id] { report(m_bridge.discardReview(id)); });
    }
    if (m_bridge.liveReviews().empty() && live.edits().empty() && m_bridge.unsavedFiles() == 0) {
        auto *empty = new QLabel(QStringLiteral("Nothing to review. Edit the page, then Write Back."), self);
        empty->setObjectName(QStringLiteral("liveReviewEmpty"));
        m_column->addWidget(empty);
    }
    auto *actions = new QHBoxLayout;
    QPushButton *writeBack = button(QStringLiteral("Write Back (%1)").arg(live.edits().size()), QStringLiteral("liveWriteBack"), self);
    writeBack->setEnabled(!live.edits().empty() && !live.project().isEmpty() && live.state() == LiveSession::State::running);
    QPushButton *save = button(QStringLiteral("Save"), QStringLiteral("liveSave"), self);
    save->setEnabled(m_bridge.unsavedFiles() > 0 && m_bridge.liveReviews().empty());
    QPushButton *publish = button(QStringLiteral("Publish…"), QStringLiteral("livePublish"), self);
    publish->setEnabled(!m_bridge.publishOptions().empty());
    actions->addWidget(writeBack);
    actions->addStretch();
    actions->addWidget(save);
    actions->addWidget(publish);
    m_column->addLayout(actions);
    m_body->show();
    connect(writeBack, &QPushButton::clicked, this, [this] { report(m_bridge.liveWriteBack(false)); });
    connect(save, &QPushButton::clicked, this, [this] { report(m_bridge.liveSave()); });
    connect(publish, &QPushButton::clicked, this, [this] { AgentSheets::publish(m_bridge, window()); });
}
