#include "UI/ExportForScreensSheet.h"
#include <QListWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// Export for Screens' checklist of artboards.
class ExportForScreensSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void anArtboardSetNotToExportIsNotOffered()
    {
        QSettings().clear();
        EditorSession session;
        session.createDocument({400, 300});
        session.addArtboard(QRectF(500, 0, 400, 300));
        session.addArtboard(QRectF(1000, 0, 400, 300));
        session.renameArtboard(1, QStringLiteral("Scratch"));
        session.setArtboardExported(1, false);
        ExportForScreensSheet sheet(session);
        auto *list = sheet.findChild<QListWidget *>(QStringLiteral("screenExportArtboards"));
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        for (int row = 0; row < list->count(); ++row)
            QVERIFY(list->item(row)->text() != QLatin1String("Scratch"));
        // The rows carry their artboard's id, so the last one is still the third board.
        QCOMPARE(list->item(1)->data(Qt::UserRole).toString(), session.document()->artboard(2).id.toString(QUuid::WithoutBraces));
    }

    void aPageWhoseArtboardsAreAllUnexportedHasNoHeadingOrRows()
    {
        QSettings().clear();
        EditorSession session;
        session.createDocument({400, 300});
        session.addPage(QStringLiteral("Scratch"));
        session.setArtboardExported(0, false);
        ExportForScreensSheet sheet(session);
        auto *list = sheet.findChild<QListWidget *>(QStringLiteral("screenExportArtboards"));
        QVERIFY(list);
        // Two pages: the first's heading and board, nothing of the second.
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Page 1"));
    }
};

QTEST_MAIN(ExportForScreensSheetTests)
#include "ExportForScreensSheetTests.moc"
