#include "Canvas/EditorCanvas.h"
#include "Document/DocumentCodec.h"
#include "Document/PathOperations.h"
#include "IO/DocumentExporter.h"
#include "IO/ProjectStore.h"
#include "IO/ScreenExport.h"
#include "IO/SvgExporter.h"
#include "IO/ExcalidrawImporter.h"
#include "IO/SvgImporter.h"
#include "Rendering/CanvasViewport.h"
#include "Rendering/VectorRenderer.h"
#include "UI/ExportSheet.h"
#include "UI/PresetStore.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPdfWriter>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// The tested artboard limit (README): 50 m and the 1,000,000 pt maximum draw, zoom, save and
// export without a full-size raster ever being made.
namespace {
constexpr double fiftyMeters = 141732; // 50 m in points, rounded
constexpr double maximum = VectorDocument::maximumArtboardSide;

VectorObject rectangleObject(const QRectF &rect, const QColor &color)
{
    VectorObject object;
    object.path = Shapes::rectangle(rect);
    object.fill = Paint::solid(color);
    object.stroke.paint = Paint::none();
    return object;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class LargeArtboardTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void theLimitIsOneNumberEverythingReads();
    void savingAndReopeningKeepsTheSize_data();
    void savingAndReopeningKeepsTheSize();
    void theCodecReopensWhatIsBeyondTheLimit();
    void anSvgAtTheLimitSavesAndReopensAndOneBeyondIsRefusedClearly();
    void anOversizedImportSavesAndReopens();
    void fitArtboardToArtworkStaysWithinTheLimitAndReopens();
    void deletingArtboardOneKeepsAnOversizedSecondBoardOpenable();
    void aMaskGroupInAnImageExportKeepsItsResolution();
    void aMaskGroupInAPosterPdfKeepsItsResolution();
    void sizeEditsClampToTheLimitAndUndo();
    void svgAndPdfKeepTheFullSize_data();
    void svgAndPdfKeepTheFullSize();
    void rasterExportRefusesTheFullSizeAndWritesASmallScale();
    void rasterFitsAndLargestScaleAgree();
    void aMaskGroupOnAHugePageStaysWithinTheBudget();
    void farShapesDrawWhereTheMathsSays_data();
    void farShapesDrawWhereTheMathsSays();
    void fitAndZoomWorkAndSelectionLandsOnTheCorner_data();
    void fitAndZoomWorkAndSelectionLandsOnTheCorner();
    void theExportSheetOffersScalesThatFit();
    void theExportSheetCapsThePreviewOfAVeryLargeRaster();
    void theExportSheetStillShowsTheFileSizeForOrdinaryExports();
    void screenExportSaysWhatItSkipped();
};

void LargeArtboardTests::theLimitIsOneNumberEverythingReads()
{
    QCOMPARE(PresetStore::maximumPoints, maximum);
    QCOMPARE(PresetStore::limitDescription(), QStringLiteral("1,000,000 points (about 350 m)"));
    // 50 m is well inside it.
    QVERIFY(fiftyMeters < maximum);
    // Fit can reach the limit in a small window.
    QVERIFY(1000 / maximum >= CanvasViewport::minimumZoom);
}

void LargeArtboardTests::savingAndReopeningKeepsTheSize_data()
{
    QTest::addColumn<double>("side");
    QTest::newRow("50 m") << fiftyMeters;
    QTest::newRow("limit") << maximum;
}

void LargeArtboardTests::savingAndReopeningKeepsTheSize()
{
    QFETCH(double, side);
    VectorDocument document = VectorDocument::blank({side, side});
    const QRectF far(side - 200, side - 200, 100, 50);
    document.insert(rectangleObject(far, Qt::red), document.layers().front());
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("big.omai"));
    ProjectStore::write(document, file);
    const VectorDocument back = ProjectStore::read(file);
    QCOMPARE(back.size, QSizeF(side, side));
    QCOMPARE(back.objects.back().path.bounds(), far);
}

