#pragma once
#include "Cloud/CloudProviders.h"
#include "Cloud/CloudStorage.h"
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>

// File ▸ Connect Cloud Storage…: the connected services, Connect… and Disconnect. rclone does the
// signing in and keeps it; what's typed here goes to rclone and nowhere else.
class CloudStorageSheet : public QDialog {
    Q_OBJECT
public:
    CloudStorageSheet(CloudStorage &storage, QWidget *parent = nullptr);
    ~CloudStorageSheet() override;

    // Opens a terminal running `omarchy pkg add rclone`; the reason when it can't.
    static QString installRclone();
    // `wanted`, or `wanted-2` and on, whichever isn't taken.
    static QString freeName(const QString &wanted, const QList<CloudRemote> &taken);

    enum Page { remotesPage, providersPage, formPage, questionPage, waitingPage, missingPage };
    Page page() const { return Page(m_pages->currentIndex()); }

public slots:
    void reject() override;

private:
    QWidget *makeRemotesPage();
    QWidget *makeProvidersPage();
    QWidget *makeFormPage();
    QWidget *makeQuestionPage();
    QWidget *makeWaitingPage();
    QWidget *makeMissingPage();
    void showRemotes();
    void fillRemotes();
    void showForm(const CloudProvider &provider);
    void connectRemote();
    void handle(const CloudConfigStep &step);
    void ask(const CloudQuestion &question);
    void answerQuestion();
    void wait(const QString &text);
    void cancelConnect();
    void abandon();
    void disconnectSelected();
    void checkInstalled();

    CloudStorage &m_storage;
    QStackedWidget *const m_pages;
    QListWidget *m_remoteList = nullptr;
    QLabel *m_emptyNote = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_disconnect = nullptr;
    QListWidget *m_providerList = nullptr;
    QLabel *m_formTitle = nullptr;
    QLabel *m_formNote = nullptr;
    QLabel *m_formError = nullptr;
    QLineEdit *m_name = nullptr;
    QWidget *m_fieldsHolder = nullptr;
    QFormLayout *m_fieldsForm = nullptr;
    QWidget *m_fields = nullptr;
    std::vector<std::pair<CloudField, QWidget *>> m_inputs;
    QLabel *m_questionTitle = nullptr;
    QLabel *m_questionHelp = nullptr;
    QLabel *m_questionError = nullptr;
    QComboBox *m_choice = nullptr;
    QLineEdit *m_answer = nullptr;
    QLabel *m_waitingText = nullptr;
    QLabel *m_installError = nullptr;
    QTimer m_installPoll;
    CloudProvider m_provider;
    // The remote being connected, until rclone finishes or it's abandoned.
    QString m_connecting;
    CloudQuestion m_question;
    QPointer<CloudJob> m_job;
    bool m_cancelling = false;
};
