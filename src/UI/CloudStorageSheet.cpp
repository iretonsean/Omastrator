#include "UI/CloudStorageSheet.h"
#include "UI/CloudBadge.h"
#include "UI/KeyboardShortcuts.h"
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QLabel *heading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPixelSize(17);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QLabel *note(const QString &text, QWidget *parent, const QString &name = QString())
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

QLabel *errorLine(QWidget *parent, const QString &name)
{
    auto *label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::BrightText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->hide();
    return label;
}

QPushButton *button(const QString &text, const QString &name, QWidget *parent)
{
    auto *made = new QPushButton(text, parent);
    made->setObjectName(name);
    return made;
}

QString omarchy()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_OMARCHY");
    return overridden.isEmpty() ? QStringLiteral("omarchy") : overridden;
}
}

CloudStorageSheet::CloudStorageSheet(CloudStorage &storage, QWidget *parent) : QDialog(parent), m_storage(storage), m_pages(new QStackedWidget(this))
{
    setObjectName(QStringLiteral("cloudStorageSheet"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(QStringLiteral("Cloud Storage"));
    setMinimumWidth(480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->addWidget(m_pages);
    m_pages->addWidget(makeRemotesPage());
    m_pages->addWidget(makeProvidersPage());
    m_pages->addWidget(makeFormPage());
    m_pages->addWidget(makeQuestionPage());
    m_pages->addWidget(makeWaitingPage());
    m_pages->addWidget(makeMissingPage());
    connect(&m_storage, &CloudStorage::remotesChanged, this, &CloudStorageSheet::fillRemotes);
    // While rclone is missing, look again now and then: the install happens in a terminal.
    m_installPoll.setInterval(2000);
    connect(&m_installPoll, &QTimer::timeout, this, &CloudStorageSheet::checkInstalled);
    showRemotes();
    if (CloudStorage::isInstalled())
        m_storage.refreshRemotes();
}

CloudStorageSheet::~CloudStorageSheet()
{
    abandon();
}

QString CloudStorageSheet::freeName(const QString &wanted, const QList<CloudRemote> &taken)
{
    const auto used = [&](const QString &name) {
        return std::any_of(taken.begin(), taken.end(), [&](const CloudRemote &each) { return each.name == name; });
    };
    QString name = wanted.isEmpty() ? QStringLiteral("remote") : wanted;
    for (int number = 2; used(name); ++number)
        name = QStringLiteral("%1-%2").arg(wanted).arg(number);
    return name;
}

QString CloudStorageSheet::installRclone()
{
    const QStringList command{omarchy(), QStringLiteral("pkg"), QStringLiteral("add"), QStringLiteral("rclone")};
    const QString manual = QStringLiteral("Install it from a terminal with: omarchy pkg add rclone");
    QString program = qEnvironmentVariable("OMASTRATOR_TERMINAL");
    QStringList arguments = command;
    if (program.isEmpty()) {
        // Omarchy's own installers run in its floating terminal; any terminal will do otherwise.
        program = QStandardPaths::findExecutable(QStringLiteral("omarchy-launch-floating-terminal-with-presentation"));
        // xdg-terminal-exec adds its terminal's own -e.
        if (program.isEmpty())
            program = QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec"));
    }
    if (program.isEmpty())
        return QStringLiteral("There's no terminal to install rclone in. ") + manual;
    if (!QProcess::startDetached(program, arguments))
        return QStringLiteral("The terminal didn't open. ") + manual;
    return {};
}

QWidget *CloudStorageSheet::makeRemotesPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addWidget(heading(QStringLiteral("Cloud storage"), page));
    column->addWidget(note(QStringLiteral("Open and save documents on these as well as on this computer. rclone keeps the sign-ins; "
                                          "Omastrator never sees them."),
                           page));
    m_remoteList = new QListWidget(page);
    m_remoteList->setObjectName(QStringLiteral("remoteList"));
    m_remoteList->setIconSize(QSize(20, 20));
    m_remoteList->setMinimumHeight(140);
    column->addWidget(m_remoteList);
    m_emptyNote = note(QStringLiteral("No cloud storage connected."), page, QStringLiteral("noRemotes"));
    column->addWidget(m_emptyNote);
    m_message = note(QString(), page, QStringLiteral("storageMessage"));
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(m_message);
    auto *buttons = new QHBoxLayout;
    QPushButton *add = button(QStringLiteral("Connect…"), QStringLiteral("connectButton"), page);
    m_disconnect = button(QStringLiteral("Disconnect"), QStringLiteral("disconnectButton"), page);
    QPushButton *done = button(QStringLiteral("Done"), QStringLiteral("doneButton"), page);
    done->setDefault(true);
    buttons->addWidget(add);
    buttons->addWidget(m_disconnect);
    buttons->addStretch(1);
    buttons->addWidget(done);
    column->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, [this] {
        m_providerList->setCurrentRow(0);
        m_pages->setCurrentIndex(providersPage);
    });
    connect(m_disconnect, &QPushButton::clicked, this, &CloudStorageSheet::disconnectSelected);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_remoteList, &QListWidget::currentRowChanged, this, [this](int row) { m_disconnect->setEnabled(row >= 0); });
    return page;
}

