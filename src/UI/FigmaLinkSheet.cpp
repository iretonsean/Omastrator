#include "UI/FigmaLinkSheet.h"
#include "IO/FigmaImporter.h"
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

FigmaLinkSheet::FigmaLinkSheet(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Import from Figma Link"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_urlField = new QLineEdit(this);
    m_urlField->setPlaceholderText(QStringLiteral("https://www.figma.com/design/…"));
    form->addRow(QStringLiteral("Link:"), m_urlField);

    m_tokenLabel = new QLabel(QStringLiteral("Personal access token:"), this);
    m_tokenField = new QLineEdit(this);
    m_tokenField->setEchoMode(QLineEdit::Password);
    form->addRow(m_tokenLabel, m_tokenField);
    m_tokenLink = new QLabel(QStringLiteral("<a href=\"%1\">Get a token from Figma…</a>").arg(FigmaImporter::Token::settingsPageURL()), this);
    m_tokenLink->setOpenExternalLinks(true);
    form->addRow(QString(), m_tokenLink);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color: #c0392b;"));
    layout->addWidget(m_status);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_importButton = m_buttons->addButton(QStringLiteral("Import"), QDialogButtonBox::AcceptRole);
    m_importButton->setDefault(true);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this] {
        if (m_reply)
            m_reply->abort();
        reject();
    });
    connect(m_importButton, &QPushButton::clicked, this, &FigmaLinkSheet::startImport);

    updateTokenRow();
    resize(420, sizeHint().height());
}

void FigmaLinkSheet::updateTokenRow()
{
    const bool hasToken = FigmaImporter::Token::load().has_value();
    m_tokenLabel->setVisible(!hasToken);
    m_tokenField->setVisible(!hasToken);
    m_tokenLink->setVisible(!hasToken);
}

void FigmaLinkSheet::setBusy(bool busy)
{
    m_urlField->setEnabled(!busy);
    m_tokenField->setEnabled(!busy);
    m_importButton->setEnabled(!busy);
    m_status->setStyleSheet(busy ? QString() : QStringLiteral("color: #c0392b;"));
    m_status->setText(busy ? QStringLiteral("Importing…") : QString());
}

void FigmaLinkSheet::fail(const QString &message)
{
    setBusy(false);
    m_status->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_status->setText(message);
}

void FigmaLinkSheet::startImport()
{
    const std::optional<FigmaImporter::LinkTarget> target = FigmaImporter::parseLink(m_urlField->text().trimmed());
    if (!target) {
        fail(QStringLiteral("That doesn’t look like a Figma file link."));
        return;
    }
    if (!FigmaImporter::Token::load()) {
        const QString typed = m_tokenField->text().trimmed();
        if (typed.isEmpty()) {
            fail(QStringLiteral("Add a personal access token to import from Figma."));
            return;
        }
        FigmaImporter::Token::save(typed);
    }
    setBusy(true);
    QUrl url;
    if (target->nodeID.isEmpty()) {
        url = QUrl(QStringLiteral("https://api.figma.com/v1/files/%1").arg(target->fileKey));
        url.setQuery(QUrlQuery{{QStringLiteral("geometry"), QStringLiteral("paths")}});
    } else {
        url = QUrl(QStringLiteral("https://api.figma.com/v1/files/%1/nodes").arg(target->fileKey));
        url.setQuery(QUrlQuery{{QStringLiteral("ids"), target->nodeID}, {QStringLiteral("geometry"), QStringLiteral("paths")}});
    }
    QNetworkRequest request(url);
    request.setRawHeader("X-Figma-Token", FigmaImporter::Token::load()->toUtf8());
    m_reply = m_network.get(request);
    const QString nodeID = target->nodeID;
    connect(m_reply, &QNetworkReply::finished, this, [this, nodeID] {
        if (QNetworkReply *reply = m_reply)
            finish(reply, nodeID);
    });
}

void FigmaLinkSheet::finish(QNetworkReply *reply, const QString &nodeID)
{
    reply->deleteLater();
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status != 200)) {
        if (status == 403 || status == 401)
            fail(QStringLiteral("The Figma token was rejected. Preferences lets you forget it and try another."));
        else if (status == 404)
            fail(QStringLiteral("That file couldn’t be found. Check the link and that you have access to it."));
        else if (status == 429)
            fail(QStringLiteral("Figma is rate-limiting this token; try again in a moment."));
        else
            fail(QStringLiteral("Couldn’t reach Figma: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray json = reply->readAll();
    QStringList warnings;
    try {
        VectorDocument document = FigmaImporter::parseRestFile(json, nodeID, &warnings);
        if (onImported)
            onImported(std::move(document), QStringLiteral("Figma Import"), warnings);
        accept();
    } catch (const FileError &error) {
        fail(error.message());
    }
}
