#include "UI/AgentSheets.h"
#include "Agent/AgentProtocol.h"
#include "UI/AgentBridge.h"
#include "UI/KeyboardShortcuts.h"
#include "Live/Registry.h"
#include <QCheckBox>
#include <QCommandLinkButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTimer>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// A window-modal sheet with a hidden line for a launch failure.
QDialog *sheet(QWidget *window, const QString &name, const QString &title, QFormLayout *&form, QLabel *&error)
{
    auto *dialog = new QDialog(window);
    dialog->setObjectName(name);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(title);
    dialog->setMinimumWidth(460);
    form = new QFormLayout(dialog);
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(10);
    error = new QLabel(dialog);
    error->setObjectName(QStringLiteral("launchError"));
    error->setWordWrap(true);
    error->hide();
    return dialog;
}

// OK runs `start`; its error shows in the sheet, which then stays open.
void finish(QDialog *dialog, QFormLayout *form, QLabel *error, const QString &okText, std::function<QString()> start)
{
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(okText);
    ok->setObjectName(QStringLiteral("dialogOK"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("dialogCancel"));
    NativeShortcut::bind(*dialog, ok, buttons->button(QDialogButtonBox::Cancel));
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, error, start = std::move(start)] {
        const QString failure = start();
        if (failure.isEmpty()) {
            dialog->accept();
            return;
        }
        error->setText(failure);
        error->show();
        dialog->adjustSize();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    dialog->open();
}

QPlainTextEdit *prompt(QDialog *dialog, const QString &name, const QString &placeholder, const QString &text)
{
    auto *field = new QPlainTextEdit(text, dialog);
    field->setObjectName(name);
    field->setPlaceholderText(placeholder);
    field->setTabChangesFocus(true);
    field->setFixedHeight(92);
    return field;
}

// Names what the instruction will touch, so a right-click's Ask AI shows its target.
QString instructionScope(const EditorSession &session)
{
    const std::vector<QUuid> &selected = session.selection();
    if (selected.empty())
        return QStringLiteral("Applies to the whole document.");
    const VectorObject *object = selected.size() == 1 ? session.document()->find(selected.front()) : nullptr;
    if (object && !object->name.isEmpty())
        return QStringLiteral("Applies to “%1”.").arg(object->name);
    return selected.size() == 1 ? QStringLiteral("Applies to the selection.") : QStringLiteral("Applies to %1 selected objects.").arg(selected.size());
}
}

namespace AgentSheets {
QDialog *generate(AgentBridge &bridge, QWidget *window, const QString &text)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("generateSheet"), QStringLiteral("Generate"), form, error);
    QPlainTextEdit *field = prompt(dialog, QStringLiteral("generatePrompt"), QStringLiteral("A fox logo, geometric, two colours"), text);
    form->addRow(QStringLiteral("Prompt:"), field);
    auto *count = new QSpinBox(dialog);
    count->setObjectName(QStringLiteral("variationCount"));
    count->setRange(1, 6);
    count->setValue(3);
    count->setFixedWidth(80);
    form->addRow(QStringLiteral("Variations:"), count);
    auto *fit = new QCheckBox(QStringLiteral("Fit to selection"), dialog);
    fit->setObjectName(QStringLiteral("fitToSelection"));
    const bool selected = bridge.session()->hasSelection();
    fit->setVisible(selected);
    form->addRow(QString(), fit);
    finish(dialog, form, error, QStringLiteral("Generate"), [&bridge, field, count, fit, selected] {
        return bridge.generate(field->toPlainText(), count->value(), selected && fit->isChecked());
    });
    return dialog;
}

QDialog *editWithInstruction(AgentBridge &bridge, QWidget *window)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("editInstructionSheet"), QStringLiteral("Edit with Instruction"), form, error);
    auto *scope = new QLabel(instructionScope(*bridge.session()), dialog);
    scope->setObjectName(QStringLiteral("instructionScope"));
    QPlainTextEdit *field = prompt(dialog, QStringLiteral("instructionField"), QStringLiteral("Recolor to this palette: #1e66f5, #fe640b"), QString());
    form->addRow(QStringLiteral("Instruction:"), field);
    form->addRow(QString(), scope);
    finish(dialog, form, error, QStringLiteral("Edit"), [&bridge, field] { return bridge.editWithInstruction(field->toPlainText()); });
    return dialog;
}

