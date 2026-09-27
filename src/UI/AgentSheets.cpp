#include "UI/AgentSheets.h"
#include "Agent/AgentProtocol.h"
#include "UI/AgentBridge.h"
#include "UI/KeyboardShortcuts.h"
#include "Live/Registry.h"
#include <QCheckBox>
#include <QCommandLinkButton>
#include <QComboBox>
#include <QFileDialog>
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
    const bool selected = bridge.session()->hasSelection();
    auto *scope = new QLabel(selected ? QStringLiteral("Applies to the selection.") : QStringLiteral("Applies to the whole document."), dialog);
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
    auto *logo = new QRadioButton(QStringLiteral("Logo & icon"), dialog);
    logo->setObjectName(QStringLiteral("vectorizeLogo"));
    logo->setChecked(true);
    auto *sketch = new QRadioButton(QStringLiteral("Sketch & line art"), dialog);
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
    text += agent.isEmpty() ? QStringLiteral("Default agent: none. %1\n\n").arg(error)
                            : QStringLiteral("Default agent: %1, which Generate, Edit with Instruction, Vectorize with AI and "
                                             "Roast My Design launch.\n\n").arg(AgentBridge::displayName(agent));
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
    finish(dialog, form, error, QStringLiteral("Start Live"), [&bridge, url, folder, choose] {
        const QString text = url->text().trimmed();
        const QString code = folder->currentData().toString() == choose ? QString() : folder->currentData().toString();
        if (text.isEmpty() && code.isEmpty())
            return QStringLiteral("Enter a page, or choose its project folder.");
        return bridge.startLive(text.isEmpty() ? QUrl() : QUrl::fromUserInput(text), code);
    });
    return dialog;
}

QDialog *publish(AgentBridge &bridge, QWidget *window)
{
    QFormLayout *form = nullptr;
    QLabel *error = nullptr;
    QDialog *dialog = sheet(window, QStringLiteral("publishSheet"), QStringLiteral("Publish"), form, error);
    const auto options = bridge.publishOptions();
    auto *intro = new QLabel(options.empty() ? QStringLiteral("This project has nothing set up to publish with: no git upstream, and no "
                                                              "Vercel, Netlify or Cloudflare CLI.")
                                             : QStringLiteral("Publishing sends what you've saved. Choose one; it runs when you click it."),
                             dialog);
    intro->setWordWrap(true);
    form->addRow(intro);
    for (const auto &option : options) {
        auto *button = new QCommandLinkButton(option.label, option.description, dialog);
        button->setObjectName(QStringLiteral("publish-") + option.id);
        form->addRow(button);
        QObject::connect(button, &QCommandLinkButton::clicked, dialog, [&bridge, dialog, error, id = option.id] {
            QString output;
            const QString failure = bridge.livePublish(id, true, &output);
            if (failure.isEmpty()) {
                dialog->accept();
                return;
            }
            error->setText(failure);
            error->show();
        });
    }
    form->addRow(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("dialogCancel"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    form->addRow(buttons);
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
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    column->addWidget(buttons);
    dialog->open();
    return dialog;
}
}