void LargeArtboardTests::theCodecReopensWhatIsBeyondTheLimit()
{
    const VectorDocument fine = VectorDocument::blank({maximum, maximum});
    QCOMPARE(DocumentCodec::decode(DocumentCodec::encode(fine)).size, QSizeF(maximum, maximum));
    // The limit belongs to the edits; anything that could be written must open again.
    const VectorDocument beyond = VectorDocument::blank({maximum * 4, 100});
    QCOMPARE(DocumentCodec::decode(DocumentCodec::encode(beyond)).size, QSizeF(maximum * 4, 100));
    // A size that isn't a size is still refused.
    for (const QSizeF bad : {QSizeF(0, 100), QSizeF(100, -5)}) {
        QJsonObject json = DocumentCodec::encode(beyond);
        json["width"] = bad.width();
        json["height"] = bad.height();
        QVERIFY_EXCEPTION_THROWN(DocumentCodec::decode(json), std::exception);
    }
}

void LargeArtboardTests::anSvgAtTheLimitSavesAndReopensAndOneBeyondIsRefusedClearly()
{
    const auto svg = [](double side) {
        return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"") + QByteArray::number(side, 'f', 0) + "\" height=\"1000\">"
               "<rect x=\"10\" y=\"10\" width=\"50\" height=\"50\" fill=\"#f00\"/></svg>";
    };
    const VectorDocument imported = SvgImporter::parse(svg(maximum));
    QCOMPARE(imported.size, QSizeF(maximum, 1000));
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("svg.omai"));
    ProjectStore::write(imported, file);
    QCOMPARE(ProjectStore::read(file).size, QSizeF(maximum, 1000));
    try {
        SvgImporter::parse(svg(maximum * 2));
        QFAIL("an SVG beyond the limit was imported");
    } catch (const FileError &error) {
        QVERIFY2(QString(error.what()).contains(QLatin1String("1,000,000")), error.what());
    }
}

void LargeArtboardTests::anOversizedImportSavesAndReopens()
{
    // Importers that size the page from the art (Excalidraw here, PDF and Figma likewise) can pass the limit.
    QJsonObject wide;
    for (const auto &[key, value] : std::initializer_list<std::pair<const char *, QJsonValue>>{
             {"id", "a"}, {"type", "rectangle"}, {"x", 0}, {"y", 0}, {"width", 50}, {"height", 50}, {"angle", 0},
             {"strokeColor", "#1e1e1e"}, {"backgroundColor", "transparent"}, {"fillStyle", "solid"}, {"strokeWidth", 2},
             {"strokeStyle", "solid"}, {"opacity", 100}, {"locked", false}, {"isDeleted", false}})
        wide[QLatin1String(key)] = value;
    QJsonObject far = wide;
    far["id"] = "b";
    far["x"] = maximum * 2;
    const QJsonDocument scene(QJsonObject{{"type", "excalidraw"}, {"version", 2}, {"elements", QJsonArray{wide, far}}, {"files", QJsonObject()}});
    EditorSession session;
    session.loadDocument(ExcalidrawImporter::parse(scene.toJson()));
    QVERIFY(session.document()->size.width() > maximum);
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("imported.omai"));
    ProjectStore::write(*session.document(), file);
    const VectorDocument back = ProjectStore::read(file);
    QCOMPARE(back.size, session.document()->size);
    QCOMPARE(back.objects.size(), session.document()->objects.size());
}

void LargeArtboardTests::fitArtboardToArtworkStaysWithinTheLimitAndReopens()
{
    EditorSession session;
    session.createDocument({800, 600});
    session.addPath(Shapes::rectangle(QRectF(0, 0, 100, 100)), QStringLiteral("Left"));
    session.addPath(Shapes::rectangle(QRectF(maximum * 3, 0, 100, 100)), QStringLiteral("Right"));
    session.fitArtboardToArtwork(0);
    // The height is the art's (with its stroke); only the width is held to the limit.
    QCOMPARE(session.document()->size.width(), maximum);
    QVERIFY(session.document()->size.height() >= 100 && session.document()->size.height() < 110);
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("fit.omai"));
    ProjectStore::write(*session.document(), file);
    QCOMPARE(ProjectStore::read(file).size, session.document()->size);
    session.undo();
    QCOMPARE(session.document()->size, QSizeF(800, 600));

    // Art that fits still fits (its 1 pt stroke adds half a point a side).
    session.deleteSelection();
    session.selectAll();
    session.deleteSelection();
    session.addPath(Shapes::rectangle(QRectF(10, 20, 300, 200)), QStringLiteral("Small"));
    session.fitArtboardToArtwork(0);
    QCOMPARE(session.document()->size, QSizeF(301, 201));
}

