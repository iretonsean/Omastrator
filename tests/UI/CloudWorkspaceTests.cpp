#include "../Cloud/FakeCloud.h"
#include "Document/PathOperations.h"
#include "IO/ProjectStore.h"
#include "UI/CloudBrowser.h"
#include "UI/CloudStorageSheet.h"
#include "UI/ProjectWorkspace.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

// Cloud storage as the user meets it: the sheet, the browser, and a workspace that opens,
// saves, uploads, resolves conflicts and waits out being offline, all through the fake rclone.
namespace {
QStringList logged;

void keepLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    logged << message;
}

template <typename T = QWidget>
T *shown(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return qobject_cast<T *>(widget);
    }
    return nullptr;
}

void click(QMessageBox &box, const QString &text)
{
    for (QAbstractButton *button : box.buttons()) {
        if (button->text() == text) {
            button->click();
            return;
        }
    }
    QFAIL(qPrintable("no button " + text));
}

template <typename T>
T *child(QWidget &parent, const QString &name)
{
    T *found = parent.findChild<T *>(name);
    if (!found)
        qWarning("no child %s", qPrintable(name));
    return found;
}

// Every piece of text a widget tree shows or holds.
QString allText(QWidget &root)
{
    QStringList texts;
    for (QLabel *label : root.findChildren<QLabel *>())
        texts << label->text() << label->toolTip();
    for (QLineEdit *line : root.findChildren<QLineEdit *>())
        texts << line->text();
    for (QListWidget *list : root.findChildren<QListWidget *>()) {
        for (int row = 0; row < list->count(); ++row)
            texts << list->item(row)->text() << list->item(row)->toolTip();
    }
    return texts.join(QLatin1Char('\n'));
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QListWidgetItem *entry(CloudBrowser &browser, const QString &name)
{
    auto *entries = browser.findChild<QListWidget *>(QStringLiteral("entries"));
    for (int row = 0; row < entries->count(); ++row) {
        if (entries->item(row)->text() == name)
            return entries->item(row);
    }
    return nullptr;
}
}

class CloudWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();
    void connectingAndDisconnectingFromTheSheet();
    void aServiceThatAsksForACode();
    void missingRcloneOffersToInstallIt();
    void openEditSaveUploads();
    void saveAsAndExportGoToARemote();
    void placeDownloadsFirst();
    void keepBothUploadsYoursAsACopy();
    void overwriteReplacesTheirs();
    void openTheirsKeepsYoursUnsaved();
    void offlineSavesUploadLater();
    void closingWithAnUploadPendingAsks();
    void recentFilesRememberTheService();

private:
    // Opens `path` on `work` through File ▸ Open and the browser, as a user would.
    void openThroughBrowser(ProjectWorkspace &workspace, const QString &folder, const QString &file);
    void putDocument(const QString &remote, const QString &path);
    std::unique_ptr<FakeCloud> m_cloud;
};

void CloudWorkspaceTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    qInstallMessageHandler(keepLog);
}

void CloudWorkspaceTests::init()
{
    QSettings().clear();
    m_cloud = std::make_unique<FakeCloud>();
    m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
}

void CloudWorkspaceTests::cleanup()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (qobject_cast<QMessageBox *>(widget) || qobject_cast<QDialog *>(widget))
            delete widget;
    }
    QTest::qWait(50);
    m_cloud.reset();
}

// The secret the fake puts in rclone's config never reaches a log line or Omastrator's settings.
void CloudWorkspaceTests::cleanupTestCase()
{
    qInstallMessageHandler(nullptr);
    const QString everything = logged.join(QLatin1Char('\n'));
    QVERIFY(!everything.contains(QLatin1String(FakeCloud::secret)));
    QVERIFY(!everything.contains(QLatin1String("FAKE-REFRESH")));
    QVERIFY(!everything.contains(QLatin1String("hunter2")));
}

void CloudWorkspaceTests::putDocument(const QString &remote, const QString &path)
{
    VectorDocument document;
    document.size = QSizeF(200, 100);
    const QString local = m_cloud->root() + QStringLiteral("/made.omai");
    ProjectStore::write(document, local);
    m_cloud->put(remote, path, readFile(local), QDateTime::currentDateTime().addSecs(-3600));
}

