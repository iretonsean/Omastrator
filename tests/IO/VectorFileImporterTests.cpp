#include "IO/VectorFileImporter.h"
#include <QTemporaryDir>
#include <QTest>

class VectorFileImporterTests : public QObject {
    Q_OBJECT

private slots:
    // Dropped on an open canvas these open in their own tab; pictures and SVGs are placed.
    void designFilesOpenInTheirOwnTab()
    {
        QVERIFY(VectorFileImporter::isDesignFile(QStringLiteral("/nowhere/App.sketch")));
        QVERIFY(VectorFileImporter::isDesignFile(QStringLiteral("/nowhere/App.PENPOT")));
        QVERIFY(!VectorFileImporter::isDesignFile(QStringLiteral("/nowhere/logo.svg")));
        QVERIFY(!VectorFileImporter::isDesignFile(QStringLiteral("/nowhere/photo.png")));
        QVERIFY(!VectorFileImporter::isDesignFile(QStringLiteral("/nowhere/board.excalidraw")));
    }

    void aFigSuffixNeedsFigmaContent()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("notes.fig"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("just text");
        file.close();
        QVERIFY(!VectorFileImporter::isDesignFile(path));
    }
};

QTEST_MAIN(VectorFileImporterTests)
#include "VectorFileImporterTests.moc"
