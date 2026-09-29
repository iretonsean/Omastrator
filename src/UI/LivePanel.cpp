#include "UI/LivePanel.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSyntaxHighlighter>
#include <QUrl>
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

QLabel *label(const QString &text, const QString &name, QWidget *parent)
{
    auto *made = new QLabel(text, parent);
    made->setObjectName(name);
    made->setWordWrap(true);
    return made;
}
}

LivePanel::LivePanel(AgentBridge &bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge), m_outer(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("livePanel"));
    setFixedWidth(560);
    m_outer->setContentsMargins(18, 16, 18, 16);
    connect(&m_bridge, &AgentBridge::liveReviewChanged, this, &LivePanel::rebuild);
    connect(&m_bridge.liveSession(), &LiveSession::changed, this, &LivePanel::rebuild);
    connect(&m_bridge, &AgentBridge::waitingChanged, this, &LivePanel::rebuild);
    rebuild();
}

void LivePanel::showChanges(bool shown)
{
    if (m_changes == shown)
        return;
    m_changes = shown;
    rebuild();
}

void LivePanel::report(const QString &failure)
{
    if (auto *line = m_body ? m_body->findChild<QLabel *>(QStringLiteral("liveReviewMessage")) : nullptr) {
        line->setText(failure);
        line->setVisible(!failure.isEmpty());
    }
}

void LivePanel::addSite(QVBoxLayout *column)
{
    QWidget *const self = m_body;
    LiveSession &live = m_bridge.liveSession();
    const std::vector<EditSets::Set> sets = live.editSets();
    const int pending = int(live.edits().size());
    column->addWidget(label(pending == 0 ? QStringLiteral("No edits waiting to be kept.")
                                         : QStringLiteral("%1 edits not kept yet. Keep them as an edit set to have them back next visit.").arg(pending),
                            QStringLiteral("liveSitePending"), self));
    for (const EditSets::Set &set : sets) {
        auto *box = new QCheckBox(QStringLiteral("%1 (%2 edits)").arg(set.name, QString::number(set.edits.size())), self);
        box->setObjectName(QStringLiteral("liveEditSet"));
        box->setChecked(set.enabled);
        box->setToolTip(QStringLiteral("Shown on %1 every time it opens in Omastrator").arg(live.origin()));
        column->addWidget(box);
        connect(box, &QCheckBox::toggled, this,
                [this, name = set.name](bool on) { QMetaObject::invokeMethod(this, [this, name, on] { report(m_bridge.liveSession().setEditSetEnabled(name, on)); }, Qt::QueuedConnection); });
    }
    auto *row = new QHBoxLayout;
    auto *name = new QLineEdit(self);
    name->setObjectName(QStringLiteral("liveEditSetName"));
    name->setPlaceholderText(EditSets::suggestedName(live.origin()));
    QPushButton *keep = button(QStringLiteral("Keep Edits"), QStringLiteral("liveKeepEdits"), self);
    keep->setEnabled(pending > 0);
    keep->setToolTip(QStringLiteral("Keep the edits on this machine as a named set that comes back when you revisit the site"));
    row->addWidget(name, 1);
    row->addWidget(keep);
    column->addLayout(row);
    connect(keep, &QPushButton::clicked, this, [this, name] { report(m_bridge.liveSession().keepEdits(name->text())); });

    auto *more = new QHBoxLayout;
    const bool any = !live.editsShown().empty();
    QPushButton *exportCss = button(QStringLiteral("Export CSS…"), QStringLiteral("liveExportEdits"), self);
    exportCss->setToolTip(QStringLiteral("The edits on the page as a style sheet or a userstyle"));
    QPushButton *beforeAfter = button(QStringLiteral("Before and After to Desk"), QStringLiteral("liveBeforeAfter"), self);
    beforeAfter->setToolTip(QStringLiteral("The page lifted without its edits and with them, as two frames on the Desk"));
    QPushButton *handOff = button(QStringLiteral("Hand to Agent…"), QStringLiteral("liveHandOff"), self);
    handOff->setToolTip(QStringLiteral("Your agent builds these edits into your own app's source"));
    exportCss->setEnabled(any);
    beforeAfter->setEnabled(any);
    for (QPushButton *each : {exportCss, beforeAfter, handOff})
        more->addWidget(each);
    more->addStretch();
    column->addLayout(more);
    auto run = [this](const QString &action) {
        QJsonObject result;
        report(m_bridge.siteAction(action, {}, result));
    };
    connect(exportCss, &QPushButton::clicked, this, [run] { run(QStringLiteral("export")); });
    connect(beforeAfter, &QPushButton::clicked, this, [run] { run(QStringLiteral("beforeAfter")); });
    connect(handOff, &QPushButton::clicked, this, [run] { run(QStringLiteral("handoff")); });
}