QWidget *CloudStorageSheet::makeProvidersPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addWidget(heading(QStringLiteral("Connect cloud storage"), page));
    m_providerList = new QListWidget(page);
    m_providerList->setObjectName(QStringLiteral("providerList"));
    m_providerList->setIconSize(QSize(20, 20));
    for (const CloudProvider &provider : CloudProviders::all())
        m_providerList->addItem(new QListWidgetItem(CloudBadge::icon(provider.type), provider.name));
    m_providerList->setMinimumHeight(300);
    column->addWidget(m_providerList);
    auto *buttons = new QHBoxLayout;
    QPushButton *back = button(QStringLiteral("Back"), QStringLiteral("backButton"), page);
    QPushButton *next = button(QStringLiteral("Next"), QStringLiteral("nextButton"), page);
    buttons->addWidget(back);
    buttons->addStretch(1);
    buttons->addWidget(next);
    column->addLayout(buttons);
    const auto choose = [this] {
        const int row = m_providerList->currentRow();
        if (row >= 0)
            showForm(CloudProviders::all().at(size_t(row)));
    };
    connect(back, &QPushButton::clicked, this, &CloudStorageSheet::showRemotes);
    connect(next, &QPushButton::clicked, this, choose);
    connect(m_providerList, &QListWidget::itemActivated, this, choose);
    return page;
}

QWidget *CloudStorageSheet::makeFormPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    m_formTitle = heading(QString(), page);
    column->addWidget(m_formTitle);
    m_formNote = note(QString(), page, QStringLiteral("formNote"));
    column->addWidget(m_formNote);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(page);
    m_name->setObjectName(QStringLiteral("remoteName"));
    form->addRow(QStringLiteral("Name:"), m_name);
    column->addLayout(form);
    m_fieldsHolder = new QWidget(page);
    auto *holder = new QVBoxLayout(m_fieldsHolder);
    holder->setContentsMargins(0, 0, 0, 0);
    column->addWidget(m_fieldsHolder);
    m_formError = errorLine(page, QStringLiteral("connectError"));
    column->addWidget(m_formError);
    column->addStretch(1);
    auto *buttons = new QHBoxLayout;
    QPushButton *back = button(QStringLiteral("Back"), QStringLiteral("formBack"), page);
    QPushButton *go = button(QStringLiteral("Connect"), QStringLiteral("connectRemote"), page);
    go->setDefault(true);
    buttons->addWidget(back);
    buttons->addStretch(1);
    buttons->addWidget(go);
    column->addLayout(buttons);
    connect(back, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(providersPage); });
    connect(go, &QPushButton::clicked, this, &CloudStorageSheet::connectRemote);
    connect(m_name, &QLineEdit::returnPressed, this, &CloudStorageSheet::connectRemote);
    return page;
}

QWidget *CloudStorageSheet::makeQuestionPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    m_questionTitle = heading(QString(), page);
    column->addWidget(m_questionTitle);
    m_questionHelp = note(QString(), page, QStringLiteral("questionHelp"));
    m_questionHelp->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(m_questionHelp);
    m_choice = new QComboBox(page);
    m_choice->setObjectName(QStringLiteral("questionChoice"));
    column->addWidget(m_choice);
    m_answer = new QLineEdit(page);
    m_answer->setObjectName(QStringLiteral("questionAnswer"));
    column->addWidget(m_answer);
    m_questionError = errorLine(page, QStringLiteral("questionError"));
    column->addWidget(m_questionError);
    column->addStretch(1);
    auto *buttons = new QHBoxLayout;
    QPushButton *cancel = button(QStringLiteral("Cancel"), QStringLiteral("questionCancel"), page);
    QPushButton *go = button(QStringLiteral("Continue"), QStringLiteral("questionContinue"), page);
    go->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(go);
    column->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &CloudStorageSheet::cancelConnect);
    connect(go, &QPushButton::clicked, this, &CloudStorageSheet::answerQuestion);
    connect(m_answer, &QLineEdit::returnPressed, this, &CloudStorageSheet::answerQuestion);
    return page;
}

