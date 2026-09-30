#include "UI/CapturePanel.h"
#include "../Agent/FakeHyprctl.h"
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
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
