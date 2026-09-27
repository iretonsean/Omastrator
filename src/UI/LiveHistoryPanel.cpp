#include "UI/LiveHistoryPanel.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

LiveHistoryPanel::LiveHistoryPanel(AgentBridge &bridge, QWidget *parent) : QWidget(parent), m_bridge(bridge), m_outer(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("liveHistory"));
    setFixedWidth(560);
    m_outer->setContentsMargins(18, 16, 18, 16);
    // A save or deploy adds a commit or marks one deployed.
    connect(&m_bridge, &AgentBridge::liveReviewChanged, this, [this] {
        if (!m_bridge.deployState().running)
            rebuild();
    });
    rebuild();
}

void LiveHistoryPanel::rebuild()
{
    if (m_body)
        m_body->deleteLater();
    m_body = new QWidget(this);
    m_outer->addWidget(m_body);
    auto *column = new QVBoxLayout(m_body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    const QString project = m_bridge.deployProject();
    auto *where = new QLabel(project.isEmpty() ? QStringLiteral("Open a project in Live to see its history.") : project, m_body);
    where->setWordWrap(true);
    column->addWidget(where);
    auto *message = new QLabel(m_message, m_body);
    message->setObjectName(QStringLiteral("historyMessage"));
    message->setWordWrap(true);
    message->setVisible(!m_message.isEmpty());
    column->addWidget(message);
    // After a restore, Deploy is the next step.
    if (!m_message.isEmpty() && m_message.startsWith(QLatin1String("Restored"))) {
        auto *deploy = new QPushButton(QStringLiteral("Deploy"), m_body);
        deploy->setObjectName(QStringLiteral("historyDeploy"));
        column->addWidget(deploy, 0, Qt::AlignLeft);
        connect(deploy, &QPushButton::clicked, this, [this] {
            bool needsAnswer = false;
            m_message = m_bridge.liveDeploy({}, &needsAnswer);
            if (needsAnswer)
                AgentSheets::deploy(m_bridge, window());
            rebuild();
        });
    }

    auto *list = new QWidget(m_body);
    auto *rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(10);
    const std::vector<History::Entry> entries = m_bridge.history();
    const QLocale locale;
    for (const History::Entry &entry : entries) {
        auto *row = new QWidget(list);
        row->setObjectName(QStringLiteral("historyEntry"));
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto *subject = new QLabel(QStringLiteral("<b>%1</b>").arg(entry.subject.toHtmlEscaped()), row);
        subject->setWordWrap(true);
        layout->addWidget(subject);
        QString meta = QStringLiteral("%1 · %2 · %3 · %4").arg(entry.sha.left(7), entry.author, locale.toString(entry.time, QLocale::ShortFormat),
                                                               entry.files.size() == 1 ? QStringLiteral("1 file") : QStringLiteral("%1 files").arg(entry.files.size()));
        if (entry.deploy)
            meta += entry.deploy->url.isEmpty() ? QStringLiteral(" · Deployed") : QStringLiteral(" · Deployed to %1").arg(entry.deploy->url);
        auto *details = new QLabel(meta, row);
        details->setObjectName(QStringLiteral("historyMeta"));
        details->setWordWrap(true);
        details->setToolTip(entry.files.join(QLatin1Char('\n')));
        layout->addWidget(details);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        auto *open = new QPushButton(QStringLiteral("Open on GitHub"), row);
        open->setObjectName(QStringLiteral("historyOpen"));
        open->setEnabled(!entry.link.isEmpty());
        auto *restore = new QPushButton(QStringLiteral("Restore"), row);
        restore->setObjectName(QStringLiteral("historyRestore"));
        restore->setToolTip(QStringLiteral("Bring back this version's files as a new commit"));
        restore->setEnabled(&entry != &entries.front() && !m_bridge.deployState().running);
        buttons->addWidget(open);
        buttons->addWidget(restore);
        layout->addLayout(buttons);
        rows->addWidget(row);
        connect(open, &QPushButton::clicked, this, [link = entry.link] { QDesktopServices::openUrl(QUrl(link)); });
        connect(restore, &QPushButton::clicked, this, [this, sha = entry.sha] {
            const QString failure = m_bridge.restoreVersion(sha);
            m_message = failure.isEmpty() ? QStringLiteral("Restored %1 as a new commit. Deploy to put it live.").arg(sha.left(7)) : failure;
            rebuild();
        });
    }
    if (entries.empty() && !project.isEmpty())
        rows->addWidget(new QLabel(QStringLiteral("No commits yet."), list));
    rows->addStretch();
    auto *scroll = new QScrollArea(m_body);
    scroll->setWidget(list);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(360);
    column->addWidget(scroll);
    m_body->show();
}