QDialog *vectorize(AgentBridge &bridge, QWidget *window)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("vectorizeSheet"), QStringLiteral("Vectorize with AI"), form, error);
    auto *logo = new QRadioButton(QStringLiteral("Logo && icon"), dialog);
    logo->setObjectName(QStringLiteral("vectorizeLogo"));
    logo->setChecked(true);
    auto *sketch = new QRadioButton(QStringLiteral("Sketch && line art"), dialog);
    sketch->setObjectName(QStringLiteral("vectorizeSketch"));
    form->addRow(QStringLiteral("Mode:"), logo);
    form->addRow(QString(), sketch);
    finish(dialog, form, error, QStringLiteral("Vectorize"), [&bridge, sketch] {
        return bridge.vectorize(sketch->isChecked() ? AgentLauncher::TraceMode::sketch : AgentLauncher::TraceMode::logo);
    });
    return dialog;
}

QString connectText(const AgentBridge &bridge)
{
    QString text;
    if (bridge.serverError().isEmpty() && !bridge.serverPath().isEmpty())
        text += QStringLiteral("Omastrator is listening at:\n  %1\n\n").arg(bridge.serverPath());
    else
        text += QStringLiteral("Omastrator is not listening for agents: %1\n\n")
                    .arg(bridge.serverError().isEmpty() ? QStringLiteral("the bridge has not started.") : bridge.serverError());
    text += QStringLiteral("Connect Claude Code with:\n  claude mcp add omastrator -- omastrator --mcp\n\n");
    QString error;
    const QString agent = AgentLauncher::defaultAgent(&error);
    if (agent.isEmpty()) {
        text += QStringLiteral("Default agent: none. %1\n\n").arg(error);
    } else {
        const bool headless = !AgentLauncher::showTerminal() && AgentLauncher::headlessCommand(agent, AgentAccess::omastrator, QString(), QString());
        text += QStringLiteral("Default agent: %1, which Generate, Edit with Instruction, Vectorize with AI and Roast My Design "
                               "run %2.\n")
                    .arg(AgentBridge::displayName(agent),
                         headless ? QStringLiteral("in the background, with no terminal and no MCP servers")
                                  : QStringLiteral("in a terminal, through `omarchy agent prompt`"));
        text += QStringLiteral("Logs of background runs: %1\n\n").arg(AgentLauncher::logFolder());
    }
    text += QStringLiteral("Any agent can also use the command line:\n\n") + AgentProtocol::helpText();
    return text;
}

QDialog *live(AgentBridge &bridge, QWidget *window)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("liveSheet"), QStringLiteral("Live"), form, error);
    auto *intro = new QLabel(QStringLiteral("Open a page in Omastrator's own Chromium to edit it live. Changes are "
                                            "written back only to a project folder you confirm here."),
                             dialog);
    intro->setWordWrap(true);
    form->addRow(intro);
    auto *url = new QLineEdit(dialog);
    url->setObjectName(QStringLiteral("liveUrl"));
    url->setPlaceholderText(QStringLiteral("https://your-site.com, or leave empty to run a project folder"));
    form->addRow(QStringLiteral("Page:"), url);
    auto *asApp = new QCheckBox(QStringLiteral("Open it as an app window, as Omarchy's web apps are"), dialog);
    asApp->setObjectName(QStringLiteral("liveAsApp"));
    form->addRow(QString(), asApp);
    auto *command = new QLineEdit(dialog);
    command->setObjectName(QStringLiteral("liveCommand"));
    command->setPlaceholderText(QStringLiteral("Or an Electron app's command, such as /usr/bin/obsidian"));
    form->addRow(QStringLiteral("App:"), command);
    auto *folder = new QComboBox(dialog);
    folder->setObjectName(QStringLiteral("liveFolder"));
    form->addRow(QStringLiteral("Its code:"), folder);
    auto *reason = new QLabel(dialog);
    reason->setObjectName(QStringLiteral("liveReason"));
    reason->setWordWrap(true);
    form->addRow(QString(), reason);
    const QString choose = QStringLiteral("choose");
    // The folder list follows the address: registered first, then suggestions, then a mock-up.
    auto refresh = [url, folder, reason, choose] {
        const QString chosen = folder->currentData().toString();
        folder->clear();
        const QString text = url->text().trimmed();
        const QUrl page = QUrl::fromUserInput(text);
        if (!text.isEmpty()) {
            if (const auto registered = ProjectRegistry::folderFor(page))
                folder->addItem(QStringLiteral("%1 (registered)").arg(*registered), *registered);
            for (const auto &suggestion : ProjectRegistry::suggest(page)) {
                if (folder->findData(suggestion.folder) < 0)
                    folder->addItem(suggestion.folder, suggestion.folder);
                folder->setItemData(folder->count() - 1, suggestion.reason, Qt::ToolTipRole);
            }
            folder->addItem(QStringLiteral("None: a mock-up, changes stay in the browser"), QString());
        }
        folder->addItem(QStringLiteral("Choose a folder…"), choose);
        if (const int again = folder->findData(chosen); again >= 0 && !chosen.isEmpty() && chosen != choose)
            folder->setCurrentIndex(again);
        reason->setText(folder->currentData(Qt::ToolTipRole).toString());
    };
    auto *debounce = new QTimer(dialog);
    debounce->setSingleShot(true);
    debounce->setInterval(400);
    QObject::connect(debounce, &QTimer::timeout, dialog, refresh);
    QObject::connect(url, &QLineEdit::textChanged, debounce, qOverload<>(&QTimer::start));
    QObject::connect(folder, &QComboBox::activated, dialog, [dialog, folder, reason, choose](int index) {
        reason->setText(folder->itemData(index, Qt::ToolTipRole).toString());
        if (folder->itemData(index).toString() != choose)
            return;
        const QString picked = QFileDialog::getExistingDirectory(dialog, QStringLiteral("The page's code"), QDir::homePath());
        if (picked.isEmpty())
            return;
        folder->insertItem(0, picked, picked);
        folder->setCurrentIndex(0);
    });
    refresh();
    finish(dialog, form, error, QStringLiteral("Start Live"), [&bridge, url, folder, choose, command, asApp] {
        const QString text = url->text().trimmed();
        const QString code = folder->currentData().toString() == choose ? QString() : folder->currentData().toString();
        if (text.isEmpty() && code.isEmpty() && command->text().trimmed().isEmpty())
            return QStringLiteral("Enter a page or an app, or choose a project folder.");
        return bridge.startLive(text.isEmpty() ? QUrl() : QUrl::fromUserInput(text), code, command->text().trimmed(), asApp->isChecked());
    });
    return dialog;
}

