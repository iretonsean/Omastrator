#include "IO/ProjectStore.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include <QTemporaryDir>
#include <QTest>

// Styled runs out and back in: .omai keeps everything, SVG writes each run
// as a <tspan> and reads spans back as runs.
class TextRunsIOTests : public QObject {
    Q_OBJECT

private:
    static const VectorObject *firstText(const VectorDocument &document)
    {
        for (const VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::text)
                return &object;
        }
        return nullptr;
    }

    // "One bold word in a sentence", the fourth word bold and red.
    static VectorDocument sentence()
    {
        VectorDocument document = VectorDocument::blank({400, 200});
        VectorObject object;
        object.kind = ObjectKind::text;
        object.name = QStringLiteral("Sentence");
        object.text.text = QStringLiteral("One bold word in a sentence");
        object.text.size = 24;
        object.text.formatCharacters(4, 8, [](CharacterFormat &format) {
            format.style = TextContent::styleFor(format.family, 700, false);
            format.fill = QColor(Qt::red);
        });
        object.fill = Paint::solid(Qt::black);
        object.stroke.paint = Paint::none();
        object.transform = QTransform::fromTranslate(20, 60);
        document.insert(object, document.layers().front());
        return document;
    }

private slots:
    void boldOnOneWordRoundTripsThroughOmai()
    {
        QTemporaryDir folder;
        const QString path = folder.filePath(QStringLiteral("runs.omai"));
        const VectorDocument document = sentence();
        ProjectStore::write(document, path);
        const VectorDocument back = ProjectStore::read(path);
        QCOMPARE(back, document);
        QVERIFY(firstText(back)->text.formatAt(5).isBold());
        QVERIFY(!firstText(back)->text.formatAt(1).isBold());
    }

    void boldOnOneWordRoundTripsThroughSvg()
    {
        const VectorDocument document = sentence();
        const QByteArray svg = SvgExporter::serialize(document);
        QVERIFY2(svg.contains("<tspan font-weight=\"700\" fill=\"#ff0000\">bold</tspan>"), svg.constData());
        const VectorDocument back = SvgImporter::parse(svg);
        const VectorObject *text = firstText(back);
        QVERIFY(text);
        QCOMPARE(text->text.text, QStringLiteral("One bold word in a sentence"));
        for (int index = 0; index < text->text.text.size(); ++index) {
            const bool inWord = index >= 4 && index < 8;
            QCOMPARE(text->text.formatAt(index).isBold(), inWord);
            QCOMPARE(text->text.formatAt(index).fill, inWord ? std::optional<QColor>(QColor(Qt::red)) : std::nullopt);
        }
        QCOMPARE(text->fill, Paint::solid(Qt::black));
        // The same glyphs, where they were.
        const QRectF was = document.bounds(firstText(document)->id), is = back.bounds(text->id);
        QVERIFY2(std::abs(was.left() - is.left()) < 0.5 && std::abs(was.width() - is.width()) < 0.5,
                 qPrintable(QString("%1 %2 / %3 %4").arg(was.left()).arg(was.width()).arg(is.left()).arg(is.width())));
    }

    void runSizesShiftsAndFeaturesRoundTripThroughSvg()
    {
        VectorDocument document = sentence();
        VectorObject &object = document.objects.back();
        object.text.formatCharacters(0, 3, [](CharacterFormat &format) {
            format.size = 40;
            format.baselineShift = 6;
            format.features[QStringLiteral("smcp")] = 1;
            format.underline = true;
        });
        const QByteArray svg = SvgExporter::serialize(document);
        QVERIFY2(svg.contains("font-feature-settings:'smcp' 1"), svg.constData());
        const VectorDocument back = SvgImporter::parse(svg);
        const VectorObject *text = firstText(back);
        const CharacterFormat first = text->text.formatAt(0);
        QCOMPARE(first.size, 40.0);
        QCOMPARE(first.baselineShift, 6.0);
        QCOMPARE(first.features.at(QStringLiteral("smcp")), 1);
        QVERIFY(first.underline);
        QCOMPARE(text->text.formatAt(20).size, 24.0);
        QVERIFY(!text->text.formatAt(20).underline);
        QCOMPARE(text->text.text, QStringLiteral("One bold word in a sentence"));
    }

    void outlinedTextKeepsEachRunsColour()
    {
        SvgExporter::Options options;
        options.textAsOutlines = true;
        const QByteArray svg = SvgExporter::serialize(sentence(), options);
        QVERIFY(!svg.contains("<text"));
        QCOMPARE(svg.count("<path"), 2);
        QVERIFY(svg.contains("fill=\"#ff0000\""));
    }
};

QTEST_MAIN(TextRunsIOTests)
#include "TextRunsIOTests.moc"
