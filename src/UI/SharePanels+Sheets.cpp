#include "UI/SharePanels.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/Share.h"
#include "UI/ShareController.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace SharePanels {
// Under the toolbar's Share button, right edges together; else the window's top right.
void placeUnderShare(QWidget *popup, QWidget &window)
{
    popup->adjustSize();
    QPoint at;
    if (auto *button = window.findChild<QToolButton *>(QStringLiteral("shareOptionsToolbar")); button && button->isVisible())
        at = button->mapToGlobal(QPoint(button->width(), button->height() + 4));
    else
        at = window.mapToGlobal(QPoint(window.width() - 12, 48));
    popup->move(at - QPoint(popup->width(), 0));
}

void shareNow(ShareController &share)
{
    const QString failure = share.share();
    if (!failure.isEmpty())
        share.setFailure(failure);
}

SharePopover *showOptions(ShareController &share, QWidget &window)
{
    auto *popup = new SharePopover(share, &window);
    placeUnderShare(popup, window);
    popup->show();
    return popup;
}

DevicePopover *showDevices(ShareController &share, QWidget &window)
{
    auto *popup = new DevicePopover(share, &window);
    placeUnderShare(popup, window);
    popup->show();
    share.device().refresh();
    return popup;
}

SharedPopover *showShared(ShareController &share, QWidget &window)
{
    auto *popup = new SharedPopover(share, &window);
    placeUnderShare(popup, window);
    popup->show();
    return popup;
}

QDialog *confirmGitHub(ShareController &share, QWidget &window)
{
    auto *dialog = new QDialog(&window);
    dialog->setObjectName(QStringLiteral("shareGitHubSheet"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QStringLiteral("Share through GitHub?"));
    dialog->setMinimumWidth(460);
    auto *column = new QVBoxLayout(dialog);
    column->setContentsMargins(24, 20, 24, 20);
    column->setSpacing(10);
    auto *question = new QLabel(QStringLiteral("Share through GitHub?"), dialog);
    question->setObjectName(QStringLiteral("shareGitHubQuestion"));
    QFont bold = question->font();
    bold.setBold(true);
    question->setFont(bold);
    auto *explain = new QLabel(dialog);
    explain->setObjectName(QStringLiteral("shareGitHubExplain"));
    explain->setWordWrap(true);
    explain->setTextFormat(Qt::PlainText);
    explain->setText(privacyNote(Share::github, QString()) + QStringLiteral("\n\nUnshare deletes the gist or the release. This is asked only once."));
    column->addWidget(question);
    column->addWidget(explain);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(QStringLiteral("Share via GitHub"));
    ok->setObjectName(QStringLiteral("dialogOK"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("dialogCancel"));
    NativeShortcut::bind(*dialog, ok, buttons->button(QDialogButtonBox::Cancel));
    column->addWidget(buttons);
    QPointer<ShareController> controller = &share;
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, controller] {
        dialog->accept();
        if (controller)
            controller->answerGitHub(true);
    });
    QObject::connect(dialog, &QDialog::rejected, dialog, [controller] {
        if (controller)
            controller->answerGitHub(false);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->open();
    return dialog;
}

QString privacyNote(const QString &destination, const QString &remoteType)
{
    if (destination == Share::github)
        return QStringLiteral("SVG goes to a secret gist; PNG and PDF go to a release in a public “omastrator-shares” repository on your GitHub "
                              "account. Neither is private: a secret gist is unlisted but opens for anyone with the link, and anyone can browse a "
                              "public repository's releases.");
    if (destination == Share::live)
        return QStringLiteral("A preview deploy, not production. Anyone with the link can open it.");
    if (remoteType == QLatin1String("s3") || remoteType == QLatin1String("b2"))
        return QStringLiteral("Anyone with the link can open it. S3 and B2 links are presigned and stop working after about a week.");
    if (!destination.isEmpty())
        return QStringLiteral("Anyone with the link can open it, without signing in.");
    return {};
}
}
