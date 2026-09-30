#include "UI/SyncConfirmDialog.h"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
std::function<bool(SyncConfirmDialog &)> &responder()
{
    static std::function<bool(SyncConfirmDialog &)> answer;
    return answer;
}

QLabel *heading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QLabel *body(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

SyncConfirmDialog::SyncConfirmDialog(const SyncPlan &plan, QWidget *parent) : QDialog(parent), m_plan(plan)
{
    setWindowTitle(plan.title);
    setObjectName(QStringLiteral("syncConfirmDialog"));
    setMinimumWidth(620);
    auto *layout = new QVBoxLayout(this);
    const auto section = [&](const QString &title, const QString &text) {
        layout->addWidget(heading(title, this));
        layout->addWidget(body(text, this));
        m_lines.append(title + QStringLiteral(": ") + text);
    };
    section(!plan.destinationLabel.isEmpty()                       ? plan.destinationLabel
                : plan.direction == SyncPlan::Direction::push ? QStringLiteral("Publishes to")
                                                              : QStringLiteral("Brings into"),
            plan.destination);
    if (!plan.reads.isEmpty())
        section(QStringLiteral("Reads"), plan.reads.join(QLatin1Char('\n')));

    layout->addWidget(heading(QStringLiteral("Writes these files"), this));
    m_lines.append(QStringLiteral("Writes these files:"));
    if (plan.writes.empty()) {
        layout->addWidget(body(QStringLiteral("No files."), this));
        m_lines.append(QStringLiteral("No files."));
    } else {
        m_files = new QTreeWidget(this);
        m_files->setObjectName(QStringLiteral("syncFiles"));
        m_files->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Change")});
        m_files->setRootIsDecorated(false);
        m_files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_files->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        for (const FileWrite &write : plan.writes) {
            auto *row = new QTreeWidgetItem(m_files, {write.path, write.summary()});
            row->setToolTip(0, write.path);
            m_lines.append(write.path + QStringLiteral(" (") + write.summary() + QLatin1Char(')'));
        }
        m_files->setMaximumHeight(160);
        layout->addWidget(m_files);
    }
    if (plan.git && plan.git->create) {
        section(QStringLiteral("Commits to"),
                QStringLiteral("A new git repository, branch %1: “%2”. Nothing is pushed.").arg(plan.git->branch, plan.git->message));
    } else if (plan.git) {
        section(QStringLiteral("Commits to"),
                plan.git->commit ? QStringLiteral("The git repository %1, branch %2, as “%3”. Nothing is pushed to a remote.")
                                       .arg(plan.git->repository, plan.git->branch, plan.git->message)
                                 : QStringLiteral("Nothing: the files in %1 (branch %2) are left uncommitted.").arg(plan.git->repository, plan.git->branch));
    } else if (plan.direction == SyncPlan::Direction::push) {
        section(QStringLiteral("Commits to"), QStringLiteral("No repository."));
    }
    if (const std::vector<QStringList> commands = plan.allCommands(); !commands.empty() || !plan.runsAfter.isEmpty()) {
        QStringList lines;
        for (const QStringList &command : commands) {
            QStringList quoted;
            for (const QString &word : command)
                quoted.append(word.contains(QLatin1Char(' ')) || word.isEmpty() ? QLatin1Char('"') + word + QLatin1Char('"') : word);
            lines.append(quoted.join(QLatin1Char(' ')));
        }
        lines += plan.runsAfter;
        section(QStringLiteral("Then runs"), lines.join(QLatin1Char('\n')));
    }
    if (!plan.backupFolder.isEmpty())
        section(QStringLiteral("Backs up"), QStringLiteral("Every file above, as it is now, to %1 first. Revert puts them back exactly.").arg(plan.backupFolder));
    if (!plan.note.isEmpty())
        section(QStringLiteral("Note"), plan.note);
    if (!plan.inApp.isEmpty())
        section(QStringLiteral("In Omastrator"), plan.inApp);

    // The dry run: what each file would become.
    layout->addWidget(heading(QStringLiteral("Preview"), this));
    QString preview = plan.diffSummary();
    for (const FileWrite &write : plan.writes) {
        const QString diff = write.diff();
        if (!diff.isEmpty())
            preview += QStringLiteral("\n\n") + write.path + QLatin1Char('\n') + diff;
    }
    m_lines.append(QStringLiteral("Preview: ") + plan.diffSummary());
    m_preview = new QPlainTextEdit(preview, this);
    m_preview->setObjectName(QStringLiteral("syncPreview"));
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_preview->setFont(mono);
    m_preview->setMinimumHeight(140);
    layout->addWidget(m_preview, 1);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    if (plan.problem.isEmpty()) {
        const QString label = !plan.confirmLabel.isEmpty()                  ? plan.confirmLabel
            : !plan.allCommands().empty()                                     ? QStringLiteral("Save and Apply")
            : plan.direction == SyncPlan::Direction::pull                     ? QStringLiteral("Bring In")
            : plan.git && plan.git->commit                                    ? QStringLiteral("Write and Commit")
                                                                              : QStringLiteral("Write Files");
        m_confirm = buttons->addButton(label, QDialogButtonBox::AcceptRole);
        m_confirm->setObjectName(QStringLiteral("syncConfirm"));
        // Enter cancels; confirming is a deliberate click.
        m_confirm->setAutoDefault(false);
        buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    } else {
        layout->addWidget(body(plan.problem, this));
        m_lines.append(QStringLiteral("Can't run: ") + plan.problem);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString SyncConfirmDialog::text() const
{
    return m_lines.join(QLatin1Char('\n'));
}

void SyncConfirmDialog::setResponder(std::function<bool(SyncConfirmDialog &)> answer)
{
    responder() = std::move(answer);
}

QString SyncConfirmDialog::run(const SyncPlan &plan, QWidget *parent, bool *confirmed)
{
    if (confirmed)
        *confirmed = false;
    SyncConfirmDialog dialog(plan, parent);
    const bool accepted = responder() ? responder()(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted || !plan.problem.isEmpty())
        return plan.problem;
    if (confirmed)
        *confirmed = true;
    return SyncRunner::execute(plan, Confirmation(plan));
}
