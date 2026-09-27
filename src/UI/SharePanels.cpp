#include "UI/SharePanels.h"
#include "Cloud/CloudStorage.h"
#include "Live/Deploy.h"
#include "UI/AgentSheets.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ShareController.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QPushButton *pushButton(const QString &name, const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAutoDefault(false);
    return button;
}

QLabel *label(const QString &name, QWidget *parent, bool wrap = true)
{
    auto *made = new QLabel(parent);
    made->setObjectName(name);
    made->setWordWrap(wrap);
    made->setTextFormat(Qt::PlainText);
    return made;
}

QLabel *faint(const QString &name, QWidget *parent)
{
    QLabel *made = label(name, parent);
    made->setForegroundRole(QPalette::PlaceholderText);
    return made;
}

// A popover: closes when the pointer goes elsewhere, and deletes itself.
void popover(QFrame *frame, const QString &name)
{
    frame->setObjectName(name);
    frame->setWindowFlags(Qt::Popup);
    frame->setAttribute(Qt::WA_DeleteOnClose);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setAutoFillBackground(true);
}

QString timeText(const QDateTime &time)
{
    return time.date() == QDate::currentDate() ? time.toString(QStringLiteral("HH:mm")) : time.toString(QStringLiteral("d MMM HH:mm"));
}

QString formatText(const Share::Record &record)
{
    if (record.format == QLatin1String("site"))
        return QStringLiteral("Site");
    return record.format.toUpper();
}
}

ShareToast::ShareToast(ShareController &share, QWidget &window)
    : QFrame(&window), m_share(share), m_window(window), m_text(label(QStringLiteral("shareToastText"), this)),
      m_detail(faint(QStringLiteral("shareToastDetail"), this)), m_open(pushButton(QStringLiteral("shareToastOpen"), QStringLiteral("Open"), this)),
      m_copy(pushButton(QStringLiteral("shareToastCopy"), QStringLiteral("Copy Again"), this)),
      m_details(pushButton(QStringLiteral("shareToastDetails"), QStringLiteral("Details"), this)),
      m_cancel(pushButton(QStringLiteral("shareToastCancel"), QStringLiteral("Cancel"), this)),
      m_connectCloud(pushButton(QStringLiteral("shareToastConnectCloud"), QStringLiteral("Connect Cloud Storage…"), this)),
      m_connectGitHub(pushButton(QStringLiteral("shareToastConnectGitHub"), QStringLiteral("Connect GitHub"), this))
{
    setObjectName(QStringLiteral("shareToast"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setAccessibleName(QStringLiteral("Share"));
    setFixedWidth(360);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(14, 10, 10, 10);
    column->setSpacing(4);
    auto *top = new QHBoxLayout;
    top->addWidget(m_text, 1);
    auto *dismiss = new QToolButton(this);
    dismiss->setObjectName(QStringLiteral("shareToastDismiss"));
    dismiss->setText(QStringLiteral("×"));
    dismiss->setAccessibleName(QStringLiteral("Dismiss"));
    dismiss->setToolTip(QStringLiteral("Dismiss"));
    dismiss->setAutoRaise(true);
    top->addWidget(dismiss, 0, Qt::AlignTop);
    column->addLayout(top);
    column->addWidget(m_detail);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    buttons->addStretch();
    for (QPushButton *button : {m_connectCloud, m_connectGitHub, m_details, m_cancel, m_open, m_copy})
        buttons->addWidget(button);
    column->addLayout(buttons);
    connect(dismiss, &QToolButton::clicked, &m_share, &ShareController::dismissNotice);
    connect(m_open, &QPushButton::clicked, this, [this] { m_share.openLink(m_share.notice().link); });
    connect(m_copy, &QPushButton::clicked, this, [this] { m_share.copyLink(m_share.notice().link); });
    connect(m_details, &QPushButton::clicked, this, [this] { AgentSheets::deployLog(&m_window, m_share.notice().log); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_share.job().cancel(); });
    connect(m_connectCloud, &QPushButton::clicked, this, [this] {
        m_share.dismissNotice();
        m_share.connectCloud();
    });
    connect(m_connectGitHub, &QPushButton::clicked, this, [this] {
        if (const QString failure = m_share.connectGitHub(); !failure.isEmpty())
            m_text->setText(failure);
    });
    connect(&m_share, &ShareController::changed, this, &ShareToast::synchronize);
    window.installEventFilter(this);
    synchronize();
}

bool ShareToast::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == &m_window && event->type() == QEvent::Resize)
        place();
    return QFrame::eventFilter(watched, event);
}

void ShareToast::place()
{
    adjustSize();
    int top = 8;
    if (auto *bar = m_window.findChild<QToolBar *>(QStringLiteral("toolbar")))
        top += bar->mapTo(&m_window, QPoint(0, bar->height())).y();
    move(m_window.width() - width() - 12, top);
    raise();
}

