#pragma once
#include "UI/SettingsBundle.h"
#include <QDialog>
#include <functional>

class QPushButton;
class QTreeWidget;

// Import Settings' one confirm: every setting it would replace, what it is now and what it
// becomes, and where the backup goes. Nothing is replaced until Replace Settings.
class SettingsConfirmDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsConfirmDialog(const SettingsBundle::Plan &plan, QWidget *parent = nullptr);

    // Shows the plan and, only if confirmed, applies it. Returns why it failed; `confirmed` says whether it ran.
    static QString run(const SettingsBundle::Plan &plan, QWidget *parent, bool *confirmed = nullptr, QString *backupPath = nullptr);
    // Tests answer the dialog instead of a person: return true to confirm.
    static void setResponder(std::function<bool(SettingsConfirmDialog &)> responder);

    // Everything the dialog says, as plain text.
    QString text() const { return m_lines.join(QLatin1Char('\n')); }
    QTreeWidget *changes() const { return m_changes; }

private:
    QTreeWidget *m_changes = nullptr;
    QStringList m_lines;
};
