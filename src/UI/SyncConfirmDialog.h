#pragma once
#include "System/SyncPlan.h"
#include <QDialog>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

// The author's confirmation rule (docs/DESIGN-SYSTEMS.md): before any push or pull
// of a design system, this shows where it publishes, every file it will write
// with its full path, the repository and branch it may commit to, any command it
// runs, and a dry-run preview of the changes. Nothing happens until Confirm.
class SyncConfirmDialog : public QDialog {
    Q_OBJECT
public:
    explicit SyncConfirmDialog(const SyncPlan &plan, QWidget *parent = nullptr);

    // Shows the plan and, only if confirmed, runs it. Returns why it failed; `confirmed` says whether it ran.
    static QString run(const SyncPlan &plan, QWidget *parent, bool *confirmed = nullptr);
    // Tests answer the dialog instead of a person: return true to confirm. The dialog is fully built when it's called.
    static void setResponder(std::function<bool(SyncConfirmDialog &)> responder);

    // Everything the dialog says, as plain text, for tests and the clipboard.
    QString text() const;
    QPushButton *confirmButton() const { return m_confirm; }
    QTreeWidget *files() const { return m_files; }

private:
    const SyncPlan &m_plan;
    QTreeWidget *m_files = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_confirm = nullptr;
    QStringList m_lines;
};