void LivePanel::rebuild()
{
    // A fresh body each time; the old one goes with everything in it.
    if (m_body)
        m_body->deleteLater();
    m_body = new QWidget(this);
    m_outer->addWidget(m_body);
    auto *column = new QVBoxLayout(m_body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    QWidget *const self = m_body;
    const AgentBridge::DeployState &state = m_bridge.deployState();
    const QString project = m_bridge.deployProject();
    LiveSession &live = m_bridge.liveSession();
    // A site that isn't yours: its edits stay on this machine, and there's nothing to deploy.
    const bool mockup = live.state() == LiveSession::State::running && live.isMockup();

    column->addWidget(label(mockup ? QStringLiteral("Not your site: changes stay on this machine.")
                                   : project.isEmpty() ? QStringLiteral("Open a project in Live to deploy it.") : project,
                            QStringLiteral("liveProject"), self));
    if (mockup)
        addSite(column);
    QLabel *status = label(state.message, QStringLiteral("liveStatus"), self);
    status->setVisible(!state.message.isEmpty());
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(status);
    QLabel *message = label(m_bridge.liveMessage(), QStringLiteral("liveReviewMessage"), self);
    message->setVisible(!m_bridge.liveMessage().isEmpty());
    column->addWidget(message);
    if (!m_bridge.liveMessage().isEmpty() && !m_bridge.liveLog().isEmpty()) {
        QPushButton *log = button(QStringLiteral("Show Log"), QStringLiteral("liveShowLog"), self);
        log->setToolTip(QStringLiteral("What the agent printed before it stopped"));
        column->addWidget(log, 0, Qt::AlignLeft);
        connect(log, &QPushButton::clicked, this, [this] { report(m_bridge.showLiveLog()); });
    }
    if (m_bridge.waiting() && m_bridge.waiting()->task == AgentBridge::Task::live)
        column->addWidget(label(m_bridge.waitingText(), QStringLiteral("liveReviewWaiting"), self));

    // Deploy first: it writes, commits, pushes and deploys in one go.
    auto *actions = new QHBoxLayout;
    const int edits = live.project().isEmpty() ? 0 : int(live.edits().size());
    QPushButton *deploy = button(edits > 0 ? QStringLiteral("Deploy (%1)").arg(edits) : QStringLiteral("Deploy"), QStringLiteral("liveDeploy"), self);
    deploy->setDefault(true);
    deploy->setEnabled(!project.isEmpty() && !state.running);
    deploy->setToolTip(QStringLiteral("Write the live edits into the code, commit, push, and deploy to production"));
    QPushButton *save = button(QStringLiteral("Save"), QStringLiteral("liveSave"), self);
    save->setEnabled(!project.isEmpty() && !state.running);
    save->setToolTip(QStringLiteral("Write the live edits into the code, commit and push, without deploying"));
    actions->addWidget(deploy);
    actions->addWidget(save);
    deploy->setVisible(!mockup);
    save->setVisible(!mockup);
    if (state.running) {
        QPushButton *cancel = button(QStringLiteral("Cancel"), QStringLiteral("liveCancel"), self);
        actions->addWidget(cancel);
        connect(cancel, &QPushButton::clicked, this, [this] { m_bridge.cancelDeploy(); });
    }
    actions->addStretch();
    if (!state.log.isEmpty()) {
        QPushButton *details = button(QStringLiteral("Details"), QStringLiteral("liveDetails"), self);
        details->setToolTip(QStringLiteral("The deploy's log"));
        actions->addWidget(details);
        connect(details, &QPushButton::clicked, this, [this] { report(m_bridge.showDeployLog()); });
    }
    if (!state.url.isEmpty()) {
        QPushButton *open = button(QStringLiteral("Open Site"), QStringLiteral("liveOpenSite"), self);
        actions->addWidget(open);
        connect(open, &QPushButton::clicked, this, [url = state.url] { QDesktopServices::openUrl(QUrl(url)); });
    }
    column->addLayout(actions);
    connect(deploy, &QPushButton::clicked, this, [this] {
        bool needsAnswer = false;
        report(m_bridge.liveDeploy({}, &needsAnswer));
        if (needsAnswer)
            AgentSheets::deploy(m_bridge, window());
    });
    connect(save, &QPushButton::clicked, this, [this] { report(m_bridge.liveSave()); });

    if (!state.suggested.isEmpty()) {
        QPushButton *remember = button(QStringLiteral("Remember This Command"), QStringLiteral("liveRemember"), self);
        remember->setToolTip(QStringLiteral("Save %1 in the project's omastrator.json, so the next deploy runs it").arg(state.suggested));
        auto *row = new QHBoxLayout;
        row->addWidget(label(QStringLiteral("Your agent deployed with %1").arg(state.suggested), QStringLiteral("liveSuggested"), self), 1);
        row->addWidget(remember);
        column->addLayout(row);
        connect(remember, &QPushButton::clicked, this, [this] { report(m_bridge.rememberSuggested()); });
    }

    // GitHub keeps the history; gh holds the login.
    if (!project.isEmpty() && !mockup) {
        const GitHub::Auth auth = m_bridge.githubAuth();
        const QString page = History::githubPage(project);
        auto *row = new QHBoxLayout;
        QString text;
        if (!auth.installed)
            text = QStringLiteral("GitHub: install the gh CLI to keep history there (sudo pacman -S github-cli).");
        else if (!auth.loggedIn)
            text = QStringLiteral("GitHub isn't connected.");
        else
            text = page.isEmpty() ? QStringLiteral("GitHub: signed in as %1. This project isn't on GitHub yet; Deploy offers to create it.").arg(auth.account)
                                  : QStringLiteral("GitHub: %1").arg(page);
        row->addWidget(label(text, QStringLiteral("liveGitHub"), self), 1);
        if (auth.installed && !auth.loggedIn) {
            QPushButton *connectButton = button(QStringLiteral("Connect GitHub"), QStringLiteral("liveConnectGitHub"), self);
            connectButton->setToolTip(QStringLiteral("Opens a terminal running gh auth login"));
            row->addWidget(connectButton);
            connect(connectButton, &QPushButton::clicked, this, [this] { report(m_bridge.connectGitHub()); });
        }
        column->addLayout(row);
    }

    // The record: out of the way until asked for.
    auto *secondary = new QHBoxLayout;
    const int recorded = int(m_bridge.liveReviews().size());
    QPushButton *changes = button(m_changes ? QStringLiteral("Hide Changes") : QStringLiteral("Review Changes (%1)").arg(recorded),
                                  QStringLiteral("liveReviewChanges"), self);
    changes->setEnabled(recorded > 0 || m_changes);
    changes->setToolTip(QStringLiteral("The diff of every write-back, with Discard"));
    QPushButton *history = button(QStringLiteral("History"), QStringLiteral("liveHistory"), self);
    history->setEnabled(!project.isEmpty());
    history->setToolTip(QStringLiteral("Commits, what was deployed, and Restore"));
    secondary->addWidget(changes);
    secondary->addWidget(history);
    secondary->addStretch();
    column->addLayout(secondary);
    connect(changes, &QPushButton::clicked, this, [this] { showChanges(!m_changes); });
    connect(history, &QPushButton::clicked, this, [this] { m_bridge.showHistoryPanel(); });

    if (m_changes) {
        auto *list = new QWidget(self);
        list->setObjectName(QStringLiteral("liveChanges"));
        auto *rows = new QVBoxLayout(list);
        rows->setContentsMargins(0, 0, 0, 0);
        const auto &reviews = m_bridge.liveReviews();
        // Newest first.
        for (auto it = reviews.rbegin(); it != reviews.rend(); ++it) {
            const WriteBack::Review &review = *it;
            auto *title = new QLabel(QStringLiteral("<b>%1</b> · %2").arg(review.title.toHtmlEscaped(),
                                                                          review.commit.isEmpty() ? QStringLiteral("not committed yet")
                                                                                                  : QStringLiteral("in %1").arg(review.commit.left(7))),
                                     list);
            rows->addWidget(title);
            auto *summary = new QLabel(review.summary, list);
            summary->setWordWrap(true);
            rows->addWidget(summary);
            auto *diff = new QPlainTextEdit(review.diff(), list);
            diff->setObjectName(QStringLiteral("liveReviewDiff"));
            diff->setReadOnly(true);
            diff->setLineWrapMode(QPlainTextEdit::NoWrap);
            diff->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            diff->setFixedHeight(160);
            new DiffHighlighter(diff->document());
            rows->addWidget(diff);
            auto *row = new QHBoxLayout;
            row->addStretch();
            QPushButton *discard = button(QStringLiteral("Discard"), QStringLiteral("liveDiscard"), list);
            discard->setToolTip(review.commit.isEmpty() ? QStringLiteral("Put these files back as they were")
                                                        : QStringLiteral("Undo this change in a new commit; deploy again to take it off the site"));
            discard->setEnabled(!state.running);
            row->addWidget(discard);
            rows->addLayout(row);
            connect(discard, &QPushButton::clicked, this, [this, id = review.id] { report(m_bridge.discardReview(id)); });
        }
        if (reviews.empty())
            rows->addWidget(label(QStringLiteral("Nothing written back yet."), QStringLiteral("liveReviewEmpty"), list));
        auto *scroll = new QScrollArea(self);
        scroll->setWidget(list);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumHeight(std::min(420, 60 + 240 * int(reviews.size())));
        column->addWidget(scroll);
    }
    m_body->show();
}