QDialog *handoff(AgentBridge &bridge, QWidget *window)
{
    return handoffFrom(window, QStringLiteral("the document in front"), QString(),
                       [&bridge](const QString &folder, const QString &notes) { return bridge.handToAgent(folder, notes); });
}

QDialog *handoffFrom(QWidget *window, const QString &what, const QString &given,
                     const std::function<QString(const QString &folder, const QString &notes)> &run)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("handoffSheet"), QStringLiteral("Hand to Agent"), form, error);
    auto *intro = new QLabel(QStringLiteral("Your agent changes the app's source to match %1, on a git branch of its "
                                            "own. Its change is written into the source, and Review changes shows the diff.")
                                 .arg(what.toHtmlEscaped()),
                             dialog);
    intro->setWordWrap(true);
    form->addRow(intro);
    auto *row = new QHBoxLayout;
    auto *folder = new QLineEdit(given, dialog);
    folder->setObjectName(QStringLiteral("handoffFolder"));
    folder->setPlaceholderText(QStringLiteral("The app's source folder"));
    auto *choose = new QPushButton(QStringLiteral("Choose…"), dialog);
    row->addWidget(folder);
    row->addWidget(choose);
    form->addRow(QStringLiteral("Source:"), row);
    QObject::connect(choose, &QPushButton::clicked, dialog, [dialog, folder] {
        const QString picked = QFileDialog::getExistingDirectory(dialog, QStringLiteral("The app's source"), QDir::homePath());
        if (!picked.isEmpty())
            folder->setText(picked);
    });
    QPlainTextEdit *field = prompt(dialog, QStringLiteral("handoffPrompt"), QStringLiteral("Anything the agent should know, such as which screen this is"),
                                   QString());
    form->addRow(QStringLiteral("Notes:"), field);
    finish(dialog, form, error, QStringLiteral("Hand to Agent"), [run, folder, field] { return run(folder->text().trimmed(), field->toPlainText().trimmed()); });
    return dialog;
}