void ShareToast::synchronize()
{
    using Kind = ShareController::Notice::Kind;
    const ShareController::Notice &notice = m_share.notice();
    if (notice.kind == Kind::none) {
        hide();
        return;
    }
    m_text->setText(notice.text);
    m_detail->setText(notice.detail);
    m_detail->setVisible(!notice.detail.isEmpty());
    const bool linked = notice.kind == Kind::shared && !notice.link.isEmpty();
    m_open->setVisible(linked);
    m_copy->setVisible(linked);
    m_details->setVisible(notice.kind == Kind::failed && !notice.log.isEmpty() && QFileInfo::exists(notice.log));
    m_cancel->setVisible(notice.kind == Kind::progress && m_share.running());
    m_connectCloud->setVisible(notice.kind == Kind::connect);
    m_connectGitHub->setVisible(notice.kind == Kind::connect);
    show();
    place();
}

SharePopover::SharePopover(ShareController &share, QWidget *parent)
    : QFrame(parent), m_share(share), m_scope(label(QStringLiteral("shareScope"), this)), m_format(new QComboBox(this)),
      m_destination(new QComboBox(this)), m_note(faint(QStringLiteral("shareNote"), this)), m_nowhere(new QWidget(this)),
      m_latest(pushButton(QStringLiteral("shareLatest"), QStringLiteral("Copy Latest Deploy"), this)),
      m_go(pushButton(QStringLiteral("shareGo"), QStringLiteral("Share"), this)),
      m_shared(pushButton(QStringLiteral("shareShowShared"), QStringLiteral("Shared…"), this))
{
    popover(this, QStringLiteral("sharePopover"));
    setFixedWidth(340);
    m_format->setObjectName(QStringLiteral("shareFormat"));
    m_format->setAccessibleName(QStringLiteral("Format"));
    for (Share::Format format : {Share::Format::png, Share::Format::pdf, Share::Format::svg})
        m_format->addItem(Share::label(format), Share::suffix(format));
    m_destination->setObjectName(QStringLiteral("shareDestination"));
    m_destination->setAccessibleName(QStringLiteral("Share to"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);
    auto *title = label(QStringLiteral("shareTitle"), this);
    title->setText(QStringLiteral("Share"));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    column->addWidget(title);
    column->addWidget(m_scope);
    auto *form = new QGridLayout;
    form->setHorizontalSpacing(10);
    auto *formatLabel = new QLabel(QStringLiteral("Format"), this);
    auto *toLabel = new QLabel(QStringLiteral("To"), this);
    form->addWidget(formatLabel, 0, 0);
    form->addWidget(m_format, 0, 1);
    form->addWidget(toLabel, 1, 0);
    form->addWidget(m_destination, 1, 1);
    form->setColumnStretch(1, 1);
    column->addLayout(form);
    // Nowhere to share to: one line, and what to connect.
    auto *nowhereColumn = new QVBoxLayout(m_nowhere);
    nowhereColumn->setContentsMargins(0, 0, 0, 0);
    auto *nowhereText = label(QStringLiteral("shareNowhereText"), m_nowhere);
    nowhereText->setText(QStringLiteral("There's nowhere to share to yet. Connect a cloud service or GitHub."));
    nowhereColumn->addWidget(nowhereText);
    auto *connectRow = new QHBoxLayout;
    QPushButton *connectCloud = pushButton(QStringLiteral("shareConnectCloud"), QStringLiteral("Connect Cloud Storage…"), m_nowhere);
    QPushButton *connectGitHub = pushButton(QStringLiteral("shareConnectGitHub"), QStringLiteral("Connect GitHub"), m_nowhere);
    connectRow->addWidget(connectCloud);
    connectRow->addWidget(connectGitHub);
    connectRow->addStretch();
    nowhereColumn->addLayout(connectRow);
    m_nowhere->setObjectName(QStringLiteral("shareNowhere"));
    column->addWidget(m_nowhere);
    column->addWidget(m_note);
    column->addWidget(m_latest, 0, Qt::AlignLeft);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_shared);
    buttons->addStretch();
    buttons->addWidget(m_go);
    column->addLayout(buttons);
    m_go->setDefault(true);

    connect(m_format, &QComboBox::activated, this, [this] {
        m_share.remember({Share::parseFormat(m_format->currentData().toString()), QString()});
    });
    connect(m_destination, &QComboBox::activated, this, [this] { m_share.remember({std::nullopt, m_destination->currentData().toString()}); });
    connect(connectCloud, &QPushButton::clicked, this, [this] {
        close();
        m_share.connectCloud();
    });
    connect(connectGitHub, &QPushButton::clicked, this, [this, nowhereText] {
        if (const QString failure = m_share.connectGitHub(); !failure.isEmpty())
            nowhereText->setText(failure);
    });
    connect(m_latest, &QPushButton::clicked, this, [this] {
        const QString failure = m_share.shareLive(false);
        if (!failure.isEmpty()) {
            m_note->setText(failure);
            return;
        }
        close();
    });
    connect(m_go, &QPushButton::clicked, this, [this] {
        const QString destination = m_destination->currentData().toString();
        const QString failure = destination == Share::live
            ? m_share.shareLive(true)
            : m_share.share({Share::parseFormat(m_format->currentData().toString()), destination});
        if (!failure.isEmpty()) {
            m_note->setText(failure);
            return;
        }
        close();
    });
    connect(m_shared, &QPushButton::clicked, this, [this] {
        QWidget *window = parentWidget() ? parentWidget()->window() : nullptr;
        close();
        if (window)
            SharePanels::showShared(m_share, *window);
    });
    connect(&m_share, &ShareController::changed, this, &SharePopover::synchronize);
    synchronize();
}

