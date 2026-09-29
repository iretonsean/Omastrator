#include "Document/DocumentCodec.h"
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
        text.text.style = QStringLiteral("Bold");
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

    // "Page 1" holds sample()'s art, "Page 2" a layer, an artboard and a guide of its own.
    static VectorDocument twoPages(QUuid *second = nullptr)
    {
        VectorDocument document = sample();
        document.guides.push_back({Qt::Vertical, 12});
        document.ensurePages();
        const QUuid page = QUuid::createUuid();
        document.pages.push_back({page, QStringLiteral("Page 2")});
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = QStringLiteral("Layer 2");
        layer.page = page;
        document.objects.push_back(layer);
        VectorObject rect;
        rect.name = QStringLiteral("Second");
        rect.path = Shapes::rectangle({5, 5, 20, 20});
        document.insert(rect, layer.id);
        document.artboards.push_back({QUuid::createUuid(), QStringLiteral("Artboard 1"), QRectF(0, 0, 200, 100), Qt::white, page});
        document.guides.push_back({Qt::Horizontal, 40, page});
        document.currentPage = page;
        if (second)
            *second = page;
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
        file.write(R"({"format": "omastrator", "version": 99, "width": 10, "height": 10})");
        file.close();
        try {
            ProjectStore::read(path);
            QFAIL("expected FileError");
        } catch (const FileError &error) {
            QVERIFY2(error.message().contains(QStringLiteral("newer")), qPrintable(error.message()));
        }
    }

    void pagesRoundTripThroughAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pages.omai"));
        QUuid second;
        const VectorDocument document = twoPages(&second);
        ProjectStore::write(document, path);
        const VectorDocument read = ProjectStore::read(path);
        QVERIFY(read == document);
        QCOMPARE(read.pages.size(), size_t(2));
        QCOMPARE(read.pages.back().name, QStringLiteral("Page 2"));
        QCOMPARE(read.currentPageId(), second);
        QCOMPARE(read.layers().size(), size_t(1));
        QCOMPARE(read.allLayers().size(), size_t(2));
        QCOMPARE(read.guides.back().page, second);
        QCOMPARE(read.allArtboards().front().page, second);
    }

    void onePageWritesVersion5AndTwoWriteVersion6()
    {
        QCOMPARE(DocumentCodec::encode(sample())["version"].toInt(), 5);
        VectorDocument one = sample();
        one.ensurePages();
        const QJsonObject oneJson = DocumentCodec::encode(one);
        QCOMPARE(oneJson["version"].toInt(), 5);
        QCOMPARE(oneJson["pages"].toArray().size(), 1);
        QVERIFY(DocumentCodec::decode(oneJson) == one);
        QCOMPARE(DocumentCodec::encode(twoPages())["version"].toInt(), 6);
    }

    void aVersion5FileLoadsAsOnePage()
    {
        const VectorDocument read = DocumentCodec::decode(DocumentCodec::encode(sample()));
        QVERIFY(read.pages.empty());
        QCOMPARE(read.pageCount(), 1);
        QCOMPARE(read.currentPageId(), VectorDocument::implicitPageId());
        QCOMPARE(read.allPages().front().name, QStringLiteral("Page 1"));
    }

    void anUnknownPageTagGoesToTheFirstPage()
    {
        QUuid second;
        QJsonObject json = DocumentCodec::encode(twoPages(&second));
        QJsonArray objects = json["objects"].toArray();
        for (int index = 0; index < objects.size(); ++index) {
            QJsonObject object = objects[index].toObject();
            if (object.contains("page") && object["page"].toString() == second.toString(QUuid::WithoutBraces)) {
                object["page"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                objects[index] = object;
            }
        }
        json["objects"] = objects;
        QJsonArray guides = json["guides"].toArray();
        QJsonObject guide = guides.at(1).toObject();
        guide["page"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        guides[1] = guide;
        json["guides"] = guides;
        const VectorDocument read = DocumentCodec::decode(json);
        const QUuid first = read.pages.front().id;
        QCOMPARE(read.layersOn(first).size(), size_t(2));
        QCOMPARE(read.layersOn(second).size(), size_t(0));
        QCOMPARE(read.guides.back().page, first);
    }

    void duplicatePageIdsAndNamesAreRepaired()
    {
        QUuid second;
        QJsonObject json = DocumentCodec::encode(twoPages(&second));
        QJsonArray pages = json["pages"].toArray();
        QJsonObject dupeId = pages.at(1).toObject();
        dupeId["name"] = QStringLiteral("Dropped");
        pages.append(dupeId);
        pages.append(QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"name", QStringLiteral("Page 1")}});
        pages.append(QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"name", QStringLiteral("  ")}});
        pages.append(QJsonObject{{"name", QStringLiteral("No id")}});
        json["pages"] = pages;
        const VectorDocument read = DocumentCodec::decode(json);
        QCOMPARE(read.pages.size(), size_t(4));
        QCOMPARE(read.pages[1].id, second);
        QCOMPARE(read.pages[1].name, QStringLiteral("Page 2"));
        QCOMPARE(read.pages[2].name, QStringLiteral("Page 1 2"));
        QCOMPARE(read.pages[3].name, QStringLiteral("Page 4"));
        // The pages added by hand each got an artboard.
        QCOMPARE(read.artboardsOn(read.pages[2].id).size(), size_t(1));
    }

    void aCurrentPageThatNamesNoPageMeansTheFirst()
    {
        QJsonObject json = DocumentCodec::encode(twoPages());
        json["currentPage"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const VectorDocument read = DocumentCodec::decode(json);
        QCOMPARE(read.currentPage, read.pages.front().id);
        json.remove("currentPage");
        QCOMPARE(DocumentCodec::decode(json).currentPage, read.pages.front().id);
    }

    void aCopiedLayerCarriesItsPageKey()
    {
        QUuid second;
        const VectorDocument document = twoPages(&second);
        const QJsonObject json = DocumentCodec::encode(*document.find(document.layers().front()));
        QCOMPARE(json["page"].toString(), second.toString(QUuid::WithoutBraces));
        QCOMPARE(DocumentCodec::decodeObject(json).page, second);
        const VectorDocument plain = sample();
        QVERIFY(!DocumentCodec::encode(*plain.find(plain.layers().front())).contains("page"));
    }

    void unwritableFolderIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, ProjectStore::write(sample(), QStringLiteral("/nonexistent-folder/drawing.omai")));
    }
};

QTEST_MAIN(ProjectStoreTests)
#include "ProjectStoreTests.moc"
