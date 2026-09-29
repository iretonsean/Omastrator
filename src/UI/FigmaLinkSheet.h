#pragma once
#include "Document/VectorDocument.h"
#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QDialogButtonBox;

// File ▸ Import from Figma Link…: a link, a personal access token the first
// time (docs/import/figma.md), then GET /v1/files/:key (or /nodes?ids=) over
// the REST API. `onImported` runs once, with the mapped document.
class FigmaLinkSheet : public QDialog {
    Q_OBJECT
public:
    explicit FigmaLinkSheet(QWidget *parent = nullptr);

    std::function<void(VectorDocument, QString title, QStringList warnings)> onImported;

private:
    QLabel *m_tokenLabel = nullptr;
    QLineEdit *m_tokenField = nullptr;
    QLabel *m_tokenLink = nullptr;
    QLineEdit *m_urlField = nullptr;
    QLabel *m_status = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QPushButton *m_importButton = nullptr;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_pendingToken;

    void updateTokenRow();
    void startImport();
    void fetch(const QString &url, bool wantsNodeGeometry);
    void finish(QNetworkReply *reply, const QString &nodeID);
    void setBusy(bool busy);
    void fail(const QString &message);
};