void SharePopover::synchronize()
{
    const std::vector<ShareController::Destination> destinations = m_share.destinations();
    const QString chosen = m_share.chosenDestination();
    m_destination->clear();
    for (const ShareController::Destination &each : destinations) {
        m_destination->addItem(each.label, each.id);
        if (each.id == chosen)
            m_destination->setCurrentIndex(m_destination->count() - 1);
    }
    m_format->setCurrentIndex(m_format->findData(Share::suffix(m_share.chosenFormat())));
    const bool nowhere = destinations.empty();
    const bool site = chosen == Share::live;
    m_nowhere->setVisible(nowhere);
    m_destination->setEnabled(!nowhere);
    m_format->setEnabled(!site && !nowhere);
    if (site)
        m_scope->setText(QStringLiteral("Shares a preview of the Live project, %1.").arg(QFileInfo(m_share.liveProject()).fileName()));
    else if (m_share.hasDocument())
        m_scope->setText(QStringLiteral("Shares %1.").arg(m_share.scopeText()));
    else
        m_scope->setText(QStringLiteral("Open a document to share it."));
    const QString remote = Share::remoteOf(chosen);
    m_note->setText(nowhere ? QString() : SharePanels::privacyNote(chosen, remote.isEmpty() ? QString() : CloudStorage::rememberedType(remote)));
    m_note->setVisible(!m_note->text().isEmpty());
    const std::optional<Deploy::Record> latest = site ? Deploy::latest(m_share.liveProject()) : std::nullopt;
    m_latest->setVisible(latest.has_value());
    if (latest)
        m_latest->setToolTip(latest->url);
    m_go->setText(site ? QStringLiteral("Preview Deploy") : QStringLiteral("Share"));
    m_go->setEnabled(!nowhere && !m_share.running() && (site || m_share.hasDocument()));
    const size_t count = m_share.sharedList().size();
    m_shared->setText(count ? QStringLiteral("Shared (%1)…").arg(count) : QStringLiteral("Shared…"));
    adjustSize();
}

SharedPopover::SharedPopover(ShareController &share, QWidget *parent)
    : QFrame(parent), m_share(share), m_rows(new QVBoxLayout), m_feedback(new QWidget(this)), m_feedbackFor(new QComboBox(m_feedback)),
      m_feedbackText(new QPlainTextEdit(m_feedback)),
      m_feedbackButton(pushButton(QStringLiteral("sharedFeedback"), QStringLiteral("Paste Client Feedback…"), this)),
      m_message(faint(QStringLiteral("sharedMessage"), this))
{
    popover(this, QStringLiteral("sharedPopover"));
    setFixedWidth(420);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);
    auto *title = label(QStringLiteral("sharedTitle"), this);
    title->setText(QStringLiteral("Shared"));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    column->addWidget(title);
    m_rows->setSpacing(6);
    column->addLayout(m_rows);
    column->addWidget(m_feedbackButton, 0, Qt::AlignLeft);
    auto *feedbackColumn = new QVBoxLayout(m_feedback);
    feedbackColumn->setContentsMargins(0, 0, 0, 0);
    m_feedback->setObjectName(QStringLiteral("sharedFeedbackBox"));
    m_feedbackFor->setObjectName(QStringLiteral("sharedFeedbackFor"));
    m_feedbackFor->setAccessibleName(QStringLiteral("Feedback on"));
    m_feedbackText->setObjectName(QStringLiteral("sharedFeedbackText"));
    m_feedbackText->setAccessibleName(QStringLiteral("Client feedback"));
    m_feedbackText->setPlaceholderText(QStringLiteral("Paste the client's reply. The AI makes the changes it asks for, as a preview you keep or discard."));
    m_feedbackText->setTabChangesFocus(true);
    m_feedbackText->setFixedHeight(96);
    QPushButton *apply = pushButton(QStringLiteral("sharedFeedbackApply"), QStringLiteral("Apply Feedback"), m_feedback);
    feedbackColumn->addWidget(m_feedbackFor);
    feedbackColumn->addWidget(m_feedbackText);
    feedbackColumn->addWidget(apply, 0, Qt::AlignRight);
    m_feedback->hide();
    column->addWidget(m_feedback);
    column->addWidget(m_message);
    m_message->hide();
    connect(m_feedbackButton, &QPushButton::clicked, this, [this] {
        m_feedback->setVisible(!m_feedback->isVisible());
        if (m_feedback->isVisible())
            m_feedbackText->setFocus();
        adjustSize();
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        const QString failure = m_share.pasteFeedback(m_feedbackFor->currentData().toString(), m_feedbackText->toPlainText());
        if (!failure.isEmpty()) {
            m_message->setText(failure);
            m_message->show();
            adjustSize();
            return;
        }
        close();
    });
    connect(&m_share, &ShareController::changed, this, &SharedPopover::synchronize);
    synchronize();
}

