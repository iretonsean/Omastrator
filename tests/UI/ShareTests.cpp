#include "../Agent/FakeAgents.h"
#include "../Cloud/FakeCloud.h"
#include "Agent/AgentLauncher.h"
#include "Document/PathOperations.h"
#include "Live/Deploy.h"
#include "Live/WriteBack.h"
#include "UI/CommandPalette.h"
#include "UI/ContextMenus.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SharePanels.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// Share with client (docs/SHARE.md): one press shares the artboard or the selection and
// copies the link, through a cloud service (the fake rclone), GitHub (a fake gh) or a Live
// preview deploy (a fake deploy command, a local bare remote). Nothing here touches the
// user's rclone config, GitHub account or clipboard history beyond this process.
namespace {
QStringList logged;

void keepLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    logged << message;
}

// Stands in for gh: logs each call; gists and releases are files in $FAKE_GH_DIR.
constexpr const char *fakeGh = R"sh(#!/bin/sh
echo "$@" >> "$FAKE_GH_DIR/calls.log"
case "$1 $2" in
"auth status")
  if [ -f "$FAKE_GH_DIR/logged-in" ]; then echo '  Logged in to github.com account tester (keyring)'; exit 0; fi
  echo 'You are not logged into any GitHub hosts.' >&2; exit 1;;
"gist create")
  cp "$3" "$FAKE_GH_DIR/gist-$(basename "$3")"; echo '- Creating gist'; echo 'Created secret gist'; echo 'https://gist.github.com/tester/0a1b2c3d4e'; exit 0;;
"gist delete") exit 0;;
"repo view")
  if [ -f "$FAKE_GH_DIR/repo" ]; then echo tester/omastrator-shares; exit 0; fi
  echo 'GraphQL: Could not resolve to a Repository with the name tester/omastrator-shares.' >&2; exit 1;;
"repo create")
  touch "$FAKE_GH_DIR/repo"; echo 'Created repository tester/omastrator-shares on GitHub'; echo '  https://github.com/tester/omastrator-shares'; exit 0;;
"release create")
  cp "$4" "$FAKE_GH_DIR/release-$(basename "$4")"; echo "https://github.com/tester/omastrator-shares/releases/tag/$3"; exit 0;;
"release delete") exit 0;;
esac
exit 2
)sh";

// Stands in for `omarchy`: the default agent is $FAKE_AGENT, and a prompt is recorded.
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT/prompt\"; exit 0; fi\n"
                                    "exit 2\n";

void writeFile(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The files in a fake remote's folder, recursively, relative to it.
QStringList filesIn(const QString &folder)
{
    QStringList found;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        found << QDir(folder).relativeFilePath(it.next());
    return found;
}

QString clipboard()
{
    return QGuiApplication::clipboard()->text();
}

struct Window {
    ProjectWorkspace workspace;
    ProjectWorkspaceView view{workspace};

    explicit Window(bool document = true)
    {
        view.show();
        if (document)
            workspace.createDocument(QSizeF(200, 100));
    }
    EditorSession &session() { return workspace.current().session; }
    ShareController &share() { return *view.share(); }
    QUuid box(QRectF rect) { return session().addPath(Shapes::rectangle(rect), QStringLiteral("Box")); }
    template <typename T = QWidget> T *find(const QString &name) { return view.findChild<T *>(name); }
    bool waitShared()
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 20'000 && (share().running() || share().notice().kind == ShareController::Notice::Kind::progress))
            QTest::qWait(20);
        return share().notice().kind == ShareController::Notice::Kind::shared;
    }
};
}

class ShareTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    std::unique_ptr<FakeCloud> m_cloud;

    QString ghDir() const { return m_directory.filePath(QStringLiteral("gh")); }
    QString ghCalls() const { return QString::fromUtf8(readFile(ghDir() + QStringLiteral("/calls.log"))); }
    void logInToGitHub(bool in)
    {
        if (in)
            writeFile(ghDir() + QStringLiteral("/logged-in"), "");
        else
            QFile::remove(ghDir() + QStringLiteral("/logged-in"));
    }
    void useGitHub(bool installed)
    {
        qputenv("OMASTRATOR_GH", installed ? m_directory.filePath(QStringLiteral("bin-gh/gh")).toUtf8() : QByteArray("/nonexistent/gh"));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qInstallMessageHandler(keepLog);
        QVERIFY(m_directory.isValid());
        writeFile(m_directory.filePath(QStringLiteral("bin-gh/gh")), fakeGh, true);
        writeFile(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("FAKE_OUT", m_directory.path().toUtf8());
        qputenv("FAKE_GH_DIR", ghDir().toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        // Open writes what it was given here instead of starting a browser.
        writeFile(m_directory.filePath(QStringLiteral("open")), "#!/bin/sh\nprintf '%s' \"$1\" > \"$FAKE_OUT/opened\"\n", true);
        qputenv("OMASTRATOR_XDG_OPEN", m_directory.filePath(QStringLiteral("open")).toUtf8());
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        writeFile(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
    }

    void init()
    {
        QSettings().clear();
        // Each test has its own rclone, config folder (so its own Shared list) and gh state.
        m_cloud = std::make_unique<FakeCloud>();
        QDir(ghDir()).removeRecursively();
        QDir().mkpath(ghDir());
        useGitHub(false);
        qputenv("FAKE_AGENT", "sh");
        AgentLauncher::setShowTerminal(false);
        QGuiApplication::clipboard()->clear();
        logged.clear();
    }

    void cleanup()
    {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (qobject_cast<QDialog *>(widget) || widget->windowFlags().testFlag(Qt::Popup))
                delete widget;
        }
        QTest::qWait(20);
        m_cloud.reset();
        // Nothing secret, and no share link, is ever logged.
        const QString all = logged.join(QLatin1Char('\n'));
        QVERIFY(!all.contains(QLatin1String(FakeCloud::secret)));
        QVERIFY(!all.contains(QLatin1String("share.example.test")));
        QVERIFY(!all.contains(QLatin1String("gist.github.com")));
    }

    void onePressSharesTheArtboardToACloudServiceAndCopiesTheLink()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
        Window w;
        w.box({10, 10, 40, 20});
        w.session().deselectAll();
        QCOMPARE(w.share().scopeText(), QStringLiteral("the artboard"));
        // The toolbar button, at the top right, says what it will share.
        auto *button = w.find<QToolButton>(QStringLiteral("shareToolbar"));
        QVERIFY(button && button->isEnabled());
        QVERIFY(button->toolTip().startsWith(QLatin1String("Share the artboard")));
        button->click();
        QVERIFY(w.waitShared());

        QVERIFY(clipboard().startsWith(QLatin1String("https://share.example.test/work/")));
        auto *toast = w.find<QFrame>(QStringLiteral("shareToast"));
        QVERIFY(toast && toast->isVisible());
        QCOMPARE(w.find<QLabel>(QStringLiteral("shareToastText"))->text(), QStringLiteral("Link copied — Google Drive"));
        QCOMPARE(w.find<QLabel>(QStringLiteral("shareToastDetail"))->text(), QStringLiteral("The artboard as PNG at 2×."));
        // Open and Copy Again.
        QGuiApplication::clipboard()->clear();
        w.find<QPushButton>(QStringLiteral("shareToastCopy"))->click();
        QVERIFY(clipboard().startsWith(QLatin1String("https://share.example.test/work/")));
        w.find<QPushButton>(QStringLiteral("shareToastOpen"))->click();
        QTRY_COMPARE(QString::fromUtf8(readFile(m_directory.filePath(QStringLiteral("opened")))), clipboard());

        // Omastrator Shares/<document>/<time>.png, at twice the artboard's size.
        const QStringList files = filesIn(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Omastrator Shares")));
        QCOMPARE(files.size(), 1);
        QVERIFY(files.front().startsWith(QLatin1String("Untitled/") + QDate::currentDate().toString(Qt::ISODate)));
        QVERIFY(files.front().endsWith(QLatin1String(".png")));
        const QImage image(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Omastrator Shares/") + files.front()));
        QCOMPARE(image.size(), QSize(400, 200));
        QVERIFY(m_cloud->calls().contains("link work:Omastrator Shares/Untitled/"));

        // The Shared list is kept outside the document, and holds no credential.
        const std::vector<Share::Record> list = w.share().sharedList();
        QCOMPARE(list.size(), size_t(1));
        QCOMPARE(list.front().kind, QStringLiteral("cloud:work"));
        QCOMPARE(list.front().scope, QStringLiteral("artboard"));
        QCOMPARE(list.front().link, clipboard());
        QVERIFY(!readFile(Share::storePath()).contains(FakeCloud::secret));
        QVERIFY(!clipboard().contains(QLatin1String(FakeCloud::secret)));
    }

    void theSelectionIsSharedAloneAndTheToastSaysSo()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("dropbox"));
        Window w;
        const QUuid kept = w.box({10, 10, 40, 20});
        w.box({120, 50, 30, 30});
        w.session().select({kept});
        QCOMPARE(w.share().scopeText(), QStringLiteral("the selection (1 object)"));
        QVERIFY(w.find<QToolButton>(QStringLiteral("shareToolbar"))->toolTip().startsWith(QLatin1String("Share the selection (1 object)")));
        QCOMPARE(w.share().share({Share::Format::svg, QString()}), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — Dropbox"));
        QCOMPARE(w.share().notice().detail, QStringLiteral("The selection (1 object) as SVG."));
        const QStringList files = filesIn(m_cloud->remoteFile(QStringLiteral("work"), QStringLiteral("Omastrator Shares")));
        QCOMPARE(files.size(), 1);
        const QByteArray svg = m_cloud->read(QStringLiteral("work"), QStringLiteral("Omastrator Shares/") + files.front());
        // One box, cropped to its bounds (the stroke included), on clear paper.
        QCOMPARE(svg.count("<path"), 1);
        QVERIFY(!svg.contains("<rect"));
        const QRectF bounds = w.session().document()->bounds(std::vector<QUuid>{kept}, true);
        QVERIFY(svg.contains(QStringLiteral("viewBox=\"0 0 %1 %2\"").arg(bounds.width()).arg(bounds.height()).toUtf8()));
        const Share::Record record = w.share().sharedList().front();
        QCOMPARE(record.scope, QStringLiteral("selection"));
        QCOMPARE(record.objects, QStringList{kept.toString(QUuid::WithoutBraces)});

        // The canvas's right-click menu, which the task bar's overflow is, names it.
        QMenu *menu = ContextMenus::forCanvas(*w.view.menus(), w.session(), w.view.content()->canvas(), {}, &w.view);
        QAction *entry = menu->findChild<QAction *>(QStringLiteral("shareSelection"));
        QVERIFY(entry);
        QCOMPARE(entry->text(), QStringLiteral("Share Selection"));
        delete menu;
    }

    void formatsAndTheDestinationAreRememberedPerDocument()
    {
        m_cloud->addRemote(QStringLiteral("a"), QStringLiteral("dropbox"));
        m_cloud->addRemote(QStringLiteral("b"), QStringLiteral("drive"));
        m_cloud->addRemote(QStringLiteral("c"), QStringLiteral("sftp"));
        Window w;
        w.box({10, 10, 40, 20});
        w.session().deselectAll();
        // The first press lists rclone's remotes, then takes the first that makes links.
        QCOMPARE(w.share().share(), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(filesIn(m_cloud->remoteFile(QStringLiteral("a"), QString())).size(), 1);
        QStringList ids;
        for (const auto &each : w.share().destinations())
            ids << each.id;
        // SFTP makes no links, and there's no gh.
        QCOMPARE(ids, (QStringList{"cloud:a", "cloud:b"}));

        // The popover's choices stick to the document.
        SharePopover *popover = SharePanels::showOptions(w.share(), w.view);
        auto *format = popover->findChild<QComboBox *>(QStringLiteral("shareFormat"));
        auto *destination = popover->findChild<QComboBox *>(QStringLiteral("shareDestination"));
        QCOMPARE(format->currentText(), QStringLiteral("PNG at 2×"));
        QCOMPARE(destination->currentText(), QStringLiteral("Dropbox"));
        QCOMPARE(popover->findChild<QLabel *>(QStringLiteral("shareScope"))->text(), QStringLiteral("Shares the artboard."));
        QVERIFY(popover->findChild<QLabel *>(QStringLiteral("shareNote"))->text().contains(QLatin1String("Anyone with the link")));
        format->setCurrentIndex(format->findData(QStringLiteral("pdf")));
        emit format->activated(format->currentIndex());
        destination->setCurrentIndex(destination->findData(QStringLiteral("cloud:b")));
        emit destination->activated(destination->currentIndex());
        QCOMPARE(w.share().chosenFormat(), Share::Format::pdf);
        QCOMPARE(w.share().chosenDestination(), QStringLiteral("cloud:b"));
        popover->findChild<QPushButton *>(QStringLiteral("shareGo"))->click();
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — Google Drive"));

        // A plain press now goes to Google Drive as PDF.
        QCOMPARE(w.share().share(), QString());
        QVERIFY(w.waitShared());
        const QStringList onB = filesIn(m_cloud->remoteFile(QStringLiteral("b"), QString()));
        QCOMPARE(onB.size(), 2);
        for (const QString &file : onB)
            QVERIFY(file.endsWith(QLatin1String(".pdf")));
        QCOMPARE(w.share().sharedList().size(), size_t(3));

        // Another document starts from the defaults.
        w.workspace.addTab(false);
        w.workspace.createDocument(QSizeF(50, 50));
        QCOMPARE(w.share().chosenFormat(), Share::Format::png);
        QCOMPARE(w.share().chosenDestination(), QStringLiteral("cloud:a"));
        QVERIFY(w.share().sharedList().empty());
    }

    void withNowhereToGoItSaysWhatToConnect()
    {
        Window w;
        w.box({10, 10, 40, 20});
        QVERIFY(w.share().destinations().empty());
        QCOMPARE(w.share().share(), QString());
        QTRY_COMPARE(w.share().notice().kind, ShareController::Notice::Kind::connect);
        QCOMPARE(w.find<QLabel>(QStringLiteral("shareToastText"))->text(),
                 QStringLiteral("There's nowhere to share to yet. Connect a cloud service or GitHub, then press Share again."));
        QVERIFY(w.find<QPushButton>(QStringLiteral("shareToastConnectCloud"))->isVisible());
        QVERIFY(w.find<QPushButton>(QStringLiteral("shareToastConnectGitHub"))->isVisible());
        QVERIFY(!w.find<QPushButton>(QStringLiteral("shareToastOpen"))->isVisible());
        SharePopover *popover = SharePanels::showOptions(w.share(), w.view);
        QVERIFY(popover->findChild<QWidget *>(QStringLiteral("shareNowhere"))->isVisible());
        QVERIFY(!popover->findChild<QPushButton *>(QStringLiteral("shareGo"))->isEnabled());
    }

    void gitHubAsksOnceThenSharesSvgAsASecretGistAndPicturesAsReleaseAssets()
    {
        useGitHub(true);
        logInToGitHub(true);
        Window w;
        w.box({10, 10, 40, 20});
        w.session().deselectAll();
        QCOMPARE(w.share().chosenDestination(), Share::github);

        // The first upload to GitHub asks, and says honestly who can see it.
        QCOMPARE(w.share().share({Share::Format::svg, QString()}), QString());
        QTRY_VERIFY(w.share().askingGitHub());
        auto *sheet = w.find<QDialog>(QStringLiteral("shareGitHubSheet"));
        QVERIFY(sheet && sheet->isVisible());
        const QString explain = sheet->findChild<QLabel *>(QStringLiteral("shareGitHubExplain"))->text();
        QVERIFY(explain.contains(QLatin1String("secret gist is unlisted but opens for anyone with the link")));
        QVERIFY(explain.contains(QLatin1String("public")));
        // No: nothing is uploaded, and it asks again next time.
        sheet->findChild<QPushButton *>(QStringLiteral("dialogCancel"))->click();
        QVERIFY(!w.share().askingGitHub());
        QVERIFY(!ghCalls().contains(QLatin1String("gist create")));
        QCOMPARE(w.share().share({Share::Format::svg, QString()}), QString());
        sheet = w.find<QDialog>(QStringLiteral("shareGitHubSheet"));
        QVERIFY(sheet);
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — GitHub gist"));
        QCOMPARE(clipboard(), QStringLiteral("https://gist.github.com/tester/0a1b2c3d4e"));
        QVERIFY(ghCalls().contains(QLatin1String("gist create ")));
        QVERIFY(readFile(QDir(ghDir()).entryInfoList({"gist-*.svg"}).value(0).filePath()).contains("<svg"));

        // A PNG: a public omastrator-shares repository is made once, then each share is a release asset. No second question.
        QCOMPARE(w.share().share({Share::Format::png, QString()}), QString());
        QVERIFY(!w.share().askingGitHub());
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — GitHub release"));
        QVERIFY(ghCalls().contains(QLatin1String("repo create omastrator-shares --public --add-readme")));
        const Share::Record release = w.share().sharedList().front();
        QCOMPARE(release.repository, QStringLiteral("tester/omastrator-shares"));
        QVERIFY(release.tag.startsWith(QLatin1String("share-")));
        QVERIFY(clipboard().startsWith(QStringLiteral("https://github.com/tester/omastrator-shares/releases/download/%1/Untitled-").arg(release.tag)));
        QVERIFY(clipboard().endsWith(QLatin1String(".png")));
        QCOMPARE(QDir(ghDir()).entryList({"release-*.png"}).size(), 1);
        QCOMPARE(w.share().share({Share::Format::pdf, QString()}), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(ghCalls().count(QLatin1String("repo create")), 1);
        QVERIFY(clipboard().endsWith(QLatin1String(".pdf")));
    }

    void gitHubSignedOutOffersToConnect()
    {
        useGitHub(true);
        logInToGitHub(false);
        Share::setGithubConfirmed(true);
        Window w;
        w.box({10, 10, 40, 20});
        QCOMPARE(w.share().share(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(w.share().notice().kind, ShareController::Notice::Kind::connect, 10'000);
        QVERIFY(w.share().notice().text.startsWith(QLatin1String("GitHub isn't connected.")));
        QVERIFY(w.find<QPushButton>(QStringLiteral("shareToastConnectGitHub"))->isVisible());
        QVERIFY(!ghCalls().contains(QLatin1String("gist")));
    }

    void failuresSayWhatHappenedFirst()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
        FakeCloud::failing("link");
        Window w;
        w.box({10, 10, 40, 20});
        QCOMPARE(w.share().share(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(w.share().notice().kind, ShareController::Notice::Kind::failed, 10'000);
        QVERIFY(w.share().notice().text.startsWith(QLatin1String("Google Drive didn't make a link: ")));
        QVERIFY(w.share().sharedList().empty());
        QVERIFY(clipboard().isEmpty());
        FakeCloud::failing(nullptr);
    }

    void unshareTakesDownTheFileTheGistAndTheRelease()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
        useGitHub(true);
        logInToGitHub(true);
        Share::setGithubConfirmed(true);
        Window w;
        w.box({10, 10, 40, 20});
        QCOMPARE(w.share().share(), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().share({Share::Format::svg, Share::github}), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().share({Share::Format::png, Share::github}), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(w.share().sharedList().size(), size_t(3));

        // From the Shared popover: Copy, Open, Unshare on each.
        SharedPopover *popover = SharePanels::showShared(w.share(), w.view);
        QCOMPARE(popover->findChildren<QPushButton *>(QStringLiteral("sharedUnshare")).size(), 3);
        const auto buttonFor = [&](const QString &name, const QString &id) -> QPushButton * {
            for (QPushButton *button : popover->findChildren<QPushButton *>(name))
                if (button->property("recordId").toString() == id && button->isVisible())
                    return button;
            return nullptr;
        };
        const std::vector<Share::Record> list = w.share().sharedList();
        QGuiApplication::clipboard()->clear();
        buttonFor(QStringLiteral("sharedCopy"), list[2].id)->click();
        QCOMPARE(clipboard(), list[2].link);

        // The cloud file is deleted.
        QCOMPARE(filesIn(m_cloud->remoteFile(QStringLiteral("work"), QString())).size(), 1);
        buttonFor(QStringLiteral("sharedUnshare"), list[2].id)->click();
        QTRY_VERIFY_WITH_TIMEOUT(!w.share().running(), 10'000);
        QCOMPARE(w.share().notice().text, QStringLiteral("Unshared. The link no longer works."));
        QVERIFY(filesIn(m_cloud->remoteFile(QStringLiteral("work"), QString())).isEmpty());
        QTRY_COMPARE(popover->findChildren<QPushButton *>(QStringLiteral("sharedUnshare")).size(), 2);

        // The release and its tag; then the gist.
        QCOMPARE(w.share().unshare(list[0].id), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!w.share().running(), 10'000);
        QVERIFY(ghCalls().contains(QStringLiteral("release delete %1 --repo tester/omastrator-shares --yes --cleanup-tag").arg(list[0].tag)));
        QCOMPARE(w.share().unshare(list[1].id), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!w.share().running(), 10'000);
        QVERIFY(ghCalls().contains(QLatin1String("gist delete 0a1b2c3d4e --yes")));
        QVERIFY(w.share().sharedList().empty());
        QTRY_VERIFY(popover->findChild<QLabel *>(QStringLiteral("sharedEmpty")));
    }

    void theSharedListFollowsTheDocumentNotTheFile()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
        const QString path = m_directory.filePath(QStringLiteral("docs/logo.omai"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString link;
        {
            Window w;
            w.box({10, 10, 40, 20});
            QCOMPARE(w.share().share(), QString());
            QVERIFY(w.waitShared());
            link = clipboard();
            // Saved for the first time: what it shared while untitled comes along.
            QVERIFY(w.workspace.saveTo(w.workspace.current(), path));
            QCOMPARE(w.share().documentKey(), path);
            QCOMPARE(w.share().sharedList().size(), size_t(1));
        }
        // Not in the .omai.
        QVERIFY(!readFile(path).contains(link.toUtf8()));
        Window again(false);
        QVERIFY(again.workspace.openFile(path));
        const std::vector<Share::Record> list = again.share().sharedList();
        QCOMPARE(list.size(), size_t(1));
        QCOMPARE(list.front().link, link);
        QVERIFY(list.front().time.isValid());
        QCOMPARE(list.front().where, QStringLiteral("Google Drive"));
        QCOMPARE(list.front().format, QStringLiteral("png"));
    }

    void pasteClientFeedbackProposesAnEditOnWhatWasShared()
    {
        m_cloud->addRemote(QStringLiteral("work"), QStringLiteral("drive"));
        Window w;
        const QUuid logo = w.box({10, 10, 40, 20});
        const QUuid other = w.box({120, 50, 30, 30});
        w.session().select({logo});
        QCOMPARE(w.share().share(), QString());
        QVERIFY(w.waitShared());
        w.session().select({other});

        SharedPopover *popover = SharePanels::showShared(w.share(), w.view);
        auto *toggle = popover->findChild<QPushButton *>(QStringLiteral("sharedFeedback"));
        QVERIFY(toggle && toggle->isVisible());
        toggle->click();
        auto *field = popover->findChild<QPlainTextEdit *>(QStringLiteral("sharedFeedbackText"));
        QVERIFY(field->isVisible());
        popover->findChild<QPushButton *>(QStringLiteral("sharedFeedbackApply"))->click();
        QCOMPARE(popover->findChild<QLabel *>(QStringLiteral("sharedMessage"))->text(), QStringLiteral("Paste what the client said first."));
        field->setPlainText(QStringLiteral("Love it. Can the logo be bigger and teal?"));
        popover->findChild<QPushButton *>(QStringLiteral("sharedFeedbackApply"))->click();

        // Edit with Instruction, on the shared selection, with the feedback as the instruction.
        QCOMPARE(w.session().selection(), std::vector<QUuid>{logo});
        const QString prompt = FakeAgents::read(m_directory.filePath(QStringLiteral("prompt")));
        QVERIFY(prompt.contains(QLatin1String("Love it. Can the logo be bigger and teal?")));
        QVERIFY(prompt.contains(QLatin1String("selection_get")));
        AgentBridge &bridge = *w.view.agent();
        QVERIFY(bridge.waiting() && bridge.waiting()->task == AgentBridge::Task::edit);
        // The agent answers with a proposal to keep or discard.
        bridge.tools().call(QStringLiteral("set_style"), {{"fill", "#008080"}});
        bridge.tools().call(QStringLiteral("proposal_finish"), {{"title", "Client feedback"}});
        QVERIFY(bridge.hasProposalIn(w.session()));
        bridge.discardProposal();
        QVERIFY(!bridge.hasProposalIn(w.session()));
    }

    void liveSharesTheLatestDeployOrAPreviewDeploy()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git is needed for Live.");
        const QString site = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site");
        writeFile(site + "/index.html", "<h1>Hello</h1>\n");
        writeFile(site + "/omastrator.json",
                  R"({"deploy": {"command": "echo 'Production: https://prod.example.test/'"}, "preview": {"command": "echo 'Preview: https://preview.example.test/abc'"}})");
        WriteBack::git(site, {"init", "-q", "-b", "main"});
        WriteBack::git(site, {"add", "-A"});
        WriteBack::git(site, {"commit", "-q", "-m", "First"});
        QProcess::execute(QStringLiteral("git"), {"init", "-q", "--bare", site + ".git"});
        WriteBack::git(site, {"remote", "add", "origin", site + ".git"});
        WriteBack::git(site, {"push", "-q", "-u", "origin", "main"});
        const QString remoteHead = WriteBack::git(site + ".git", {"rev-parse", "main"}).trimmed();

        Window w(false);
        AgentBridge &bridge = *w.view.agent();
        QVERIFY(w.share().liveProject().isEmpty());
        // A production deploy through Live's own Deploy, to the local bare remote.
        QCOMPARE(bridge.liveDeploy({true, true, true, std::nullopt, site}), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.deployState().running, 20'000);
        QCOMPARE(bridge.deployState().stage, QStringLiteral("done"));
        QCOMPARE(w.share().liveProject(), site);
        QCOMPARE(w.share().chosenDestination(), Share::live);

        // Nothing changed since: Share copies the latest deploy's URL, with no deploy.
        QCOMPARE(w.share().share(), QString());
        QVERIFY(w.waitShared());
        QCOMPARE(clipboard(), QStringLiteral("https://prod.example.test/"));
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — Live site"));

        // The popover's Preview Deploy runs the preview command: never production, and nothing pushed.
        SharePopover *popover = SharePanels::showOptions(w.share(), w.view);
        QVERIFY(popover->findChild<QPushButton *>(QStringLiteral("shareLatest"))->isVisible());
        auto *go = popover->findChild<QPushButton *>(QStringLiteral("shareGo"));
        QCOMPARE(go->text(), QStringLiteral("Preview Deploy"));
        QVERIFY(popover->findChild<QLabel *>(QStringLiteral("shareNote"))->text().contains(QLatin1String("not production")));
        go->click();
        QVERIFY(w.waitShared());
        QCOMPARE(clipboard(), QStringLiteral("https://preview.example.test/abc"));
        QCOMPARE(w.share().notice().text, QStringLiteral("Link copied — Preview deploy"));
        const QString head = WriteBack::git(site, {"rev-parse", "HEAD"}).trimmed();
        QCOMPARE(Deploy::deployed(site, head)->url, QStringLiteral("https://prod.example.test/"));
        QVERIFY(Deploy::latest(site)->preview);
        QCOMPARE(WriteBack::git(site + ".git", {"rev-parse", "main"}).trimmed(), remoteHead);

        // Both are in the Shared list; removing one leaves the deploy up.
        const std::vector<Share::Record> list = w.share().sharedList();
        QCOMPARE(list.size(), size_t(2));
        QCOMPARE(list.front().where, QStringLiteral("Preview deploy"));
        QVERIFY(!list.front().canUnshare());
        QCOMPARE(w.share().unshare(list.front().id), QString());
        QTRY_VERIFY(!w.share().running());
        QCOMPARE(w.share().notice().text, QStringLiteral("Removed from the list. The deploy itself stays up."));
        QCOMPARE(w.share().sharedList().size(), size_t(1));
    }

    void namesAndClippedSelectionsRenderAsTheyLook()
    {
        QCOMPARE(Share::safeName(QStringLiteral("Logo / final (v3)")), QStringLiteral("Logo-final-v3"));
        QCOMPARE(Share::safeName(QStringLiteral("..")), QStringLiteral("design"));
        // A shape inside a clipping group keeps its clipping path.
        EditorSession session;
        session.createDocument(QSizeF(200, 100));
        // Illustrator's rule: the topmost object clips.
        const QUuid art = session.addPath(Shapes::rectangle({10, 10, 100, 20}), QStringLiteral("Art"));
        const QUuid clip = session.addPath(Shapes::rectangle({0, 0, 50, 50}), QStringLiteral("Clip"));
        session.select({clip, art});
        session.makeClippingMask();
        const VectorDocument cropped = Share::selectionDocument(*session.document(), {art});
        QVERIFY(cropped.find(art));
        QVERIFY(cropped.find(clip));
        QCOMPARE(cropped.background.alpha(), 0);
        QCOMPARE(cropped.size, session.document()->bounds(std::vector<QUuid>{art}, true).size());
    }

    void shareIsInTheFileMenuTheKeysAndCtrlK()
    {
        Window w;
        QAction *entry = w.view.menus()->action(QStringLiteral("shareWithClient"));
        QVERIFY(entry && entry->isEnabled());
        QCOMPARE(entry->text(), QStringLiteral("Share"));
        QCOMPARE(entry->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S));
        QVERIFY(w.view.menus()->action(QStringLiteral("shareOptions")));
        QVERIFY(w.view.menus()->action(QStringLiteral("sharedLinks")));
        CommandPalette *palette = w.view.menus()->commandPalette();
        palette->open();
        QCOMPARE(palette->results(QStringLiteral("share")).front().id, QStringLiteral("action:shareWithClient"));
        QStringList ids;
        for (const auto &each : palette->results(QStringLiteral("unshare")))
            ids << each.id;
        QVERIFY(ids.contains(QStringLiteral("action:sharedLinks")));
        palette->close();
    }
};

QTEST_MAIN(ShareTests)
#include "ShareTests.moc"
