#pragma once
#include <QFrame>
#include <QPointer>

class QComboBox;
class QDialog;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class ShareController;

// The toast Share answers with, at the window's top right: "Link copied — Google Drive" with Open and Copy Again,
// a failure, or what to connect. It goes by itself after a success.
class ShareToast : public QFrame {
    Q_OBJECT
public:
    ShareToast(ShareController &share, QWidget &window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    void place();

    ShareController &m_share;
    QWidget &m_window;
    QLabel *const m_text;
    QLabel *const m_detail;
    QPushButton *const m_open;
    QPushButton *const m_copy;
    QPushButton *const m_details;
    QPushButton *const m_cancel;
    QPushButton *const m_connectCloud;
    QPushButton *const m_connectGitHub;
};

// The popover beside Share: what it shares, the format and destination (remembered for the document), and an
// honest line on who can open the link.
class SharePopover : public QFrame {
    Q_OBJECT
public:
    SharePopover(ShareController &share, QWidget *parent);

private:
    void synchronize();

    ShareController &m_share;
    QLabel *const m_scope;
    QComboBox *const m_format;
    QComboBox *const m_destination;
    QLabel *const m_note;
    QWidget *const m_nowhere;
    QPushButton *const m_latest;
    QPushButton *const m_go;
    QPushButton *const m_shared;
};

// The Shared popover: each link with Copy, Open and Unshare, and Paste client feedback.
class SharedPopover : public QFrame {
    Q_OBJECT
public:
    SharedPopover(ShareController &share, QWidget *parent);

private:
    void synchronize();

    ShareController &m_share;
    QVBoxLayout *const m_rows;
    QWidget *const m_feedback;
    QComboBox *const m_feedbackFor;
    QPlainTextEdit *const m_feedbackText;
    QPushButton *const m_feedbackButton;
    QLabel *const m_message;
};

namespace SharePanels {
// Share, from the menu, the toolbar or a key: failures go to the toast.
void shareNow(ShareController &share);
// Opens the popovers under the toolbar's Share button (or the window's top right).
SharePopover *showOptions(ShareController &share, QWidget &window);
SharedPopover *showShared(ShareController &share, QWidget &window);
// Under the toolbar's Share button, right edges together; else the window's top right.
void placeUnderShare(QWidget *popup, QWidget &window);
// Asked once, before the first upload to GitHub.
QDialog *confirmGitHub(ShareController &share, QWidget &window);
// The popover's line on who can open a link that goes to `destination`.
QString privacyNote(const QString &destination, const QString &remoteType);
}