void SharedPopover::synchronize()
{
    while (QLayoutItem *item = m_rows->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const std::vector<Share::Record> list = m_share.sharedList();
    const QString previous = m_feedbackFor->currentData().toString();
    m_feedbackFor->clear();
    if (list.empty()) {
        QLabel *empty = faint(QStringLiteral("sharedEmpty"), this);
        empty->setText(QStringLiteral("Nothing shared from this document yet."));
        m_rows->addWidget(empty);
    }
    for (const Share::Record &record : list) {
        auto *row = new QWidget(this);
        row->setObjectName(QStringLiteral("sharedRow"));
        row->setProperty("recordId", record.id);
        auto *rowColumn = new QVBoxLayout(row);
        rowColumn->setContentsMargins(0, 0, 0, 0);
        rowColumn->setSpacing(2);
        const QString scope = record.scope == QLatin1String("selection") ? QStringLiteral(" · selection") : QString();
        QLabel *what = label(QStringLiteral("sharedWhat"), row);
        what->setText(QStringLiteral("%1 · %2%3 · %4").arg(record.where, formatText(record), scope, timeText(record.time)));
        QLabel *link = faint(QStringLiteral("sharedLink"), row);
        link->setWordWrap(false);
        link->setText(link->fontMetrics().elidedText(record.link, Qt::ElideMiddle, 380));
        link->setToolTip(record.link);
        rowColumn->addWidget(what);
        rowColumn->addWidget(link);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        QPushButton *copy = pushButton(QStringLiteral("sharedCopy"), QStringLiteral("Copy"), row);
        QPushButton *open = pushButton(QStringLiteral("sharedOpen"), QStringLiteral("Open"), row);
        QPushButton *unshare = pushButton(QStringLiteral("sharedUnshare"), record.canUnshare() ? QStringLiteral("Unshare") : QStringLiteral("Remove"), row);
        unshare->setToolTip(record.canUnshare() ? QStringLiteral("Delete the shared copy, so the link stops working")
                                                : QStringLiteral("Take it off this list; the deploy stays up"));
        unshare->setEnabled(!m_share.running());
        for (QPushButton *button : {copy, open, unshare}) {
            button->setProperty("recordId", record.id);
            buttons->addWidget(button);
        }
        buttons->addStretch();
        rowColumn->addLayout(buttons);
        const QString link_ = record.link, id = record.id;
        connect(copy, &QPushButton::clicked, this, [this, link_] { m_share.copyLink(link_); });
        connect(open, &QPushButton::clicked, this, [this, link_] { m_share.openLink(link_); });
        connect(unshare, &QPushButton::clicked, this, [this, id] {
            const QString failure = m_share.unshare(id);
            m_message->setText(failure.isEmpty() ? QStringLiteral("Unsharing…") : failure);
            m_message->show();
        });
        m_rows->addWidget(row);
        if (record.kind != Share::live) {
            QString scopeWord = record.scope == QLatin1String("selection") ? QStringLiteral("Selection") : QStringLiteral("Artboard");
            m_feedbackFor->addItem(QStringLiteral("%1, %2 %3").arg(scopeWord, formatText(record), timeText(record.time)), record.id);
        }
    }
    if (const int at = m_feedbackFor->findData(previous); at >= 0)
        m_feedbackFor->setCurrentIndex(at);
    m_feedbackButton->setVisible(m_feedbackFor->count() > 0);
    if (m_feedbackFor->count() == 0)
        m_feedback->hide();
    if (!m_share.running() && m_message->text() == QLatin1String("Unsharing…"))
        m_message->hide();
    adjustSize();
}