QWidget *CloudStorageSheet::makeWaitingPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addStretch(1);
    m_waitingText = new QLabel(page);
    m_waitingText->setObjectName(QStringLiteral("waitingText"));
    m_waitingText->setWordWrap(true);
    m_waitingText->setAlignment(Qt::AlignCenter);
    column->addWidget(m_waitingText);
    column->addStretch(1);
    auto *buttons = new QHBoxLayout;
    QPushButton *cancel = button(QStringLiteral("Cancel"), QStringLiteral("waitingCancel"), page);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    column->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &CloudStorageSheet::cancelConnect);
    return page;
}

QWidget *CloudStorageSheet::makeMissingPage()
{
    auto *page = new QWidget(m_pages);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);
    column->addWidget(heading(QStringLiteral("Cloud storage"), page));
    auto *line = new QLabel(QStringLiteral("rclone isn't installed. Omastrator uses it to reach cloud storage."), page);
    line->setObjectName(QStringLiteral("missingRclone"));
    line->setWordWrap(true);
    column->addWidget(line);
    m_installError = errorLine(page, QStringLiteral("installError"));
    column->addWidget(m_installError);
    column->addStretch(1);
    auto *buttons = new QHBoxLayout;
    QPushButton *install = button(QStringLiteral("Install rclone"), QStringLiteral("installRclone"), page);
    install->setDefault(true);
    QPushButton *again = button(QStringLiteral("Check Again"), QStringLiteral("checkAgain"), page);
    QPushButton *close = button(QStringLiteral("Close"), QStringLiteral("closeMissing"), page);
    buttons->addWidget(install);
    buttons->addWidget(again);
    buttons->addStretch(1);
    buttons->addWidget(close);
    column->addLayout(buttons);
    connect(install, &QPushButton::clicked, this, [this] {
        const QString failure = installRclone();
        m_installError->setText(failure);
        m_installError->setVisible(!failure.isEmpty());
    });
    connect(again, &QPushButton::clicked, this, &CloudStorageSheet::checkInstalled);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    return page;
}

void CloudStorageSheet::checkInstalled()
{
    if (!CloudStorage::isInstalled())
        return;
    m_installPoll.stop();
    m_storage.refreshRemotes();
    showRemotes();
}

void CloudStorageSheet::showRemotes()
{
    if (!CloudStorage::isInstalled()) {
        m_pages->setCurrentIndex(missingPage);
        m_installPoll.start();
        return;
    }
    fillRemotes();
    m_pages->setCurrentIndex(remotesPage);
}

void CloudStorageSheet::fillRemotes()
{
    const QString selected = m_remoteList->currentItem() ? m_remoteList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_remoteList->clear();
    for (const CloudRemote &remote : m_storage.remotes()) {
        auto *item = new QListWidgetItem(CloudBadge::icon(remote.type), QStringLiteral("%1 — %2").arg(remote.name, CloudProviders::forType(remote.type).name));
        item->setData(Qt::UserRole, remote.name);
        m_remoteList->addItem(item);
        if (remote.name == selected)
            m_remoteList->setCurrentItem(item);
    }
    m_remoteList->setVisible(m_remoteList->count() > 0);
    m_emptyNote->setVisible(m_remoteList->count() == 0);
    m_disconnect->setEnabled(m_remoteList->currentRow() >= 0);
}