QDialog *deploy(AgentBridge &bridge, QWidget *window, const QString &folder, bool deploying, bool fromFrame)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    const AgentBridge::DeployQuestion question = bridge.deployQuestion(folder);
    QDialog *dialog = sheet(window, QStringLiteral("deploySheet"), deploying ? QStringLiteral("Deploy") : QStringLiteral("Save"), form, error);
    QCheckBox *dontAsk = nullptr;
    if (deploying && question.confirm) {
        const QString how = question.command.viaAgent()
                                ? QStringLiteral("Deploy to production with %1? It deploys with the project's own setup and .env files.")
                                      .arg(question.agent.isEmpty() ? QStringLiteral("your agent") : question.agent)
                                : QStringLiteral("Deploy to production with <code>%1</code>?").arg(question.command.command.toHtmlEscaped());
        auto *ask = new QLabel(how, dialog);
        ask->setObjectName(QStringLiteral("deployQuestion"));
        ask->setWordWrap(true);
        ask->setTextFormat(Qt::RichText);
        form->addRow(ask);
        QStringList files;
        // Names only: the values stay in the files and the deploy's own environment.
        const QStringList keys = Deploy::keys(Deploy::projectEnv(question.folder, question.command.cwd, &files));
        auto *environment = new QLabel(keys.isEmpty() ? QStringLiteral("No .env files: the deploy uses your environment as it is.")
                                                       : QStringLiteral("With %1 from %2.").arg(keys.join(QStringLiteral(", ")),
                                                                                              [&] {
                                                                                                  QStringList names;
                                                                                                  for (const QString &file : files)
                                                                                                      names << QFileInfo(file).fileName();
                                                                                                  return names.join(QStringLiteral(", "));
                                                                                              }()),
                                       dialog);
        environment->setObjectName(QStringLiteral("deployEnvironment"));
        environment->setWordWrap(true);
        form->addRow(environment);
        dontAsk = new QCheckBox(QStringLiteral("Don't ask again for this project"), dialog);
        dontAsk->setObjectName(QStringLiteral("deployDontAsk"));
        form->addRow(dontAsk);
    }
    QCheckBox *create = nullptr;
    QLineEdit *name = nullptr;
    if (!question.github.isEmpty()) {
        create = new QCheckBox(QStringLiteral("Keep its history on GitHub: create a private repository"), dialog);
        create->setObjectName(QStringLiteral("deployCreateRepository"));
        create->setChecked(true);
        form->addRow(create);
        name = new QLineEdit(question.github, dialog);
        name->setObjectName(QStringLiteral("deployRepositoryName"));
        form->addRow(QStringLiteral("Name:"), name);
        QObject::connect(create, &QCheckBox::toggled, name, &QLineEdit::setEnabled);
    }
    finish(dialog, form, error, deploying ? QStringLiteral("Deploy") : QStringLiteral("Save"), [&bridge, question, deploying, dontAsk, create, name, named = !folder.isEmpty(), fromFrame] {
        AgentBridge::DeployRequest request;
        request.deploy = deploying;
        request.confirm = true;
        request.remember = dontAsk && dontAsk->isChecked();
        request.fromFrame = fromFrame;
        // Asked with no folder (the island's Deploy), it answers with none, so the island stays what it was.
        if (named)
            request.folder = question.folder;
        if (create)
            request.github = create->isChecked() ? name->text().trimmed() : QString();
        if (create && create->isChecked() && name->text().trimmed().isEmpty())
            return QStringLiteral("Name the repository.");
        return bridge.liveDeploy(request);
    });
    return dialog;
}

QDialog *deployLog(QWidget *window, const QString &path, AgentBridge *bridge, const QString &folder)
{
    auto *dialog = new QDialog(window);
    dialog->setObjectName(QStringLiteral("deployLog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QStringLiteral("Deploy Details"));
    auto *column = new QVBoxLayout(dialog);
    column->setContentsMargins(24, 20, 24, 20);
    auto *where = new QLabel(path, dialog);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(where);
    QFile file(path);
    auto *text = new QPlainTextEdit(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QStringLiteral("The log is gone."), dialog);
    text->setObjectName(QStringLiteral("deployLogText"));
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setMinimumSize(680, 420);
    text->moveCursor(QTextCursor::End);
    column->addWidget(text);
    if (bridge && bridge->deployFailed(folder, path))
        addDeployFix(dialog, column, *bridge, folder);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    column->addWidget(buttons);
    dialog->open();
    return dialog;
}

QDialog *connectAgent(AgentBridge &bridge, QWidget *window)
{
    auto *dialog = new QDialog(window);
    dialog->setObjectName(QStringLiteral("connectAgentSheet"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QStringLiteral("Connect an Agent"));
    auto *column = new QVBoxLayout(dialog);
    column->setContentsMargins(24, 20, 24, 20);
    auto *text = new QPlainTextEdit(connectText(bridge), dialog);
    text->setObjectName(QStringLiteral("connectText"));
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setMinimumSize(640, 420);
    column->addWidget(text);
    auto *terminal = new QCheckBox(QStringLiteral("Open the agent in a terminal while it works"), dialog);
    terminal->setObjectName(QStringLiteral("showTerminal"));
    terminal->setChecked(AgentLauncher::showTerminal());
    QObject::connect(terminal, &QCheckBox::toggled, dialog, [&bridge, text](bool on) {
        AgentLauncher::setShowTerminal(on);
        text->setPlainText(connectText(bridge));
    });
    column->addWidget(terminal);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    column->addWidget(buttons);
    dialog->open();
    return dialog;
}
}
