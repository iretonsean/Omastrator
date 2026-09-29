#include "Document/PathOperations.h"
#include "WidgetCleanup.h"
#include "UI/CommandPalette.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SharePanels.h"
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// Send to a device (docs/SHARE.md): the selection goes to a nearby Apple device by AirDrop, through omdrop and
// omadrop. Both are fakes here that log what they're asked, so nothing reaches the radio.
namespace {
// omdrop: `on` opens the window; `peers` lists $FAKE_AIRDROP_DIR/peers.json once it's open, and `-n --stream`
// answers with named.jsonl.
constexpr const char *fakeOmdrop = R"sh(#!/bin/sh
echo "$@" >> "$FAKE_AIRDROP_DIR/omdrop.log"
case "$1" in
on) touch "$FAKE_AIRDROP_DIR/on"; exit 0;;
peers)
  case "$*" in
  *--stream*) cat "$FAKE_AIRDROP_DIR/named.jsonl" 2>/dev/null; exit 0;;
  esac
  if [ ! -f "$FAKE_AIRDROP_DIR/on" ]; then echo '[]'; echo 'Omdrop is currently OFF. Turn it on to see peers.' >&2; exit 1; fi
  if [ -f "$FAKE_AIRDROP_DIR/peers.json" ]; then cat "$FAKE_AIRDROP_DIR/peers.json"; exit 0; fi
  echo '[]'; echo 'No devices found yet. Open the AirDrop sheet on the device you want to see.' >&2; exit 1;;
esac
exit 2
)sh";

// omadrop: keeps a copy of the file it was asked to send, and ends as $FAKE_SEND says.
constexpr const char *fakeOmadrop = R"sh(#!/bin/sh
echo "$@" >> "$FAKE_AIRDROP_DIR/omadrop.log"
for f; do last="$f"; done
cp "$last" "$FAKE_AIRDROP_DIR/sent-${last##*/}"
case "$FAKE_SEND" in
decline) echo "Asking them to accept it..."; echo "They declined it."; exit 1;;
asleep) echo "Asking them to accept it..."; echo "They never answered: never opened"; exit 1;;
broken) echo "The transfer failed: reset by peer"; exit 1;;
slow) echo "Asking them to accept it..."; exec sleep 30;;
*) echo "Asking them to accept it..."; echo "They accepted. Sending..."; echo "Sent photo.png, 12 KB"; exit 0;;
esac
)sh";

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

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

constexpr const char *twoPeers = R"([{"mac":"aa:bb:cc:00:00:01","rssi":-48,"name":null},{"mac":"aa:bb:cc:00:00:02","rssi":-60,"name":null}])";
constexpr const char *twoNamed = "{\"mac\":\"aa:bb:cc:00:00:01\",\"rssi\":-48,\"name\":\"Test iPhone\"}\n"
                                 "{\"mac\":\"aa:bb:cc:00:00:02\",\"rssi\":-60,\"name\":\"Studio iPad\"}\n";

struct Window {
    ProjectWorkspace workspace;
    ProjectWorkspaceView view{workspace};

    Window()
    {
        view.show();
        workspace.createDocument(QSizeF(200, 100));
    }
    EditorSession &session() { return workspace.current().session; }
    ShareController &share() { return *view.share(); }
    QUuid box(QRectF rect) { return session().addPath(Shapes::rectangle(rect), QStringLiteral("Box")); }
    bool waitFor(const std::function<bool()> &done, int ms = 10'000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < ms && !done())
            QTest::qWait(20);
        return done();
    }
    bool waitSearch() { return waitFor([this] { return !share().device().searching(); }); }
    bool waitNotice(ShareController::Notice::Kind kind)
    {
        return waitFor([this, kind] { return !share().running() && share().notice().kind == kind; });
    }
};
}

class DeviceShareTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString dir() const { return m_directory.filePath(QStringLiteral("airdrop")); }
    QString state(const QString &name) const { return dir() + QLatin1Char('/') + name; }
    void windowIsOn() { writeFile(state(QStringLiteral("on")), ""); }
    void peersAre(const char *listed, const char *named = twoNamed)
    {
        writeFile(state(QStringLiteral("peers.json")), listed);
        writeFile(state(QStringLiteral("named.jsonl")), named);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        writeFile(m_directory.filePath(QStringLiteral("bin/omdrop")), fakeOmdrop, true);
        writeFile(m_directory.filePath(QStringLiteral("bin/omadrop")), fakeOmadrop, true);
        qputenv("FAKE_AIRDROP_DIR", dir().toUtf8());
        qputenv("OMASTRATOR_DEVICE_RETRY_MS", "20");
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
    }

    void init()
    {
        QSettings().clear();
        QDir(dir()).removeRecursively();
        QDir().mkpath(dir());
        // Nothing here may find the real omdrop and its radio.
        qputenv("OMASTRATOR_OMDROP", m_directory.filePath(QStringLiteral("bin/omdrop")).toUtf8());
        qputenv("OMASTRATOR_OMADROP", m_directory.filePath(QStringLiteral("bin/omadrop")).toUtf8());
        qputenv("FAKE_SEND", "ok");
    }

    void cleanup()
    {
        deleteTopLevelWidgets([](QWidget *widget) { return widget->windowFlags().testFlag(Qt::Popup); });
        QTest::qWait(20);
    }

    void peersAreReadFromBothTheArrayAndTheStream()
    {
        const auto array = DeviceSend::parsePeers(twoPeers);
        QCOMPARE(array.size(), size_t(2));
        QCOMPARE(array[0].address, QStringLiteral("aa:bb:cc:00:00:01"));
        QCOMPARE(array[0].signal, std::optional<int>(-48));
        QVERIFY(array[0].name.isEmpty());
        QCOMPARE(array[0].label(), QStringLiteral("Apple device (-48 dBm)"));
        const auto lines = DeviceSend::parsePeers(twoNamed);
        QCOMPARE(lines.size(), size_t(2));
        QCOMPARE(lines[1].label(), QStringLiteral("Studio iPad"));
        QVERIFY(DeviceSend::parsePeers("[]").empty());
        QVERIFY(DeviceSend::parsePeers("not json").empty());
    }

    void aSendsOutputBecomesOneLineWhenItFailed()
    {
        QVERIFY(DeviceSend::failureText(0, QStringLiteral("Sent photo.png, 12 KB"), QStringLiteral("Phone")).isEmpty());
        QCOMPARE(DeviceSend::failureText(1, QStringLiteral("They declined it."), QStringLiteral("Phone")), QStringLiteral("Phone declined it."));
        QVERIFY(DeviceSend::failureText(1, QStringLiteral("never opened"), QStringLiteral("Phone")).contains(QLatin1String("Everyone for 10 Minutes")));
        QCOMPARE(DeviceSend::failureText(1, QStringLiteral("a\nThe transfer failed: reset by peer\n"), QStringLiteral("Phone")),
                 QStringLiteral("The send to Phone failed: The transfer failed: reset by peer"));
        // A refused connection is a failure, not a decline.
        QVERIFY(!DeviceSend::failureText(1, QStringLiteral("connection refused"), QStringLiteral("Phone")).contains(QLatin1String("declined")));
        QCOMPARE(DeviceSend::failureText(2, QString(), QStringLiteral("Phone")), QStringLiteral("The send to Phone failed."));
    }

    void withoutOmdropTheMenuSaysHowToInstallIt()
    {
        qputenv("OMASTRATOR_OMDROP", "/nonexistent/omdrop");
        Window w;
        w.box({10, 10, 40, 20});
        QAction *entry = w.view.menus()->action(QStringLiteral("sendToDevice"));
        QVERIFY(entry && entry->isEnabled());
        entry->trigger();
        auto *popover = w.view.findChild<DevicePopover *>(QStringLiteral("deviceSendPopover"));
        QVERIFY(popover);
        auto *status = popover->findChild<QLabel *>(QStringLiteral("deviceStatus"));
        QVERIFY(status && !status->isHidden());
        QVERIFY(status->text().contains(QLatin1String("omdrop plugin")));
        QVERIFY(!popover->findChild<QPushButton *>(QStringLiteral("deviceGo"))->isEnabled());
        QVERIFY(!QFile::exists(state(QStringLiteral("omdrop.log"))));
    }

    void aSendThatCantStartSaysSo()
    {
        qputenv("OMASTRATOR_OMDROP", "/nonexistent/omdrop");
        qputenv("OMASTRATOR_OMADROP", "/nonexistent/omadrop");
        Window w;
        w.box({10, 10, 40, 20});
        QVERIFY(w.share().sendToDevice({QStringLiteral("aa:bb:cc:00:00:01"), QStringLiteral("Phone"), std::nullopt}, Share::Format::png).isEmpty());
        QCOMPARE(w.share().notice().kind, ShareController::Notice::Kind::failed);
        QVERIFY(w.share().notice().text.contains(QLatin1String("omdrop plugin")));
    }

    void whenAirdropIsOffItIsTurnedOnForFiveMinutesThenDevicesAreListedWithTheirNames()
    {
        peersAre(twoPeers);
        Window w;
        w.box({10, 10, 40, 20});
        DeviceSend &device = w.share().device();
        device.refresh();
        QVERIFY(device.searching());
        QVERIFY(w.waitSearch());
        QVERIFY(device.problem().isEmpty());
        QCOMPARE(device.devices().size(), size_t(2));
        QCOMPARE(device.devices()[0].name, QStringLiteral("Test iPhone"));
        QCOMPARE(device.devices()[1].label(), QStringLiteral("Studio iPad"));
        const QString calls = readText(state(QStringLiteral("omdrop.log")));
        QVERIFY(calls.contains(QLatin1String("on 5m")));
        QVERIFY(calls.indexOf(QLatin1String("on 5m")) < calls.indexOf(QLatin1String("peers -n")));
        QVERIFY(!QFile::exists(state(QStringLiteral("omadrop.log"))));
    }

    void noDevicesNearbyGivesTheAdvice()
    {
        windowIsOn();
        Window w;
        w.box({10, 10, 40, 20});
        DeviceSend &device = w.share().device();
        device.refresh();
        QVERIFY(w.waitSearch());
        QVERIFY(device.devices().empty());
        QVERIFY(device.problem().contains(QLatin1String("No Apple devices nearby")));
        QVERIFY(device.problem().contains(QLatin1String("Everyone for 10 Minutes")));
        // It looked several times before giving up, without turning anything on.
        QVERIFY(readText(state(QStringLiteral("omdrop.log"))).count(QLatin1String("peers --json")) > 2);
        QVERIFY(!readText(state(QStringLiteral("omdrop.log"))).contains(QLatin1String("on 5m")));
    }

    void theSelectionIsSentAsAPngToTheChosenDeviceAndTheLastDeviceIsRemembered()
    {
        windowIsOn();
        peersAre(twoPeers);
        Window w;
        const QUuid picked = w.box({10, 10, 40, 20});
        w.box({100, 50, 30, 30});
        w.session().select({picked});
        w.view.menus()->action(QStringLiteral("sendToDevice"))->trigger();
        auto *popover = w.view.findChild<DevicePopover *>(QStringLiteral("deviceSendPopover"));
        QVERIFY(popover);
        QVERIFY(popover->findChild<QLabel *>(QStringLiteral("deviceScope"))->text().contains(QLatin1String("the selection (1 object)")));
        QVERIFY(w.waitSearch());
        auto *list = popover->findChild<QComboBox *>(QStringLiteral("deviceList"));
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->itemText(1), QStringLiteral("Studio iPad"));
        list->setCurrentIndex(1);
        auto *go = popover->findChild<QPushButton *>(QStringLiteral("deviceGo"));
        QVERIFY(go->isEnabled());
        // The look, when asked for: the popover with its devices, then the toast.
        const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB");
        if (!grab.isEmpty())
            popover->grab().save(QString::fromUtf8(grab) + QStringLiteral("/send-to-device.png"));
        go->click();
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::shared));
        if (!grab.isEmpty())
            w.view.grab().save(QString::fromUtf8(grab) + QStringLiteral("/send-to-device-toast.png"));
        QCOMPARE(w.share().notice().text, QStringLiteral("Sent to Studio iPad"));
        QVERIFY(w.share().notice().detail.contains(QLatin1String("The selection (1 object) as PNG")));

        const QString call = readText(state(QStringLiteral("omadrop.log")));
        QVERIFY(call.startsWith(QLatin1String("send --quiet --to aa:bb:cc:00:00:02 --label Studio iPad -- ")));
        // Only the selection, cropped to its bounds at 2x, on clear paper.
        const QStringList sent = QDir(dir()).entryList({QStringLiteral("sent-*.png")});
        QCOMPARE(sent.size(), 1);
        const QImage image(state(sent.front()));
        QCOMPARE(image.size(), (w.session().document()->bounds(std::vector<QUuid>{picked}, true).size() * 2).toSize());
        QVERIFY(qAlpha(image.pixel(0, 0)) > 0);
        // The temporary file goes when the send ends.
        const QString path = call.mid(call.indexOf(QLatin1String("-- ")) + 3).trimmed();
        QVERIFY(!QFile::exists(path));
        QVERIFY(!popover->isVisible() || popover->isHidden());

        // The next time, that device is the one chosen.
        w.view.menus()->action(QStringLiteral("sendToDevice"))->trigger();
        auto *again = w.view.findChild<DevicePopover *>(QStringLiteral("deviceSendPopover"));
        QVERIFY(again);
        QVERIFY(w.waitSearch());
        QCOMPARE(again->findChild<QComboBox *>(QStringLiteral("deviceList"))->currentText(), QStringLiteral("Studio iPad"));
        QCOMPARE(QSettings().value(QStringLiteral("shareDeviceName")).toString(), QStringLiteral("Studio iPad"));
    }

    void withNothingSelectedTheArtboardGoesAsAPdfIfAsked()
    {
        windowIsOn();
        peersAre(twoPeers);
        Window w;
        w.box({10, 10, 40, 20});
        w.session().deselectAll();
        w.share().device().refresh();
        QVERIFY(w.waitSearch());
        QVERIFY(w.share().sendToDevice(w.share().device().devices().front(), Share::Format::pdf).isEmpty());
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::shared));
        QVERIFY(w.share().notice().detail.startsWith(QLatin1String("The artboard as PDF")));
        const QStringList sent = QDir(dir()).entryList({QStringLiteral("sent-*.pdf")});
        QCOMPARE(sent.size(), 1);
        QVERIFY(readText(state(sent.front())).startsWith(QLatin1String("%PDF")));
        QCOMPARE(w.share().deviceFormat(), Share::Format::pdf);
    }

    void aDeclinedSendSaysWhoDeclinedAndAnAsleepPhoneSaysWhatToDo()
    {
        windowIsOn();
        peersAre(twoPeers);
        Window w;
        w.box({10, 10, 40, 20});
        w.share().device().refresh();
        QVERIFY(w.waitSearch());
        const DeviceSend::Device phone = w.share().device().devices().front();

        qputenv("FAKE_SEND", "decline");
        QVERIFY(w.share().sendToDevice(phone, Share::Format::png).isEmpty());
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::failed));
        QCOMPARE(w.share().notice().text, QStringLiteral("Test iPhone declined it."));

        qputenv("FAKE_SEND", "asleep");
        QVERIFY(w.share().sendToDevice(phone, Share::Format::png).isEmpty());
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::failed));
        QVERIFY(w.share().notice().text.contains(QLatin1String("didn't answer")));
        QVERIFY(w.share().notice().text.contains(QLatin1String("Everyone for 10 Minutes")));

        qputenv("FAKE_SEND", "broken");
        QVERIFY(w.share().sendToDevice(phone, Share::Format::png).isEmpty());
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::failed));
        QVERIFY(w.share().notice().text.contains(QLatin1String("reset by peer")));
    }

    void withoutOmadropOmdropSendsAlone()
    {
        qputenv("OMASTRATOR_OMADROP", "/nonexistent/omadrop");
        windowIsOn();
        peersAre(twoPeers);
        Window w;
        w.box({10, 10, 40, 20});
        w.share().device().refresh();
        QVERIFY(w.waitSearch());
        // omdrop's own sender isn't the fake's `send`, which answers with a failure: the point is which program ran.
        QVERIFY(w.share().sendToDevice(w.share().device().devices().front(), Share::Format::png).isEmpty());
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::failed));
        QVERIFY(readText(state(QStringLiteral("omdrop.log"))).contains(QLatin1String("send --wait 45 --to aa:bb:cc:00:00:01 -- ")));
        QVERIFY(!QFile::exists(state(QStringLiteral("omadrop.log"))));
    }

    void cancelStopsTheSendAndTheToastSaysSo()
    {
        qputenv("FAKE_SEND", "slow");
        windowIsOn();
        peersAre(twoPeers);
        Window w;
        w.box({10, 10, 40, 20});
        w.share().device().refresh();
        QVERIFY(w.waitSearch());
        QVERIFY(w.share().sendToDevice(w.share().device().devices().front(), Share::Format::png).isEmpty());
        QVERIFY(w.share().running());
        QVERIFY(w.waitFor([&w] { return w.share().notice().text.startsWith(QLatin1String("Waiting for")); }));
        // Nothing else can share while it runs.
        QVERIFY(!w.share().share().isEmpty());
        QVERIFY(!w.view.menus()->action(QStringLiteral("sendToDevice"))->isEnabled());
        auto *cancel = w.view.findChild<QPushButton *>(QStringLiteral("shareToastCancel"));
        QVERIFY(cancel && !cancel->isHidden());
        cancel->click();
        QVERIFY(w.waitNotice(ShareController::Notice::Kind::failed));
        QCOMPARE(w.share().notice().text, QStringLiteral("Cancelled."));
    }

    void itsInTheSharePopoverAndCtrlK()
    {
        Window w;
        w.box({10, 10, 40, 20});
        auto *options = SharePanels::showOptions(w.share(), w.view);
        auto *button = options->findChild<QPushButton *>(QStringLiteral("shareToDevice"));
        QVERIFY(button && button->isEnabled());
        QCOMPARE(button->text(), QStringLiteral("Send to a Device…"));
        CommandPalette *palette = w.view.menus()->commandPalette();
        palette->open();
        QCOMPARE(palette->results(QStringLiteral("airdrop")).front().id, QStringLiteral("action:sendToDevice"));
        palette->close();
    }
};

QTEST_MAIN(DeviceShareTests)
#include "DeviceShareTests.moc"