void CloudStorageSheet::showForm(const CloudProvider &provider)
{
    m_provider = provider;
    m_formTitle->setText(QStringLiteral("Connect %1").arg(provider.type.isEmpty() ? QStringLiteral("another service") : provider.name));
    m_formNote->setText(provider.browserSignIn
                            ? QStringLiteral("Your browser opens to sign in to %1. rclone keeps the sign-in; Omastrator never sees or stores it.")
                                  .arg(provider.name)
                            : QStringLiteral("These go straight to rclone, which keeps them in its own config. Omastrator doesn't store them."));
    m_name->setText(freeName(provider.suggestedRemote, m_storage.remotes()));
    // A fresh set of fields for this provider.
    delete m_fields;
    m_inputs.clear();
    m_fields = new QWidget(m_fieldsHolder);
    m_fieldsForm = new QFormLayout(m_fields);
    m_fieldsForm->setContentsMargins(0, 0, 0, 0);
    for (const CloudField &field : provider.fields) {
        QWidget *input = nullptr;
        if (!field.choices.isEmpty()) {
            auto *choice = new QComboBox(m_fields);
            choice->addItems(field.choices);
            input = choice;
        } else {
            auto *line = new QLineEdit(m_fields);
            line->setPlaceholderText(field.optional && field.placeholder.isEmpty() ? QStringLiteral("Optional") : field.placeholder);
            if (field.secret)
                line->setEchoMode(QLineEdit::Password);
            connect(line, &QLineEdit::returnPressed, this, &CloudStorageSheet::connectRemote);
            input = line;
        }
        input->setObjectName(QStringLiteral("field_") + field.key);
        m_fieldsForm->addRow(field.label + QLatin1Char(':'), input);
        m_inputs.emplace_back(field, input);
    }
    m_fieldsHolder->layout()->addWidget(m_fields);
    m_formError->hide();
    m_pages->setCurrentIndex(formPage);
    m_name->setFocus();
}

void CloudStorageSheet::connectRemote()
{
    const auto fail = [this](const QString &message) {
        m_formError->setText(message);
        m_formError->show();
    };
    const QString name = m_name->text().trimmed();
    if (!CloudLocation::isValidRemoteName(name))
        return fail(QStringLiteral("A name can use letters, numbers, spaces and - _ . + @, and can't start with a space or -."));
    if (m_storage.remote(name))
        return fail(QStringLiteral("“%1” is already connected. Choose another name.").arg(name));
    QString type = m_provider.type;
    QList<std::pair<QString, QString>> options;
    for (const auto &[field, input] : m_inputs) {
        const QString value = qobject_cast<QComboBox *>(input) ? static_cast<QComboBox *>(input)->currentText()
                                                                : static_cast<QLineEdit *>(input)->text().trimmed();
        if (value.isEmpty() && !field.optional)
            return fail(QStringLiteral("Fill in %1.").arg(field.label));
        if (field.key == QLatin1String("type"))
            type = value;
        else if (!value.isEmpty())
            options.append({field.key, value});
    }
    static const QRegularExpression backend(QStringLiteral("^[a-z0-9]+$"));
    if (!backend.match(type).hasMatch())
        return fail(QStringLiteral("“%1” isn't an rclone backend name. `rclone help backends` lists them.").arg(type));
    // Passwords leave the fields as soon as they're sent.
    for (const auto &[field, input] : m_inputs) {
        if (field.secret)
            static_cast<QLineEdit *>(input)->clear();
    }
    m_connecting = name;
    m_cancelling = false;
    wait(QStringLiteral("Connecting to %1…").arg(m_provider.type.isEmpty() ? type : m_provider.name));
    // "Other" asks every basic question, since there's no form for it.
    m_job = m_storage.createRemote(name, type, options, m_provider.type.isEmpty(), [sheet = QPointer<CloudStorageSheet>(this)](const CloudConfigStep &step) {
        if (sheet)
            sheet->handle(step);
    });
}

void CloudStorageSheet::handle(const CloudConfigStep &step)
{
    m_job = nullptr;
    if (m_connecting.isEmpty())
        return;
    if (m_cancelling || (!step.finished && !step.question)) {
        const QString error = m_cancelling ? QString() : step.error;
        abandon();
        m_formError->setText(error.isEmpty() ? QString() : QStringLiteral("Couldn't connect. %1").arg(error));
        m_formError->setVisible(!error.isEmpty());
        m_pages->setCurrentIndex(formPage);
        return;
    }
    if (step.finished) {
        const QString name = m_connecting;
        m_connecting.clear();
        m_message->setText(QStringLiteral("Connected “%1”.").arg(name));
        m_storage.refreshRemotes();
        showRemotes();
        return;
    }
    // "Use the browser?" is always yes here: it opens the provider's sign-in page.
    if (step.question->name == QLatin1String("config_is_local")) {
        wait(QStringLiteral("Sign in to %1 in your browser. rclone keeps the sign-in; Omastrator never sees it.").arg(m_provider.name));
        m_job = m_storage.answer(m_connecting, step.question->state, QStringLiteral("true"), [sheet = QPointer<CloudStorageSheet>(this)](const CloudConfigStep &next) {
            if (sheet)
                sheet->handle(next);
        });
        return;
    }
    ask(*step.question);
}

