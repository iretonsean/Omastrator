#include "UI/SettingsTransfer.h"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
std::function<bool(SettingsConfirmDialog &)> &responder()
{
    static std::function<bool(SettingsConfirmDialog &)> answer;
    return answer;
}

QLabel *paragraph(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

SettingsConfirmDialog::SettingsConfirmDialog(const SettingsBundle::Plan &plan, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Replace Settings"));
    setObjectName(QStringLiteral("settingsConfirmDialog"));
    setMinimumWidth(620);
    auto *layout = new QVBoxLayout(this);
    const QString intro = QStringLiteral("Importing replaces these settings on this computer. Anything not listed stays as it is.");
    layout->addWidget(paragraph(intro, this));
    m_lines << intro;

    m_changes = new QTreeWidget(this);
    m_changes->setObjectName(QStringLiteral("settingsChanges"));
    m_changes->setHeaderLabels({QStringLiteral("Setting"), QStringLiteral("Now"), QStringLiteral("After import")});
    m_changes->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_changes->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_changes->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    QTreeWidgetItem *group = nullptr;
    for (const SettingsBundle::Change &change : plan.changes) {
        if (!group || group->text(0) != change.category) {
            group = new QTreeWidgetItem(m_changes, {change.category});
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            m_lines << change.category;
        }
        const QString now = change.from.isEmpty() ? QStringLiteral("Not set") : change.from;
        const QString after = change.to.isEmpty() ? QStringLiteral("Default") : change.to;
        auto *row = new QTreeWidgetItem(group, {change.label, now, after});
        row->setToolTip(0, change.label);
        m_lines << QStringLiteral("  %1: %2 → %3").arg(change.label, now, after);
    }
    m_changes->expandAll();
    m_changes->setMinimumHeight(180);
    layout->addWidget(m_changes, 1);

    for (const QString &note : plan.notes) {
        layout->addWidget(paragraph(note, this));
        m_lines << note;
    }
    const QString backup = QStringLiteral("What's here now is saved to %1 first. Import that file to go back.").arg(SettingsBundle::backupFolder());
    layout->addWidget(paragraph(backup, this));
    m_lines << backup;

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    auto *confirm = buttons->addButton(QStringLiteral("Replace Settings"), QDialogButtonBox::AcceptRole);
    confirm->setObjectName(QStringLiteral("settingsConfirm"));
    // Enter cancels; replacing is a deliberate click.
    confirm->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsConfirmDialog::setResponder(std::function<bool(SettingsConfirmDialog &)> answer)
{
    responder() = std::move(answer);
}

QString SettingsConfirmDialog::run(const SettingsBundle::Plan &plan, QWidget *parent, bool *confirmed, QString *backupPath)
{
    SettingsConfirmDialog dialog(plan, parent);
    const bool yes = responder() ? responder()(dialog) : dialog.exec() == QDialog::Accepted;
    if (confirmed)
        *confirmed = yes;
    return yes ? SettingsBundle::apply(plan, backupPath) : QString();
}