void CloudWorkspaceTests::openThroughBrowser(ProjectWorkspace &workspace, const QString &folder, const QString &file)
{
    if (workspace.cloud().remotes().isEmpty()) {
        QSignalSpy listed(&workspace.cloud(), &CloudStorage::remotesChanged);
        workspace.cloud().refreshRemotes();
        QVERIFY(listed.wait(10000));
    }
    workspace.open();
    CloudBrowser *browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    browser->showRemote(QStringLiteral("work"));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading() && entry(*browser, folder), 10000);
    // Folders come first and open in place.
    auto *entries = browser->findChild<QListWidget *>(QStringLiteral("entries"));
    QCOMPARE(entries->item(0)->text(), folder);
    emit entries->itemActivated(entry(*browser, folder));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading() && entry(*browser, file), 10000);
    QCOMPARE(browser->folder().path, folder);
    entries->setCurrentItem(entry(*browser, file));
    child<QPushButton>(*browser, QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(workspace.current().cloud.has_value(), 10000);
}

void CloudWorkspaceTests::connectingAndDisconnectingFromTheSheet()
{
    CloudStorage storage;
    QSignalSpy listed(&storage, &CloudStorage::remotesChanged);
    CloudStorageSheet sheet(storage);
    sheet.show();
    QVERIFY(listed.wait(10000));
    auto *remotes = child<QListWidget>(sheet, QStringLiteral("remoteList"));
    QCOMPARE(remotes->count(), 1);
    QVERIFY(remotes->item(0)->text().contains(QLatin1String("Google Drive")));

    child<QPushButton>(sheet, QStringLiteral("connectButton"))->click();
    QCOMPARE(sheet.page(), CloudStorageSheet::providersPage);
    auto *providers = child<QListWidget>(sheet, QStringLiteral("providerList"));
    QStringList names;
    for (int row = 0; row < providers->count(); ++row)
        names << providers->item(row)->text();
    for (const char *name : {"Google Drive", "Dropbox", "OneDrive", "iCloud Drive", "Box", "Proton Drive", "Mega", "Amazon S3 and compatible",
                             "Backblaze B2", "Nextcloud or WebDAV", "SFTP", "Other (any rclone backend)"})
        QVERIFY2(names.contains(QString::fromUtf8(name)), name);
    // Dropbox signs in through the browser: nothing to type but a name, already filled in.
    providers->setCurrentRow(int(names.indexOf(QStringLiteral("Dropbox"))));
    child<QPushButton>(sheet, QStringLiteral("nextButton"))->click();
    QCOMPARE(sheet.page(), CloudStorageSheet::formPage);
    QCOMPARE(child<QLineEdit>(sheet, QStringLiteral("remoteName"))->text(), QString("dropbox"));
    child<QPushButton>(sheet, QStringLiteral("connectRemote"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 10000);
    QCOMPARE(sheet.page(), CloudStorageSheet::remotesPage);
    QVERIFY(m_cloud->config().contains("\"dropbox\""));
    QVERIFY(m_cloud->calls().contains("config update dropbox --continue --non-interactive --state *oauth-islocal --result true"));

    // The name is taken now: the next Dropbox gets another.
    child<QPushButton>(sheet, QStringLiteral("connectButton"))->click();
    providers->setCurrentRow(int(names.indexOf(QStringLiteral("Dropbox"))));
    child<QPushButton>(sheet, QStringLiteral("nextButton"))->click();
    QCOMPARE(child<QLineEdit>(sheet, QStringLiteral("remoteName"))->text(), QString("dropbox-2"));
    child<QPushButton>(sheet, QStringLiteral("formBack"))->click();
    child<QPushButton>(sheet, QStringLiteral("backButton"))->click();

    // A form service: the password goes to rclone, then leaves the field.
    child<QPushButton>(sheet, QStringLiteral("connectButton"))->click();
    providers->setCurrentRow(int(names.indexOf(QStringLiteral("Nextcloud or WebDAV"))));
    child<QPushButton>(sheet, QStringLiteral("nextButton"))->click();
    child<QPushButton>(sheet, QStringLiteral("connectRemote"))->click();
    QVERIFY(child<QLabel>(sheet, QStringLiteral("connectError"))->isVisible());
    child<QLineEdit>(sheet, QStringLiteral("field_url"))->setText(QStringLiteral("https://cloud.example.com/remote.php/dav/files/me"));
    child<QLineEdit>(sheet, QStringLiteral("field_user"))->setText(QStringLiteral("me"));
    auto *password = child<QLineEdit>(sheet, QStringLiteral("field_pass"));
    QCOMPARE(password->echoMode(), QLineEdit::Password);
    password->setText(QStringLiteral("hunter2"));
    child<QPushButton>(sheet, QStringLiteral("connectRemote"))->click();
    QVERIFY(password->text().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 3, 10000);
    QVERIFY(m_cloud->calls().contains("--obscure"));

    for (int row = 0; row < remotes->count(); ++row) {
        if (remotes->item(row)->text().startsWith(QLatin1String("dropbox")))
            remotes->setCurrentRow(row);
    }
    child<QPushButton>(sheet, QStringLiteral("disconnectButton"))->click();
    QMessageBox *asked = sheet.findChild<QMessageBox *>(QStringLiteral("disconnectAlert"));
    QVERIFY(asked);
    click(*asked, QStringLiteral("Disconnect"));
    QTRY_COMPARE_WITH_TIMEOUT(remotes->count(), 2, 10000);
    QVERIFY(!m_cloud->config().contains("\"dropbox\""));
    QVERIFY(!allText(sheet).contains(QLatin1String(FakeCloud::secret)));
    QVERIFY(!allText(sheet).contains(QLatin1String("hunter2")));
    QVERIFY(!readFile(QSettings().fileName()).contains("hunter2"));
    QVERIFY(!readFile(QSettings().fileName()).contains(FakeCloud::secret));
}

void CloudWorkspaceTests::aServiceThatAsksForACode()
{
    CloudStorage storage;
    QSignalSpy listed(&storage, &CloudStorage::remotesChanged);
    CloudStorageSheet sheet(storage);
    sheet.show();
    QVERIFY(listed.wait(10000));
    child<QPushButton>(sheet, QStringLiteral("connectButton"))->click();
    auto *providers = child<QListWidget>(sheet, QStringLiteral("providerList"));
    providers->setCurrentItem(providers->findItems(QStringLiteral("iCloud Drive"), Qt::MatchExactly).value(0));
    child<QPushButton>(sheet, QStringLiteral("nextButton"))->click();
    child<QLineEdit>(sheet, QStringLiteral("field_apple_id"))->setText(QStringLiteral("someone@example.com"));
    child<QLineEdit>(sheet, QStringLiteral("field_password"))->setText(QStringLiteral("hunter2"));
    child<QPushButton>(sheet, QStringLiteral("connectRemote"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(sheet.page(), CloudStorageSheet::questionPage, 10000);
    QVERIFY(child<QLabel>(sheet, QStringLiteral("questionHelp"))->text().contains(QLatin1String("2FA")));
    auto *answer = child<QLineEdit>(sheet, QStringLiteral("questionAnswer"));
    answer->setText(QStringLiteral("000000"));
    child<QPushButton>(sheet, QStringLiteral("questionContinue"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(sheet.page() == CloudStorageSheet::questionPage && child<QLabel>(sheet, QStringLiteral("questionError"))->isVisible(), 10000);
    QCOMPARE(child<QLabel>(sheet, QStringLiteral("questionError"))->text(), QString("Incorrect code"));
    answer->setText(QStringLiteral("123456"));
    child<QPushButton>(sheet, QStringLiteral("questionContinue"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(sheet.page(), CloudStorageSheet::remotesPage, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(child<QListWidget>(sheet, QStringLiteral("remoteList"))->count(), 2, 10000);

    // Cancelling halfway leaves nothing behind in rclone's config.
    child<QPushButton>(sheet, QStringLiteral("connectButton"))->click();
    providers->setCurrentItem(providers->findItems(QStringLiteral("iCloud Drive"), Qt::MatchExactly).value(0));
    child<QPushButton>(sheet, QStringLiteral("nextButton"))->click();
    child<QLineEdit>(sheet, QStringLiteral("field_apple_id"))->setText(QStringLiteral("other@example.com"));
    child<QLineEdit>(sheet, QStringLiteral("field_password"))->setText(QStringLiteral("hunter2"));
    child<QLineEdit>(sheet, QStringLiteral("remoteName"))->setText(QStringLiteral("halfway"));
    child<QPushButton>(sheet, QStringLiteral("connectRemote"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(sheet.page(), CloudStorageSheet::questionPage, 10000);
    QVERIFY(m_cloud->config().contains("\"halfway\""));
    child<QPushButton>(sheet, QStringLiteral("questionCancel"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!m_cloud->config().contains("\"halfway\""), 10000);
    QVERIFY(!allText(sheet).contains(QLatin1String("hunter2")));
}

void CloudWorkspaceTests::missingRcloneOffersToInstallIt()
{
    const QString fake = qEnvironmentVariable("OMASTRATOR_RCLONE");
    qputenv("OMASTRATOR_RCLONE", (m_cloud->root() + QStringLiteral("/no-rclone")).toUtf8());
    // A stand-in terminal that writes down what it was asked to run.
    const QString ran = m_cloud->root() + QStringLiteral("/terminal-ran");
    const QString terminal = m_cloud->root() + QStringLiteral("/terminal");
    {
        QFile script(terminal);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\necho \"$@\" > '" + ran.toUtf8() + "'\n");
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
    }
    qputenv("OMASTRATOR_TERMINAL", terminal.toUtf8());
    qputenv("OMASTRATOR_OMARCHY", "omarchy");

    CloudStorage storage;
    CloudStorageSheet sheet(storage);
    sheet.show();
    QCOMPARE(sheet.page(), CloudStorageSheet::missingPage);
    QCOMPARE(child<QLabel>(sheet, QStringLiteral("missingRclone"))->text(), QString("rclone isn't installed. Omastrator uses it to reach cloud storage."));
    child<QPushButton>(sheet, QStringLiteral("installRclone"))->click();
    QTRY_COMPARE_WITH_TIMEOUT(readFile(ran).trimmed(), QByteArray("omarchy pkg add rclone"), 10000);
    QVERIFY(!child<QLabel>(sheet, QStringLiteral("installError"))->isVisible());

    // Installed while the sheet waits: it moves on by itself.
    qputenv("OMASTRATOR_RCLONE", fake.toUtf8());
    QTRY_COMPARE_WITH_TIMEOUT(sheet.page(), CloudStorageSheet::remotesPage, 10000);
    qunsetenv("OMASTRATOR_TERMINAL");
    qunsetenv("OMASTRATOR_OMARCHY");

    // Without rclone, Open is the usual dialog and nothing else changes.
    qputenv("OMASTRATOR_RCLONE", (m_cloud->root() + QStringLiteral("/no-rclone")).toUtf8());
    ProjectWorkspace workspace;
    workspace.cloud().refreshRemotes();
    workspace.open();
    QVERIFY(!shown(QStringLiteral("cloudBrowser")));
    qputenv("OMASTRATOR_RCLONE", fake.toUtf8());
}

void CloudWorkspaceTests::openEditSaveUploads()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    m_cloud->put(QStringLiteral("work"), QStringLiteral("Designs/notes.txt"), "not a document");
    ProjectWorkspace workspace;
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    ProjectTab &tab = workspace.current();
    QCOMPARE(tab.cloud->location.toString(), QString("work:Designs/logo.omai"));
    QCOMPARE(tab.title(), QString("logo"));
    QCOMPARE(tab.place(), QString("work:Designs/logo.omai"));
    QCOMPARE(*tab.path, m_cloud->cacheRoot() + QStringLiteral("/work/Designs/logo.omai"));
    QVERIFY(!tab.session.isModified());
    const size_t base = tab.session.document()->objects.size();

    tab.session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    QVERIFY(tab.session.isModified());
    bool saved = false;
    workspace.save(tab.id, false, [&saved](bool done) { saved = done; });
    QTRY_VERIFY(saved);
    // The save is local at once; the upload follows.
    QVERIFY(!tab.session.isModified());
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    QCOMPARE(m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai")), readFile(*tab.path));
    QCOMPARE(ProjectStore::read(*tab.path).objects.size(), base + 1);
    QCOMPARE(tab.cloudStatus, QString("Saved to Google Drive"));

    // A second save goes against the new version, with no conflict.
    tab.session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Dot"));
    workspace.save(tab.id);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    QCOMPARE(ProjectStore::read(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"))).objects.size(), base + 2);
    QVERIFY(!shown(QStringLiteral("cloudConflictAlert")));
}

void CloudWorkspaceTests::saveAsAndExportGoToARemote()
{
    ProjectWorkspace workspace;
    QSignalSpy listed(&workspace.cloud(), &CloudStorage::remotesChanged);
    workspace.cloud().refreshRemotes();
    QVERIFY(listed.wait(10000));
    workspace.createDocument(QSizeF(300, 200));
    const size_t base = workspace.current().session.document()->objects.size();
    workspace.current().session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    bool saved = false;
    workspace.save(workspace.current().id, true, [&saved](bool done) { saved = done; });
    CloudBrowser *browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    browser->showRemote(QStringLiteral("work"));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading(), 10000);
    auto *name = child<QLineEdit>(*browser, QStringLiteral("fileName"));
    QCOMPARE(name->text(), QString("Untitled.omai"));
    name->setText(QStringLiteral("poster"));
    child<QPushButton>(*browser, QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY(saved);
    QCOMPARE(workspace.current().cloud->location.toString(), QString("work:poster.omai"));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(workspace.current().id.toString()), 10000);
    QCOMPARE(ProjectStore::read(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("poster.omai"))).objects.size(), base + 1);

    // Export remembers the remote it was last in.
    workspace.exportAs(DocumentExporter::Format::svg);
    browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    QCOMPARE(browser->remoteShown(), QString("work"));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading(), 10000);
    QCOMPARE(child<QLineEdit>(*browser, QStringLiteral("fileName"))->text(), QString("poster.svg"));
    child<QPushButton>(*browser, QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(m_cloud->read(QStringLiteral("work"), QStringLiteral("poster.svg")).contains("<svg"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.uploads().pendingKeys().isEmpty(), 10000);
    QVERIFY(workspace.cloudStatusText().contains(QStringLiteral("Exported “poster.svg” to Google Drive")));

    // This Computer hands over to the usual dialog.
    workspace.save(workspace.current().id, true);
    browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QSignalSpy local(browser, &CloudBrowser::chooseLocal);
    browser->findChild<QListWidget *>(QStringLiteral("locations"))->setCurrentRow(0);
    child<QPushButton>(*browser, QStringLiteral("browseLocal"))->click();
    QCOMPARE(local.size(), 1);
}

void CloudWorkspaceTests::placeDownloadsFirst()
{
    m_cloud->put(QStringLiteral("work"), QStringLiteral("art/mark.svg"),
                 "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='10' height='10' fill='#f00'/></svg>");
    ProjectWorkspace workspace;
    QSignalSpy listed(&workspace.cloud(), &CloudStorage::remotesChanged);
    workspace.cloud().refreshRemotes();
    QVERIFY(listed.wait(10000));
    workspace.createDocument(QSizeF(100, 100));
    const int base = int(workspace.current().session.document()->objects.size());
    workspace.place();
    CloudBrowser *browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    browser->navigate(CloudLocation{QStringLiteral("work"), QStringLiteral("art")});
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading() && entry(*browser, QStringLiteral("mark.svg")), 10000);
    browser->findChild<QListWidget *>(QStringLiteral("entries"))->setCurrentItem(entry(*browser, QStringLiteral("mark.svg")));
    child<QPushButton>(*browser, QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(int(workspace.current().session.document()->objects.size()) > base, 10000);
}

void CloudWorkspaceTests::keepBothUploadsYoursAsACopy()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    ProjectWorkspace workspace;
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    ProjectTab &tab = workspace.current();
    const size_t base = tab.session.document()->objects.size();
    m_cloud->put(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"), "their version, saved elsewhere");
    tab.session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    workspace.save(tab.id);
    QTRY_VERIFY_WITH_TIMEOUT(shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), 10000);
    QCOMPARE(workspace.uploadStatus(tab.id).phase, CloudUploader::Phase::conflict);
    QCOMPARE(workspace.cloudStatusText(), QString("Not uploaded: changed on Google Drive"));
    click(*shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), QStringLiteral("Keep Both"));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    // Theirs is untouched; yours sits beside it, and the tab follows yours.
    QCOMPARE(m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai")), QByteArray("their version, saved elsewhere"));
    const QStringList beside = QDir(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Designs"))).entryList({QStringLiteral("logo (conflict *).omai")});
    QCOMPARE(beside.size(), 1);
    QCOMPARE(tab.cloud->location.toString(), QStringLiteral("work:Designs/") + beside.first());
    QCOMPARE(ProjectStore::read(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Designs/") + beside.first())).objects.size(), base + 1);
    // The next save goes to the copy, with no conflict.
    tab.session.addPath(Shapes::rectangle(QRectF(0, 0, 5, 5)), QStringLiteral("Dot"));
    workspace.save(tab.id);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    QCOMPARE(ProjectStore::read(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Designs/") + beside.first())).objects.size(), base + 2);
    QVERIFY(!shown(QStringLiteral("cloudConflictAlert")));
}

void CloudWorkspaceTests::overwriteReplacesTheirs()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    ProjectWorkspace workspace;
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    ProjectTab &tab = workspace.current();
    m_cloud->put(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"), "their version, saved elsewhere");
    tab.session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    workspace.save(tab.id);
    QTRY_VERIFY_WITH_TIMEOUT(shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), 10000);
    click(*shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), QStringLiteral("Overwrite"));
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    QCOMPARE(m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai")), readFile(*tab.path));
    QCOMPARE(tab.cloud->location.toString(), QString("work:Designs/logo.omai"));
}

void CloudWorkspaceTests::openTheirsKeepsYoursUnsaved()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    ProjectWorkspace workspace;
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    const QUuid yoursID = workspace.current().id;
    std::shared_ptr<ProjectTab> yours = workspace.tab(yoursID);
    const size_t base = yours->session.document()->objects.size();
    // Their version: a document with two shapes.
    {
        ProjectWorkspace other;
        other.createDocument(QSizeF(200, 100));
        QCOMPARE(other.current().session.document()->objects.size(), base);
        other.current().session.addPath(Shapes::rectangle(QRectF(0, 0, 5, 5)), QStringLiteral("A"));
        other.current().session.addPath(Shapes::rectangle(QRectF(9, 9, 5, 5)), QStringLiteral("B"));
        const QString theirs = m_cloud->root() + QStringLiteral("/theirs.omai");
        QVERIFY(other.saveTo(other.current(), theirs));
        m_cloud->put(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"), readFile(theirs));
    }
    yours->session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    workspace.save(yoursID);
    QTRY_VERIFY_WITH_TIMEOUT(shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), 10000);
    click(*shown<QMessageBox>(QStringLiteral("cloudConflictAlert")), QStringLiteral("Open Theirs"));
    QTRY_COMPARE_WITH_TIMEOUT(int(workspace.tabs().size()), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.current().id != yoursID && workspace.current().cloud.has_value(), 10000);
    QCOMPARE(workspace.current().session.document()->objects.size(), base + 2);
    QCOMPARE(workspace.current().cloud->location.toString(), QString("work:Designs/logo.omai"));
    // Yours: still open, unsaved, no longer tied to the file.
    QVERIFY(!yours->cloud);
    QVERIFY(!yours->path);
    QVERIFY(yours->session.isModified());
    QCOMPARE(yours->title(), QString("logo (yours)"));
    QCOMPARE(yours->session.document()->objects.size(), base + 1);
    QVERIFY(!workspace.uploads().isPending(yoursID.toString()));
}

void CloudWorkspaceTests::offlineSavesUploadLater()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    ProjectWorkspace workspace;
    workspace.uploads().setRetryDelays(100, 400);
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    ProjectTab &tab = workspace.current();
    const QByteArray before = m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    FakeCloud::failing("offline");
    tab.session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    bool saved = false;
    workspace.save(tab.id, false, [&saved](bool done) { saved = done; });
    QTRY_VERIFY(saved);
    QVERIFY(!tab.session.isModified());
    QTRY_COMPARE_WITH_TIMEOUT(workspace.uploadStatus(tab.id).phase, CloudUploader::Phase::waiting, 10000);
    QVERIFY(workspace.cloudStatusText().startsWith(QStringLiteral("Upload failed — retrying in")));
    QVERIFY(workspace.uploadStatus(tab.id).error.contains(QLatin1String("no such host")));
    QTRY_VERIFY_WITH_TIMEOUT(workspace.uploadStatus(tab.id).attempts >= 3, 10000);
    QCOMPARE(m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai")), before);
    // Back online: the next try goes through, against the version that was opened.
    FakeCloud::failing(nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.uploads().isPending(tab.id.toString()), 10000);
    QCOMPARE(m_cloud->read(QStringLiteral("work"), QStringLiteral("Designs/logo.omai")), readFile(*tab.path));
    QCOMPARE(tab.cloudStatus, QString("Saved to Google Drive"));
}

void CloudWorkspaceTests::closingWithAnUploadPendingAsks()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    ProjectWorkspace workspace;
    workspace.uploads().setRetryDelays(60000, 60000);
    openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    const QUuid id = workspace.current().id;
    FakeCloud::failing("offline");
    workspace.current().session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    workspace.save(id);
    QTRY_COMPARE_WITH_TIMEOUT(workspace.uploadStatus(id).phase, CloudUploader::Phase::waiting, 10000);

    workspace.close(id);
    QTRY_VERIFY(shown<QMessageBox>(QStringLiteral("uploadPendingAlert")));
    QVERIFY(shown<QMessageBox>(QStringLiteral("uploadPendingAlert"))->text().contains(QStringLiteral("isn’t uploaded to Google Drive yet")));
    click(*shown<QMessageBox>(QStringLiteral("uploadPendingAlert")), QStringLiteral("Keep Open"));
    QTest::qWait(50);
    QVERIFY(workspace.tab(id));
    QVERIFY(workspace.uploads().isPending(id.toString()));

    workspace.close(id);
    QTRY_VERIFY(shown<QMessageBox>(QStringLiteral("uploadPendingAlert")));
    click(*shown<QMessageBox>(QStringLiteral("uploadPendingAlert")), QStringLiteral("Close Anyway"));
    QTRY_VERIFY(!workspace.tab(id));
    QVERIFY(!workspace.uploads().isPending(id.toString()));
    FakeCloud::failing(nullptr);
}

void CloudWorkspaceTests::recentFilesRememberTheService()
{
    putDocument(QStringLiteral("work"), QStringLiteral("Designs/logo.omai"));
    {
        ProjectWorkspace workspace;
        openThroughBrowser(workspace, QStringLiteral("Designs"), QStringLiteral("logo.omai"));
    }
    const QStringList recent = ProjectWorkspace::recentFiles();
    QCOMPARE(recent.value(0), QString("work:Designs/logo.omai"));
    // The cache copy is never listed as a file of its own.
    QVERIFY(!recent.join(QLatin1Char('\n')).contains(m_cloud->cacheRoot()));
    QCOMPARE(ProjectWorkspace::recentLabel(recent.first()), QString("logo.omai — Google Drive"));
    QVERIFY(!ProjectWorkspace::recentIcon(recent.first()).isNull());
    QCOMPARE(ProjectWorkspace::recentLabel(QStringLiteral("/tmp/local.omai")), QString("local.omai"));
    QVERIFY(ProjectWorkspace::recentIcon(QStringLiteral("/tmp/local.omai")).isNull());

    // Open Recent brings it back from the service.
    ProjectWorkspace again;
    QVERIFY(again.openFile(recent.first()));
    QTRY_VERIFY_WITH_TIMEOUT(again.current().cloud.has_value(), 10000);
    QCOMPARE(again.current().cloud->location.toString(), QString("work:Designs/logo.omai"));

    // Offline, the copy from last time still opens, and says so.
    FakeCloud::failing("offline");
    ProjectWorkspace offline;
    QVERIFY(offline.openFile(recent.first()));
    QTRY_VERIFY_WITH_TIMEOUT(offline.current().cloud.has_value(), 10000);
    QVERIFY(offline.current().cloudStatus.contains(QStringLiteral("couldn’t be reached")));
    FakeCloud::failing(nullptr);
    QVERIFY(!readFile(QSettings().fileName()).contains(FakeCloud::secret));
}

QTEST_MAIN(CloudWorkspaceTests)
#include "CloudWorkspaceTests.moc"
