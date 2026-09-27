#include "Document/PathOperations.h"
#include "IO/ProjectStore.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ProjectStoreTests : public QObject {
    Q_OBJECT

private:
    static VectorDocument sample()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        document.background = QColor(250, 240, 230);
        const QUuid layer = document.layers().front();

        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = QStringLiteral("Group");
        group.opacity = 0.5;
        group.blendMode = LayerBlendMode::multiply;
        group.isClipGroup = true;
        document.insert(group, layer);

        VectorObject rect;
        rect.name = QStringLiteral("Rectangle");
        rect.path = Shapes::rectangle({10, 10, 100, 50}, 8);
        rect.fill = Paint::linear(Qt::red, Qt::blue);
        rect.stroke.width = 3;
        rect.stroke.dashes = {4, 2};
        document.insert(rect, group.id);

        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Title");
        text.text.text = QStringLiteral("Hello\nWorld");
        text.text.bold = true;
        text.fill = Paint::solid(QColor(10, 20, 30));
        text.transform = QTransform::fromTranslate(50, 80).rotate(15);
        document.insert(text, layer);

        VectorObject image;
        image.kind = ObjectKind::image;
        image.name = QStringLiteral("Photo");
        image.image = QImage(4, 3, QImage::Format_ARGB32_Premultiplied);
        image.image.fill(QColor(0, 128, 255));
        image.transform = QTransform::fromScale(10, 10);
        image.isLocked = true;
        document.insert(image, layer);
        return document;
    }

private slots:
    void writesAndReadsTheSameDocument()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("drawing.omai"));
        const VectorDocument document = sample();
        ProjectStore::write(document, path);
        QVERIFY(QFile::exists(path));
        const VectorDocument read = ProjectStore::read(path);
        QCOMPARE(read.objects.size(), document.objects.size());
        QVERIFY(read == document);
    }

    void overwritesAnExistingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("drawing.omai"));
        ProjectStore::write(sample(), path);
        const VectorDocument blank = VectorDocument::blank({100, 100});
        ProjectStore::write(blank, path);
        QCOMPARE(ProjectStore::read(path).size, QSizeF(100, 100));
        // QSaveFile leaves no temporary behind.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }

    void missingFileIsAFileError()
    {
        QTemporaryDir dir;
        try {
            ProjectStore::read(dir.filePath(QStringLiteral("nowhere.omai")));
            QFAIL("expected FileError");
        } catch (const FileError &error) {
            QVERIFY(error.message().contains(QStringLiteral("nowhere.omai")));
        }
    }

    void damagedJsonIsAFileError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("broken.omai"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"format\": \"omaillus");
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ProjectStore::read(path));
    }

    void codecErrorsBecomeFileErrors()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("other.omai"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"format": "omaillustrator", "version": 99, "width": 10, "height": 10})");
        file.close();
        try {
            ProjectStore::read(path);
            QFAIL("expected FileError");
        } catch (const FileError &error) {
            QVERIFY2(error.message().contains(QStringLiteral("newer")), qPrintable(error.message()));
        }
    }

    void unwritableFolderIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, ProjectStore::write(sample(), QStringLiteral("/nonexistent-folder/drawing.omai")));
    }
};

QTEST_MAIN(ProjectStoreTests)
#include "ProjectStoreTests.moc"
