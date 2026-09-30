#include "UI/CapturePanel.h"
#include "../Agent/FakeHyprctl.h"
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// The dock's Capture tab (docs/WINDOW-LAYOUT.md): each button runs `omastrator island capture …`.
class CapturePanelTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString argv() const
    {
        QFile file(m_directory.filePath(QStringLiteral("omastrator.argv")));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        // The app's command: records its arguments and answers as `capture` does.
        const QString app = m_directory.filePath(QStringLiteral("omastrator"));
        QFile file(app);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\nprintf '%s\\n' \"$*\" > \"$0.argv\"\necho 'Fill: #ff6600'\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_APP", app.toUtf8());
    }

    void init() { QFile::remove(m_directory.filePath(QStringLiteral("omastrator.argv"))); }

    // A fake desktop: Omastrator's own window (never offered), a shown terminal, a browser on workspace 2
    // and a special-workspace window that isn't showing (never offered).
    void desktop(const FakeHyprctl &hyprctl, bool extraWindow = false) const
    {
        const QByteArray extra = extraWindow ? R"(,{"address":"0x5","pid":50,"class":"mpv","title":"Video","at":[0,0],"size":[640,360],
            "workspace":{"id":1,"name":"1"},"mapped":true,"hidden":false,"focusHistoryID":4})" : "";
        hyprctl.answer(QStringLiteral("clients"), QByteArray(R"([
            {"address":"0x1","pid":)") + QByteArray::number(QCoreApplication::applicationPid()) + R"(,"class":"io.github.iretonsean.Omastrator","title":"Omastrator",
             "at":[0,0],"size":[800,600],"workspace":{"id":1,"name":"1"},"mapped":true,"hidden":false,"focusHistoryID":0},
            {"address":"0x2","pid":20,"class":"kitty","title":"~/src","at":[10,20],"size":[400,300],
             "workspace":{"id":1,"name":"1"},"mapped":true,"hidden":false,"focusHistoryID":1},
            {"address":"0x3","pid":30,"class":"firefox","title":"Docs","at":[0,0],"size":[1000,700],
             "workspace":{"id":2,"name":"2"},"mapped":true,"hidden":false,"focusHistoryID":2},
            {"address":"0x4","pid":40,"class":"scratch","title":"Hidden","at":[0,0],"size":[100,100],
             "workspace":{"id":-98,"name":"special:scratch"},"mapped":true,"hidden":false,"focusHistoryID":3})" + extra + "]");
        hyprctl.answer(QStringLiteral("monitors"), R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":1920,"height":1080,"scale":1,
            "activeWorkspace":{"id":1,"name":"1"},"specialWorkspace":{"id":0,"name":""},"focused":true}])");
        hyprctl.answer(QStringLiteral("activeworkspace"), R"({"id": 1, "name": "1"})");
    }

    // A grim that writes a small PNG to the path it is given (its last argument) and logs its arguments.
    QString fakeGrim()
    {
        QImage(8, 5, QImage::Format_RGB32).save(m_directory.filePath(QStringLiteral("source.png")));
        const QString grim = m_directory.filePath(QStringLiteral("grim"));
        QFile file(grim);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$0.log\"\nfor a; do p=$a; done\ncp \"$(dirname \"$0\")/source.png\" \"$p\"\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return grim;
    }

    QList<QToolButton *> cells(CapturePanel &panel) const { return panel.findChildren<QToolButton *>(QStringLiteral("captureWindow")); }

    // A colour steps aside to the previous workspace first, and comes back when it's picked.
    void aColourStepsAsideAndComesBack()
    {
        FakeHyprctl hyprctl(m_directory.path());
        hyprctl.clearLog();
        hyprctl.answer(QStringLiteral("activeworkspace"), R"({"id": 3, "name": "3"})");
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        CapturePanel panel;
        QSignalSpy notices(&panel, &CapturePanel::notice);
        panel.findChild<QPushButton *>(QStringLiteral("captureFill"))->click();
        QVERIFY(panel.isCapturing() || !argv().isEmpty());
        QVERIFY(!panel.findChild<QPushButton *>(QStringLiteral("captureRegion"))->isEnabled() || !panel.isCapturing());
        QTRY_VERIFY(!panel.isCapturing());
        QCOMPARE(argv(), QStringLiteral("island capture color fill"));
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.first().first().toString(), QStringLiteral("Fill: #ff6600"));
        QCOMPARE(panel.findChild<QLabel *>(QStringLiteral("captureStatus"))->text(), QStringLiteral("Fill: #ff6600"));
        QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("captureRegion"))->isEnabled());
        // Away to the previous workspace, then back to workspace 3.
        const QStringList dispatches = hyprctl.dispatches();
        QCOMPARE(dispatches.size(), 2);
        QVERIFY2(dispatches.first().contains(QStringLiteral("previous")), qPrintable(dispatches.join('\n')));
        QVERIFY2(dispatches.last().contains(QStringLiteral("3")), qPrintable(dispatches.join('\n')));
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    // The grid lists the windows that can be captured, most recently used first, and Refresh reads them again.
    void theGridListsTheOpenWindows()
    {
        FakeHyprctl hyprctl(m_directory.path());
        desktop(hyprctl);
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        qputenv("OMASTRATOR_GRIM", fakeGrim().toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        CapturePanel panel;
        panel.refresh();
        QCOMPARE(cells(panel).size(), 2);
        QCOMPARE(cells(panel)[0]->property("label").toString(), QStringLiteral("kitty · ~/src"));
        QCOMPARE(cells(panel)[1]->property("label").toString(), QStringLiteral("firefox · Docs"));
        // The visible one is grabbed; the other workspace's keeps its icon.
        QTRY_VERIFY(cells(panel)[0]->property("thumbnail").toBool());
        QVERIFY(!cells(panel)[1]->property("thumbnail").toBool());
        QFile log(m_directory.filePath(QStringLiteral("grim.log")));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY2(log.readAll().contains("-g 10,20 400x300"), "grim gets the window's geometry");
        // The other windows' thumbnails aren't kept around.
        QVERIFY(QDir(m_directory.filePath(QStringLiteral("data/omastrator/captures"))).entryList({QStringLiteral("thumb-*")}).isEmpty());

        desktop(hyprctl, true);
        panel.findChild<QPushButton *>(QStringLiteral("captureRefresh"))->click();
        QCOMPARE(cells(panel).size(), 3);
        qunsetenv("OMASTRATOR_HYPRCTL");
        qunsetenv("OMASTRATOR_GRIM");
        qunsetenv("XDG_DATA_HOME");
        QFile::remove(m_directory.filePath(QStringLiteral("grim.log")));
    }

    // A window on another workspace: go there, grab it, come back, then open the picture as any capture.
    void clickingAWindowGrabsItAndOpensIt()
    {
        FakeHyprctl hyprctl(m_directory.path());
        desktop(hyprctl);
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        qputenv("OMASTRATOR_GRIM", fakeGrim().toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        CapturePanel panel;
        panel.refresh();
        QSignalSpy notices(&panel, &CapturePanel::notice);
        QTRY_VERIFY(cells(panel)[0]->property("thumbnail").toBool());
        hyprctl.clearLog();
        QFile::remove(m_directory.filePath(QStringLiteral("grim.log")));
        QCOMPARE(cells(panel).size(), 2);
        cells(panel)[1]->click();
        QVERIFY(panel.isCapturing());
        QTRY_VERIFY(!panel.isCapturing());
        QTRY_COMPARE(notices.count(), 1);
        QVERIFY(argv().startsWith(QStringLiteral("island capture image ")));
        QVERIFY(argv().endsWith(QStringLiteral(".png")));
        QVERIFY(QFileInfo::exists(argv().section(QLatin1Char(' '), 3)));
        const QStringList dispatches = hyprctl.dispatches();
        QCOMPARE(dispatches.size(), 3);
        QVERIFY2(dispatches[0].contains(QStringLiteral("2")), qPrintable(dispatches.join('\n')));
        QVERIFY2(dispatches[1].contains(QStringLiteral("0x3")), qPrintable(dispatches.join('\n')));
        QVERIFY2(dispatches[2].contains(QStringLiteral("1")), qPrintable(dispatches.join('\n')));
        QFile log(m_directory.filePath(QStringLiteral("grim.log")));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY2(log.readAll().contains("-g 0,0 1000x700"), "grim gets the window's geometry");
        qunsetenv("OMASTRATOR_HYPRCTL");
        qunsetenv("OMASTRATOR_GRIM");
        qunsetenv("XDG_DATA_HOME");
    }

    // A failed grab says so and leaves the panel usable.
    void aFailedGrabIsReported()
    {
        FakeHyprctl hyprctl(m_directory.path());
        desktop(hyprctl);
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        qputenv("OMASTRATOR_GRIM", "/nonexistent/grim");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        CapturePanel panel;
        panel.refresh();
        QSignalSpy notices(&panel, &CapturePanel::notice);
        cells(panel)[0]->click();
        QTRY_COMPARE(notices.count(), 1);
        QVERIFY(!panel.isCapturing());
        QVERIFY(argv().isEmpty());
        QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("captureRegion"))->isEnabled());
        qunsetenv("OMASTRATOR_HYPRCTL");
        qunsetenv("OMASTRATOR_GRIM");
        qunsetenv("XDG_DATA_HOME");
    }

    // The clipboard's SVG needs nothing behind the window: no workspace change.
    void pasteSvgStaysPut()
    {
        FakeHyprctl hyprctl(m_directory.path());
        hyprctl.clearLog();
        qputenv("OMASTRATOR_HYPRCTL", hyprctl.path().toUtf8());
        CapturePanel panel;
        panel.findChild<QPushButton *>(QStringLiteral("capturePasteSvg"))->click();
        QTRY_VERIFY(!panel.isCapturing());
        QCOMPARE(argv(), QStringLiteral("island capture paste-svg"));
        QVERIFY(hyprctl.dispatches().isEmpty());
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    // Without Hyprland it still captures, in place.
    void withoutHyprlandItCapturesInPlace()
    {
        qunsetenv("OMASTRATOR_HYPRCTL");
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        CapturePanel panel;
        panel.capture({QStringLiteral("screenshot")}, true);
        QTRY_VERIFY(!panel.isCapturing());
        QCOMPARE(argv(), QStringLiteral("island capture screenshot"));
    }
};

QTEST_MAIN(CapturePanelTests)
#include "CapturePanelTests.moc"