void CloudStorageSheet::ask(const CloudQuestion &question)
{
    m_question = question;
    m_questionTitle->setText(QStringLiteral("%1 needs an answer").arg(m_provider.type.isEmpty() ? QStringLiteral("rclone") : m_provider.name));
    m_questionHelp->setText(question.help.isEmpty() ? question.name : question.help);
    m_questionError->setText(question.error);
    m_questionError->setVisible(!question.error.isEmpty());
    const bool choosing = !question.choices.isEmpty();
    m_choice->setVisible(choosing);
    m_answer->setVisible(!choosing);
    m_choice->clear();
    m_choice->setEditable(choosing && !question.exclusive);
    for (qsizetype index = 0; index < question.choices.size(); ++index) {
        const QString help = question.choiceHelp.value(index);
        m_choice->addItem(help.isEmpty() || help == question.choices[index] ? question.choices[index]
                                                                           : QStringLiteral("%1 (%2)").arg(help, question.choices[index]),
                          question.choices[index]);
    }
    if (choosing)
        m_choice->setCurrentIndex(std::max(0, int(question.choices.indexOf(question.defaultValue))));
    m_answer->setEchoMode(question.password ? QLineEdit::Password : QLineEdit::Normal);
    m_answer->setText(question.defaultValue);
    m_pages->setCurrentIndex(questionPage);
    (choosing ? static_cast<QWidget *>(m_choice) : m_answer)->setFocus();
}

void CloudStorageSheet::answerQuestion()
{
    if (m_connecting.isEmpty())
        return;
    QString value = m_answer->text();
    if (m_choice->isVisible() || !m_question.choices.isEmpty()) {
        const int index = m_choice->currentIndex();
        value = m_choice->isEditable() && m_choice->currentText() != m_choice->itemText(index) ? m_choice->currentText()
                                                                                                : m_choice->itemData(index).toString();
    }
    if (value.isEmpty() && m_question.required) {
        m_questionError->setText(QStringLiteral("This one needs an answer."));
        m_questionError->show();
        return;
    }
    m_answer->clear();
    wait(QStringLiteral("Connecting to %1…").arg(m_provider.type.isEmpty() ? QStringLiteral("the service") : m_provider.name));
    m_job = m_storage.answer(m_connecting, m_question.state, value, [sheet = QPointer<CloudStorageSheet>(this)](const CloudConfigStep &step) {
        if (sheet)
            sheet->handle(step);
    });
}

void CloudStorageSheet::wait(const QString &text)
{
    m_waitingText->setText(text);
    m_pages->setCurrentIndex(waitingPage);
}

void CloudStorageSheet::cancelConnect()
{
    if (m_job && m_job->isRunning()) {
        m_cancelling = true;
        m_job->cancel();
        return;
    }
    abandon();
    m_formError->hide();
    m_pages->setCurrentIndex(formPage);
}

// A half-made remote is removed, so a failed connect leaves nothing behind in rclone's config.
void CloudStorageSheet::abandon()
{
    if (m_connecting.isEmpty())
        return;
    const QString name = m_connecting;
    m_connecting.clear();
    if (m_job)
        m_job->cancel();
    m_storage.deleteRemote(name, [](const QString &) {});
}

void CloudStorageSheet::reject()
{
    abandon();
    QDialog::reject();
}

void CloudStorageSheet::disconnectSelected()
{
    QListWidgetItem *item = m_remoteList->currentItem();
    if (!item)
        return;
    const QString name = item->data(Qt::UserRole).toString();
    auto *alert = new QMessageBox(this);
    alert->setObjectName(QStringLiteral("disconnectAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Question);
    alert->setText(QStringLiteral("Disconnect “%1”?").arg(name));
    alert->setInformativeText(QStringLiteral("rclone forgets this sign-in. Nothing on %1 is deleted.").arg(m_storage.serviceName(name)));
    const QPushButton *confirm = alert->addButton(QStringLiteral("Disconnect"), QMessageBox::AcceptRole);
    alert->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    connect(alert, &QDialog::finished, this, [this, alert, confirm, name] {
        if (alert->clickedButton() != confirm)
            return;
        m_storage.deleteRemote(name, [this, sheet = QPointer<CloudStorageSheet>(this), name](const QString &error) {
            if (!sheet)
                return;
            m_message->setText(error.isEmpty() ? QStringLiteral("Disconnected “%1”.").arg(name)
                                               : QStringLiteral("Couldn't disconnect “%1”. %2").arg(name, error));
            m_storage.refreshRemotes();
        });
    });
    alert->open();
}
