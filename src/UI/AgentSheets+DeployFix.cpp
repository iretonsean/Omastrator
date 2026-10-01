#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

// Deploy Details on a failed deploy (docs/LIVE-IN-FRAME.md, Deploy): hand it to the default agent, watch it, then deploy again
// yourself. The agent never deploys.
namespace AgentSheets {
void addDeployFix(QDialog *dialog, QBoxLayout *column, AgentBridge &bridge, const QString &folder)
{
    const QString project = QFileInfo(folder).canonicalFilePath().isEmpty() ? folder : QFileInfo(folder).canonicalFilePath();
    // Asked once: it starts Omarchy's launcher.
    QString hint;
    const QString agent = AgentLauncher::defaultAgent(&hint);
    const QString name = AgentBridge::displayName(agent);

    auto *status = new QLabel(dialog);
    status->setObjectName(QStringLiteral("fixStatus"));
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *row = new QHBoxLayout;
    auto *fix = new QPushButton(QStringLiteral("Fix with %1").arg(name), dialog);
    fix->setObjectName(QStringLiteral("fixWithAgent"));
    fix->setToolTip(QStringLiteral("%1 reads this log, fixes what it can in the project and checks the build. It doesn't deploy.").arg(name));
    auto *stop = new QPushButton(QStringLiteral("Stop"), dialog);
    stop->setObjectName(QStringLiteral("fixStop"));
    auto *again = new QPushButton(QStringLiteral("Deploy again"), dialog);
    again->setObjectName(QStringLiteral("fixDeployAgain"));
    again->setToolTip(QStringLiteral("Commit what was fixed, push, and deploy to production"));
    row->addWidget(fix);
    row->addWidget(stop);
    row->addWidget(again);
    row->addStretch();
    column->addLayout(row);
    column->addWidget(status);

    // No default agent: no button, only why.
    if (agent.isEmpty()) {
        fix->hide();
        stop->hide();
        again->hide();
        status->setText(hint);
        return;
    }

    const QPointer<QDialog> guard(dialog);
    auto refresh = [&bridge, project, fix, stop, again, status, name] {
        const AgentBridge::DeployFixState &state = bridge.deployFix();
        const bool mine = !state.folder.isEmpty() && state.folder == project;
        const bool running = mine && state.running;
        const bool finished = mine && state.finished;
        fix->setVisible(!running);
        fix->setEnabled(!bridge.deployState().running);
        fix->setText(finished ? QStringLiteral("Fix with %1 again").arg(name) : QStringLiteral("Fix with %1").arg(name));
        stop->setVisible(running);
        again->setVisible(finished);
        again->setEnabled(!bridge.deployState().running);
        if (running) {
            status->setText(QStringLiteral("%1 is looking into it. You can close this window; it keeps working.").arg(state.agent));
        } else if (finished) {
            QString text = state.error.isEmpty() ? state.summary : state.error;
            if (state.error.isEmpty() && text.isEmpty())
                text = QStringLiteral("%1 finished without a summary.").arg(state.agent);
            text += state.files.isEmpty() ? QStringLiteral("\nNo files changed.") : QStringLiteral("\nChanged: %1").arg(state.files.join(QStringLiteral(", ")));
            status->setText(text);
        } else {
            status->clear();
        }
        status->setVisible(!status->text().isEmpty());
    };
    QObject::connect(&bridge, &AgentBridge::deployFixChanged, dialog, refresh);
    QObject::connect(&bridge, &AgentBridge::liveReviewChanged, dialog, refresh);
    QObject::connect(fix, &QPushButton::clicked, dialog, [&bridge, project, status, refresh] {
        const QString failure = bridge.fixDeploy(project);
        if (!failure.isEmpty()) {
            status->setText(failure);
            status->show();
            return;
        }
        refresh();
    });
    QObject::connect(stop, &QPushButton::clicked, dialog, [&bridge] { bridge.stopFix(); });
    QObject::connect(again, &QPushButton::clicked, dialog, [&bridge, project, status, dialog] {
        AgentBridge::DeployRequest request;
        request.folder = project;
        request.fromFrame = project != bridge.deployProject();
        bool needsAnswer = false;
        const QString failure = bridge.liveDeploy(request, &needsAnswer);
        if (!failure.isEmpty()) {
            status->setText(failure);
            status->show();
            return;
        }
        QWidget *window = dialog->parentWidget();
        const bool fromFrame = request.fromFrame;
        dialog->accept();
        if (needsAnswer)
            AgentSheets::deploy(bridge, window, project, true, fromFrame);
    });
    refresh();
}
}
