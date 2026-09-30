#include "UI/GeneratePageSheet.h"
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <memory>

namespace {
std::function<std::optional<GeneratePageSheet::Answer>(bool)> &responder()
{
    static std::function<std::optional<GeneratePageSheet::Answer>(bool)> answer;
    return answer;
}

QString tilde(const QString &path)
{
    const QString home = QDir::homePath();
    return path == home || path.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + path.mid(home.size()) : path;
}

QString expanded(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed == QLatin1String("~") || trimmed.startsWith(QLatin1String("~/")))
        return QDir::homePath() + trimmed.mid(1);
    return trimmed;
}
}

namespace GeneratePageSheet {
QString suggestedFolder(const QString &description)
{
    const QString projects = QDir::home().filePath(QStringLiteral("Projects"));
    const QString parent = QFileInfo(projects).isDir() ? projects : QDir::homePath();
    const QString name = PageTemplates::slug(description).isEmpty() ? QStringLiteral("new-page") : PageTemplates::slug(description);
    QString folder = QDir(parent).filePath(name);
    for (int n = 2; !PageTemplates::folderProblem(folder).isEmpty() && n < 100; ++n)
        folder = QDir(parent).filePath(QStringLiteral("%1-%2").arg(name).arg(n));
    return folder;
}

void setResponder(std::function<std::optional<Answer>(bool)> answer)
{
    ::responder() = std::move(answer);
}

QString open(QWidget *window, bool describe, const QString &frameName, const std::function<QString(const Answer &)> &run)
{
    if (::responder()) {
        const std::optional<Answer> answer = ::responder()(describe);
        return answer ? run(*answer) : QString();
    }
    auto *dialog = new QDialog(window);
    dialog->setObjectName(QStringLiteral("generatePageSheet"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(describe ? QStringLiteral("Generate a page") : QStringLiteral("Build It in a new project"));
    dialog->setMinimumWidth(500);
    auto *form = new QFormLayout(dialog);
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(10);

    auto *intro = new QLabel(describe ? QStringLiteral("Your default agent writes the page for %1 into a new project. You see every file before anything is "
                                                       "created.")
                                            .arg(frameName.toHtmlEscaped())
                                      : QStringLiteral("Build It needs a project to change. A new folder gets a starter project first, which you confirm; "
                                                       "a folder that already holds a project is built into as it is."),
                             dialog);
    intro->setWordWrap(true);
    form->addRow(intro);

    QPlainTextEdit *field = nullptr;
    if (describe) {
        field = new QPlainTextEdit(dialog);
        field->setObjectName(QStringLiteral("generatePagePrompt"));
        field->setPlaceholderText(QStringLiteral("A landing page for a small coffee roaster. Bold type, warm colours, three featured beans."));
        field->setTabChangesFocus(true);
        field->setFixedHeight(84);
        form->addRow(QStringLiteral("What should it be?"), field);
        auto *chips = new QHBoxLayout;
        for (const QString &kind : {QStringLiteral("Landing page"), QStringLiteral("Portfolio"), QStringLiteral("Pricing page"), QStringLiteral("Docs home")}) {
            auto *chip = new QPushButton(kind, dialog);
            chip->setObjectName(QStringLiteral("generatePageKind"));
            chip->setFlat(true);
            chips->addWidget(chip);
            QObject::connect(chip, &QPushButton::clicked, dialog, [field, kind] {
                // A chip starts the sentence; what was typed after it stays.
                field->setPlainText(QStringLiteral("A %1 for ").arg(kind.toLower()));
                field->setFocus();
                field->moveCursor(QTextCursor::End);
            });
        }
        chips->addStretch();
        form->addRow(QString(), chips);
    }

    auto *stacks = new QHBoxLayout;
    auto *group = new QButtonGroup(dialog);
    PageTemplates::Stack chosen = PageTemplates::Stack::viteTailwind;
    for (const PageTemplates::Stack stack : PageTemplates::stacks()) {
        auto *radio = new QRadioButton(PageTemplates::label(stack), dialog);
        radio->setObjectName(QStringLiteral("generatePageStack-") + PageTemplates::id(stack));
        radio->setChecked(stack == chosen);
        group->addButton(radio, int(stack));
        stacks->addWidget(radio);
    }
    stacks->addStretch();
    form->addRow(QStringLiteral("Built with"), stacks);

    auto *row = new QHBoxLayout;
    auto *folder = new QLineEdit(tilde(suggestedFolder(QString())), dialog);
    folder->setObjectName(QStringLiteral("generatePageFolder"));
    auto *choose = new QPushButton(QStringLiteral("Choose…"), dialog);
    row->addWidget(folder);
    row->addWidget(choose);
    form->addRow(QStringLiteral("Project folder"), row);
    QObject::connect(choose, &QPushButton::clicked, dialog, [dialog, folder] {
        const QString picked = QFileDialog::getExistingDirectory(dialog, QStringLiteral("Project folder"), QDir::homePath());
        if (!picked.isEmpty())
            folder->setText(tilde(picked));
    });
    // The folder follows the description until its own field is edited.
    const auto edited = std::make_shared<bool>(false);
    QObject::connect(folder, &QLineEdit::textEdited, dialog, [edited] { *edited = true; });
    if (field) {
        QObject::connect(field, &QPlainTextEdit::textChanged, dialog, [field, folder, edited] {
            if (!*edited)
                folder->setText(tilde(suggestedFolder(field->toPlainText())));
        });
    }

    auto *error = new QLabel(dialog);
    error->setObjectName(QStringLiteral("launchError"));
    error->setWordWrap(true);
    error->hide();
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    QPushButton *go = buttons->addButton(describe ? QStringLiteral("Generate") : QStringLiteral("Continue…"), QDialogButtonBox::AcceptRole);
    go->setObjectName(QStringLiteral("dialogOK"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("dialogCancel"));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        Answer answer;
        answer.description = field ? field->toPlainText().trimmed() : QString();
        answer.stack = PageTemplates::Stack(group->checkedId());
        answer.folder = expanded(folder->text());
        const QString failure = run(answer);
        if (failure.isEmpty()) {
            dialog->accept();
            return;
        }
        error->setText(failure);
        error->show();
        dialog->adjustSize();
    });
    dialog->open();
    return {};
}
}