void LargeArtboardTests::deletingArtboardOneKeepsAnOversizedSecondBoardOpenable()
{
    // Artboard 2 is oversized, as an import would leave it: the file loads because only the first counted.
    VectorDocument document = VectorDocument::blank({800, 600});
    std::vector<Artboard> boards = document.allArtboards();
    Artboard second;
    second.name = QStringLiteral("Poster");
    second.rect = QRectF(1000, 0, maximum * 2, 500);
    boards.push_back(second);
    document.setArtboards(boards);
    const VectorDocument opened = DocumentCodec::decode(DocumentCodec::encode(document));
    QCOMPARE(opened.artboardCount(), 2);

    EditorSession session;
    session.loadDocument(opened);
    session.deleteArtboard(0);
    QCOMPARE(session.document()->size, QSizeF(maximum * 2, 500));
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("deleted.omai"));
    ProjectStore::write(*session.document(), file);
    QCOMPARE(ProjectStore::read(file).size, QSizeF(maximum * 2, 500));
    session.undo();
    QCOMPARE(session.document()->size, QSizeF(800, 600));
}

void LargeArtboardTests::sizeEditsClampToTheLimitAndUndo()
{
    EditorSession session;
    session.createDocument({800, 600});
    session.setArtboardSize({maximum * 3, 50});
    QCOMPARE(session.document()->size, QSizeF(maximum, 50));
    session.undo();
    QCOMPARE(session.document()->size, QSizeF(800, 600));

    const QUuid added = session.addArtboard(QRectF(0, 0, maximum * 2, maximum * 2));
    const QSizeF addedSize = session.document()->allArtboards().back().rect.size();
    QVERIFY(session.document()->allArtboards().back().id == added);
    QCOMPARE(addedSize, QSizeF(maximum, maximum));
}

void LargeArtboardTests::svgAndPdfKeepTheFullSize_data()
{
    QTest::addColumn<double>("side");
    QTest::newRow("50 m") << fiftyMeters;
    QTest::newRow("limit") << maximum;
}

void LargeArtboardTests::svgAndPdfKeepTheFullSize()
{
    QFETCH(double, side);
    VectorDocument document = VectorDocument::blank({side, side});
    document.insert(rectangleObject({side - 200, side - 200, 100, 50}, Qt::red), document.layers().front());
    const QByteArray svg = SvgExporter::serialize(document);
    QVERIFY(svg.contains(QByteArray::number(qint64(side))));
    QTemporaryDir dir;
    const QString pdf = dir.filePath(QStringLiteral("big.pdf"));
    DocumentExporter::writePdf(document, pdf);
    const QByteArray bytes = readAll(pdf);
    QVERIFY(bytes.startsWith("%PDF"));
    const qsizetype box = bytes.indexOf("/MediaBox");
    QVERIFY(box >= 0);
    // The page is written at full size, not clamped: the box holds the side in points (Qt may
    // scale by its resolution, so only its order of magnitude is checked).
    const QList<QByteArray> parts = bytes.mid(box, 80).split(' ');
    QVERIFY(parts.size() >= 5);
    QVERIFY(parts.at(3).toDouble() > side * 0.9);
}

void LargeArtboardTests::rasterExportRefusesTheFullSizeAndWritesASmallScale()
{
    VectorDocument document = VectorDocument::blank({fiftyMeters, fiftyMeters});
    document.insert(rectangleObject({0, 0, fiftyMeters / 2, fiftyMeters / 2}, Qt::red), document.layers().front());
    QTemporaryDir dir;
    QVERIFY(!DocumentExporter::rasterFits(document.size, 1));
    QVERIFY_EXCEPTION_THROWN(DocumentExporter::writePng(document, dir.filePath(QStringLiteral("full.png")), 1), FileError);
    QVERIFY_EXCEPTION_THROWN(DocumentExporter::writeJpeg(document, dir.filePath(QStringLiteral("full.jpg")), 1), FileError);
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("full.png"))));

    // 1,000 px a side: 4 MB.
    const double scale = 1000 / fiftyMeters;
    const QString small = dir.filePath(QStringLiteral("small.png"));
    DocumentExporter::writePng(document, small, scale);
    const QImage image(small);
    QVERIFY(image.width() >= 999 && image.width() <= 1001);
    QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red));
    QCOMPARE(image.pixelColor(900, 900), QColor(Qt::white));
}

