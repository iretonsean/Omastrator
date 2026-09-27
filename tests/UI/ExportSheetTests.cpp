#include "Document/PathOperations.h"
#include "UI/ExportSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QtTest>

// PNG and JPEG export: settings, the encoded size, the answer.
namespace {
// Circles in several colours, so quality shows in bytes.
VectorDocument artwork()
{
    VectorDocument document = VectorDocument::blank(QSizeF(200, 100));
    for (int index = 0; index < 6; ++index) {
        VectorObject circle;
        circle.path = Shapes::ellipse(QRectF(index * 30, index * 10, 60, 60));
        circle.fill = Paint::linear(QColor::fromHsv(index * 60, 255, 255), Qt::black);
        document.insert(circle, document.layers().back());
    }
    return document;
}
}

class ExportSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void eachFormatShowsItsOwnSettings();
    void qualityChangesTheEncodedBytes();
    void theScaleSetsThePixelSize();
    void exportAnswersTheSettingsAndCancelNothing();
};

void ExportSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ExportSheetTests::init()
{
    QSettings().remove(ExportSheet::qualityKey);
}

void ExportSheetTests::eachFormatShowsItsOwnSettings()
{
    ExportSheet jpeg(artwork(), DocumentExporter::Format::jpeg, [](std::optional<RasterOptions>) {});
    QVERIFY(jpeg.findChild<QSlider *>("jpegQuality")->isVisibleTo(&jpeg));
    QVERIFY(!jpeg.findChild<QCheckBox *>("exportTransparent")->isVisibleTo(&jpeg));
    QCOMPARE(jpeg.options().quality, 90);
    QCOMPARE(jpeg.findChild<QLabel *>("jpegPercent")->text(), QString("90%"));
    ExportSheet png(artwork(), DocumentExporter::Format::png, [](std::optional<RasterOptions>) {});
    QVERIFY(!png.findChild<QSlider *>("jpegQuality")->isVisibleTo(&png));
    QVERIFY(png.findChild<QCheckBox *>("exportTransparent")->isVisibleTo(&png));
    png.findChild<QCheckBox *>("exportTransparent")->setChecked(true);
    QVERIFY(png.options().transparent);
    // The last quality is where the next sheet starts.
    QSettings().setValue(ExportSheet::qualityKey, 55);
    ExportSheet again(artwork(), DocumentExporter::Format::jpeg, [](std::optional<RasterOptions>) {});
    QCOMPARE(again.options().quality, 55);
}

void ExportSheetTests::qualityChangesTheEncodedBytes()
{
    ExportSheet sheet(artwork(), DocumentExporter::Format::jpeg, [](std::optional<RasterOptions>) {});
    auto &note = *sheet.findChild<QLabel *>("exportBytes");
    QCOMPARE(sheet.encodedBytes(), qint64(-1));
    QCOMPARE(note.text(), QString("· Updating preview…"));
    // The pause ends in an encode by itself.
    QTRY_VERIFY(sheet.encodedBytes() > 0);
    const qint64 high = sheet.encodedBytes();
    QVERIFY(note.text().startsWith("· ") && note.text() != QString("· Updating preview…"));
    sheet.findChild<QSlider *>("jpegQuality")->setValue(5);
    QCOMPARE(sheet.encodedBytes(), qint64(-1));
    sheet.encode();
    QVERIFY(sheet.encodedBytes() > 0);
    QVERIFY(sheet.encodedBytes() < high);
    QCOMPARE(sheet.findChild<QLabel *>("jpegPercent")->text(), QString("5%"));
}

void ExportSheetTests::theScaleSetsThePixelSize()
{
    ExportSheet sheet(artwork(), DocumentExporter::Format::png, [](std::optional<RasterOptions>) {});
    auto &size = *sheet.findChild<QLabel *>("exportSize");
    QCOMPARE(size.text(), QString("200 × 100 px"));
    auto &scale = *sheet.findChild<QComboBox *>("exportScale");
    QCOMPARE(scale.count(), 4);
    QCOMPARE(scale.itemText(1), QString("2× · 144 ppi"));
    scale.setCurrentIndex(2);
    emit scale.activated(2);
    QCOMPARE(sheet.options().scale, 3.0);
    QCOMPARE(size.text(), QString("600 × 300 px"));
}

void ExportSheetTests::exportAnswersTheSettingsAndCancelNothing()
{
    std::optional<RasterOptions> answer;
    bool answered = false;
    ExportSheet sheet(artwork(), DocumentExporter::Format::jpeg, [&](std::optional<RasterOptions> chosen) {
        answer = chosen;
        answered = true;
    });
    sheet.findChild<QSlider *>("jpegQuality")->setValue(70);
    auto &scale = *sheet.findChild<QComboBox *>("exportScale");
    scale.setCurrentIndex(1);
    emit scale.activated(1);
    sheet.findChild<QPushButton *>("exportConfirm")->click();
    QVERIFY(answered);
    QCOMPARE(answer.value().quality, 70);
    QCOMPARE(answer.value().scale, 2.0);
    QVERIFY(!answer.value().transparent);
    QCOMPARE(QSettings().value(ExportSheet::qualityKey).toInt(), 70);
    answered = false;
    sheet.findChild<QPushButton *>("exportCancel")->click();
    QVERIFY(answered);
    QVERIFY(!answer);
}

QTEST_MAIN(ExportSheetTests)
#include "ExportSheetTests.moc"