void LargeArtboardTests::rasterFitsAndLargestScaleAgree()
{
    QVERIFY(DocumentExporter::rasterFits({800, 600}, 4));
    QVERIFY(!DocumentExporter::rasterFits({800, 600}, 100));
    for (const double side : {800.0, 20000.0, fiftyMeters, maximum}) {
        const double largest = DocumentExporter::largestRasterScale({side, side});
        QVERIFY2(DocumentExporter::rasterFits({side, side}, largest * 0.99), qPrintable(QString::number(side)));
        QVERIFY2(!DocumentExporter::rasterFits({side, side}, largest * 1.01), qPrintable(QString::number(side)));
    }
    // A wide, short page is held by the side rule, not the pixel count.
    QVERIFY(!DocumentExporter::rasterFits({40000, 10}, 1));
    QVERIFY(DocumentExporter::rasterFits({20000, 10000}, 1));
}

void LargeArtboardTests::aMaskGroupOnAHugePageStaysWithinTheBudget()
{
    EditorSession session;
    session.createDocument({fiftyMeters, fiftyMeters});
    StrokeStyle none;
    none.paint = Paint::none();
    session.setDefaultStroke(none);
    session.setDefaultFill(Paint::solid(Qt::red));
    const QRectF whole(0, 0, fiftyMeters, fiftyMeters);
    const QUuid art = session.addPath(Shapes::rectangle(whole), QStringLiteral("Art"));
    const QUuid maskShape = session.addPath(Shapes::rectangle(whole), QStringLiteral("Mask"));
    session.select({maskShape});
    session.setFillOfSelection(Paint::solid(Qt::white));
    session.select({art, maskShape});
    session.makeOpacityMask();
    QVERIFY(session.document()->find(session.selection().front())->mask.has_value());

    // A 400 × 300 view of a 50 m masked page: only the visible part may be rasterized.
    QImage view(400, 300, QImage::Format_ARGB32_Premultiplied);
    view.fill(Qt::white);
    QPainter painter(&view);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-1000, -1000);
    QElapsedTimer timer;
    timer.start();
    VectorRenderer::draw(painter, *session.document(), {});
    painter.end();
    QVERIFY2(timer.elapsed() < 20000, "the masked page was rasterized far beyond what is shown");
    const QColor middle = view.pixelColor(200, 150);
    QVERIFY(middle.red() > 250 && middle.green() < 5 && middle.blue() < 5);
}

namespace {
// A full-bleed page whose masked group shows only a 1 pt white stripe: sharp only if the mask's raster keeps the output's resolution.
VectorDocument stripeMaskedPage(QSizeF page, double stripeLeft)
{
    EditorSession session;
    session.createDocument(page);
    StrokeStyle none;
    none.paint = Paint::none();
    session.setDefaultStroke(none);
    session.setDefaultFill(Paint::solid(Qt::red));
    const QUuid art = session.addPath(Shapes::rectangle(QRectF(QPointF(0, 0), page)), QStringLiteral("Art"));
    const QUuid stripe = session.addPath(Shapes::rectangle(QRectF(stripeLeft, 0, 1, page.height())), QStringLiteral("Stripe"));
    session.select({stripe});
    session.setFillOfSelection(Paint::solid(Qt::white));
    session.select({art, stripe});
    session.makeOpacityMask();
    return *session.document();
}
}

void LargeArtboardTests::aMaskGroupInAnImageExportKeepsItsResolution()
{
    // 1920 × 1080 at 4× is 33 MP, over the old fixed budget: the stripe (pixels 402 to 405) must still be pixel exact.
    const VectorDocument document = stripeMaskedPage({1920, 1080}, 100.5);
    const QImage image = VectorRenderer::render(document, 4, true);
    QCOMPARE(image.size(), QSize(7680, 4320));
    QCOMPARE(image.pixelColor(401, 2000), QColor(Qt::transparent));
    for (const int x : {402, 403, 404, 405}) {
        const QColor color = image.pixelColor(x, 2000);
        QVERIFY2(color.red() > 250 && color.green() < 5 && color.alpha() > 250, qPrintable(QString::number(x)));
    }
    QCOMPARE(image.pixelColor(406, 2000), QColor(Qt::transparent));
}

void LargeArtboardTests::aMaskGroupInAPosterPdfKeepsItsResolution()
{
    // An A1 poster: the mask's patch is embedded at 3 px per pt (216 ppi), not thinned to fit 16 MP.
    const QSizeF poster(1684, 2384);
    const VectorDocument document = stripeMaskedPage(poster, 100.5);
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("poster.pdf"));
    DocumentExporter::writePdf(document, file);
    const QByteArray bytes = readAll(file);
    QVERIFY(bytes.startsWith("%PDF"));
    int widest = 0;
    for (qsizetype at = bytes.indexOf("/Width "); at >= 0; at = bytes.indexOf("/Width ", at + 1))
        widest = std::max(widest, bytes.mid(at + 7, 8).split('\n').first().toInt());
    QVERIFY2(widest >= int(poster.width() * 3) - 10, qPrintable(QString::number(widest)));
}

void LargeArtboardTests::farShapesDrawWhereTheMathsSays_data()
{
    QTest::addColumn<double>("side");
    QTest::newRow("50 m") << fiftyMeters;
    QTest::newRow("limit") << maximum;
}

void LargeArtboardTests::farShapesDrawWhereTheMathsSays()
{
    QFETCH(double, side);
    VectorDocument document = VectorDocument::blank({side, side});
    VectorObject shape;
    shape.path = Shapes::ellipse({0, 0, side, side});
    shape.fill = Paint::solid(Qt::red);
    shape.stroke.paint = Paint::none();
    document.insert(shape, document.layers().front());
    // Zoom 32 on the ellipse's rightmost point: its edge lands on the middle column.
    QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(32, 32);
    painter.translate(-(side - 6.25), -(side / 2 - 4.6875));
    VectorRenderer::draw(painter, document, {});
    painter.end();
    QCOMPARE(image.pixelColor(10, 150), QColor(Qt::red));
    int edge = -1;
    for (int x = 0; x < 400 && edge < 0; ++x)
        if (image.pixelColor(x, 150).green() > 250)
            edge = x;
    QVERIFY2(qAbs(edge - 200) <= 1, qPrintable(QString::number(edge)));
}

void LargeArtboardTests::fitAndZoomWorkAndSelectionLandsOnTheCorner_data()
{
    QTest::addColumn<double>("side");
    QTest::newRow("50 m") << fiftyMeters;
    QTest::newRow("limit") << maximum;
}

void LargeArtboardTests::fitAndZoomWorkAndSelectionLandsOnTheCorner()
{
    QFETCH(double, side);
    EditorSession session;
    session.createDocument({side, side});
    session.usesSmartGuides = false;
    const QRectF target(side - 200, side - 200, 100, 50);
    const QUuid id = session.addPath(Shapes::rectangle(target), QStringLiteral("Far"));
    session.deselectAll();
    EditorCanvas canvas(session);
    canvas.resize(800, 600);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    // Fit shows the whole page, so the zoom is the true fit and not the clamp.
    session.zoomToFit();
    QVERIFY(session.viewport.zoom() > CanvasViewport::minimumZoom);
    QVERIFY(session.viewport.zoom() * side <= 800);
    QVERIFY(!canvas.grab().toImage().isNull());

    // Zoom to the rectangle's bottom right corner: the corner is where the maths puts it.
    session.zoomToRect(QRectF(target.bottomRight() - QPointF(10, 10), QSizeF(20, 15)));
    const QImage image = canvas.grab().toImage();
    const QPointF corner = session.viewport.viewPoint(target.bottomRight(), session.document()->size);
    QVERIFY(session.viewport.zoom() > 10);
    QVERIFY(QRectF(0, 0, 800, 600).contains(corner));
    QCOMPARE(image.pixelColor((corner - QPointF(5, 5)).toPoint()).lightness() < 100, true);
    QCOMPARE(image.pixelColor((corner + QPointF(60, 60)).toPoint()), QColor(Qt::white));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, (corner - QPointF(20, 20)).toPoint());
    QCOMPARE(session.selection(), std::vector<QUuid>{id});
}

void LargeArtboardTests::theExportSheetOffersScalesThatFit()
{
    VectorDocument document = VectorDocument::blank({fiftyMeters, fiftyMeters});
    std::optional<RasterOptions> answer;
    ExportSheet sheet(document, DocumentExporter::Format::png, [&](std::optional<RasterOptions> chosen) { answer = chosen; });
    auto &scale = *sheet.findChild<QComboBox *>("exportScale");
    QVERIFY(scale.count() > 1);
    // Every choice fits, the first is the biggest that does, and 1× isn't among them.
    QVERIFY(sheet.options().scale < 1);
    QVERIFY(DocumentExporter::rasterFits(document.size, sheet.options().scale));
    QVERIFY(sheet.findChild<QPushButton *>("exportConfirm")->isEnabled());
    for (int index = 0; index < scale.count(); ++index)
        QVERIFY(!scale.itemText(index).startsWith(QStringLiteral("1×")));

    // The smallest choice is a small image, and it previews normally with its size.
    scale.setCurrentIndex(scale.count() - 1);
    emit scale.activated(scale.count() - 1);
    sheet.encode();
    QVERIFY(sheet.encodedBytes() > 0);
    sheet.findChild<QPushButton *>("exportConfirm")->click();
    QVERIFY(answer.has_value());
    QVERIFY(DocumentExporter::rasterFits(document.size, answer->scale));
}

void LargeArtboardTests::theExportSheetCapsThePreviewOfAVeryLargeRaster()
{
    // 10,000 pt a side at 1× is 100 MP: it exports, but the preview is drawn smaller and the size isn't guessed.
    VectorDocument document = VectorDocument::blank({10000, 10000});
    ExportSheet sheet(document, DocumentExporter::Format::png, [](std::optional<RasterOptions>) {});
    QCOMPARE(sheet.options().scale, 1.0);
    QVERIFY(DocumentExporter::rasterFits(document.size, 1));
    sheet.encode();
    QCOMPARE(sheet.encodedBytes(), qint64(-1));
    QCOMPARE(sheet.findChild<QLabel *>("exportBytes")->text(), QStringLiteral("· Preview at reduced size"));
}

void LargeArtboardTests::theExportSheetStillShowsTheFileSizeForOrdinaryExports()
{
    // 1920 × 1080 at 4× is 33 MP: main showed its size, so it must too.
    VectorDocument document = VectorDocument::blank({1920, 1080});
    ExportSheet sheet(document, DocumentExporter::Format::png, [](std::optional<RasterOptions>) {});
    auto &scale = *sheet.findChild<QComboBox *>("exportScale");
    int four = -1;
    for (int index = 0; index < scale.count(); ++index)
        if (scale.itemText(index).startsWith(QStringLiteral("4×")))
            four = index;
    QVERIFY(four >= 0);
    scale.setCurrentIndex(four);
    emit scale.activated(four);
    QCOMPARE(sheet.options().scale, 4.0);
    sheet.encode();
    QVERIFY(sheet.encodedBytes() > 0);
    QVERIFY(!sheet.findChild<QLabel *>("exportBytes")->text().contains(QLatin1String("reduced")));
}

void LargeArtboardTests::screenExportSaysWhatItSkipped()
{
    VectorDocument document = VectorDocument::blank({fiftyMeters, fiftyMeters});
    document.insert(rectangleObject({0, 0, 1000, 1000}, Qt::red), document.layers().front());
    QTemporaryDir dir;
    ScreenExport::Settings settings;
    settings.folder = dir.path();
    settings.scales = {1, 0.005};
    settings.formats = {QStringLiteral("png"), QStringLiteral("jpg")};
    QStringList skipped;
    const QStringList written = ScreenExport::run(document, {document.artboard(0).id}, {}, settings, {}, &skipped);
    // Only the 0.005× files were made (709 px); the 1× ones were named as skipped, once each.
    QCOMPARE(written.size(), 2);
    for (const QString &path : written) {
        QVERIFY(path.contains(QLatin1String("@0.005x")));
        QVERIFY(QImage(path).width() < 1000);
    }
    QVERIFY(!skipped.isEmpty());
    QCOMPARE(skipped.size(), QSet<QString>(skipped.begin(), skipped.end()).size());
    QStringList names;
    for (const QFileInfo &info : QDir(dir.path()).entryInfoList(QDir::Files))
        names << info.fileName();
    QCOMPARE(names.size(), 2);
}

QTEST_MAIN(LargeArtboardTests)
#include "LargeArtboardTests.moc"
