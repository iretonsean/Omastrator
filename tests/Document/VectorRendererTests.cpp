#include "Document/VectorDocument.h"
#include "Rendering/VectorRenderer.h"
#include <QTest>

// A Browser View draws its last picture (docs/BROWSER-VIEW.md, section 3) under the frame's clip.
namespace {
VectorDocument viewDocument(QUuid &frame, QRectF rect = {20, 20, 100, 100})
{
    VectorDocument document = VectorDocument::blank({200, 200});
    VectorObject view = VectorObject::frame(rect, QStringLiteral("Site"));
    QImage picture(50, 50, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::green);
    view.browser = BrowserView{QUrl(QStringLiteral("https://example.com")), {}, picture};
    frame = view.id;
    document.insert(view, document.layers().front());
    return document;
}

// A live picture's pixel ratio is its pixels per CSS px: the page as wide as the frame was, red on its left half.
QImage pageAt(int cssWidth, int cssHeight, double ratio = 1.0)
{
    QImage page(int(cssWidth * ratio), int(cssHeight * ratio), QImage::Format_ARGB32_Premultiplied);
    page.fill(Qt::blue);
    QPainter painter(&page);
    painter.fillRect(QRect(0, 0, page.width() / 2, page.height()), Qt::red);
    painter.end();
    page.setDevicePixelRatio(ratio);
    return page;
}

// A page with a marker in each corner, at 2 px per CSS px: red 10 CSS px square at the top left, green at the
// bottom right, yellow at the top right and cyan at the bottom left, on blue.
QImage markedPage(int cssWidth, int cssHeight)
{
    QImage page(cssWidth * 2, cssHeight * 2, QImage::Format_ARGB32_Premultiplied);
    page.fill(Qt::blue);
    QPainter painter(&page);
    painter.scale(2, 2);
    painter.fillRect(QRect(0, 0, 10, 10), Qt::red);
    painter.fillRect(QRect(cssWidth - 10, 0, 10, 10), Qt::yellow);
    painter.fillRect(QRect(0, cssHeight - 10, 10, 10), Qt::cyan);
    painter.fillRect(QRect(cssWidth - 10, cssHeight - 10, 10, 10), Qt::green);
    painter.end();
    page.setDevicePixelRatio(2);
    return page;
}

// The canvas at `zoom`: a document point p lands at p * zoom.
QImage drawWith(const VectorDocument &document, const QUuid &frame, const QImage &live, double zoom = 1)
{
    QImage target(int(300 * zoom), int(200 * zoom), QImage::Format_ARGB32_Premultiplied);
    target.fill(Qt::white);
    QPainter painter(&target);
    painter.scale(zoom, zoom);
    VectorRenderer::Options options;
    options.livePicture = [&](const QUuid &id) { return id == frame ? live : QImage(); };
    VectorRenderer::draw(painter, document, options);
    painter.end();
    return target;
}
}

class VectorRendererTests : public QObject {
    Q_OBJECT

private slots:
    void anExportDrawsTheLastPictureFittedToTheFrame()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame);
        const QImage image = VectorRenderer::render(document, 1, false);
        QCOMPARE(image.pixelColor(30, 30), QColor(Qt::green));
        QCOMPARE(image.pixelColor(115, 115), QColor(Qt::green));
        QCOMPARE(image.pixelColor(130, 130), QColor(Qt::white));
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::white));
    }

    void thePictureIsClippedToTheFrame()
    {
        QUuid frame;
        VectorDocument document = viewDocument(frame);
        document.find(frame)->browser->picture = QImage(500, 500, QImage::Format_ARGB32_Premultiplied);
        document.find(frame)->browser->picture.fill(Qt::green);
        document.find(frame)->shape->rect = QRectF(20, 20, 100, 100);
        const QImage image = VectorRenderer::render(document, 1, false);
        QCOMPARE(image.pixelColor(119, 119), QColor(Qt::green));
        QCOMPARE(image.pixelColor(125, 125), QColor(Qt::white));
    }

    void theLivePictureWinsOverTheStoredOne()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame);
        QImage live(10, 10, QImage::Format_ARGB32_Premultiplied);
        live.fill(Qt::blue);
        // Ten pixels for the frame's hundred CSS px.
        live.setDevicePixelRatio(0.1);
        QImage target(200, 200, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        VectorRenderer::Options options;
        options.livePicture = [&](const QUuid &id) { return id == frame ? live : QImage(); };
        VectorRenderer::draw(painter, document, options);
        painter.end();
        QCOMPARE(target.pixelColor(30, 30), QColor(Qt::blue));
    }

    void aStalePictureInANarrowerBoxIsClippedNotSqueezed()
    {
        // The 100 by 80 page in a 60 by 50 box: squeezed, its green corner would show at the box's bottom right.
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {20, 20, 60, 50});
        for (const double zoom : {1.0, 2.0}) {
            const QImage image = drawWith(document, frame, markedPage(100, 80), zoom);
            const auto seen = [&](double x, double y) { return image.pixelColor(QPointF(x * zoom, y * zoom).toPoint()); };
            QCOMPARE(seen(20 + 5, 20 + 5), QColor(Qt::red));
            QCOMPARE(seen(20 + 12, 20 + 12), QColor(Qt::blue));
            QCOMPARE(seen(20 + 55, 20 + 45), QColor(Qt::blue));
            QCOMPARE(seen(20 + 55, 20 + 5), QColor(Qt::blue));
            QCOMPARE(seen(20 + 5, 20 + 45), QColor(Qt::blue));
            // Nothing outside the box.
            QCOMPARE(seen(20 + 62, 20 + 45), QColor(Qt::white));
            QCOMPARE(seen(20 + 55, 20 + 52), QColor(Qt::white));
            QCOMPARE(seen(20 + 95, 20 + 75), QColor(Qt::white));
        }
    }

    void aStalePictureInAWiderBoxKeepsItsSizeAndTopLeftCorner()
    {
        // The 100 by 80 page in a 200 by 150 box: stretched, its corners would be the box's; centred, the red one
        // would move in.
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {20, 20, 200, 150});
        for (const double zoom : {1.0, 2.0}) {
            const QImage image = drawWith(document, frame, markedPage(100, 80), zoom);
            const auto seen = [&](double x, double y) { return image.pixelColor(QPointF(x * zoom, y * zoom).toPoint()); };
            QCOMPARE(seen(20 + 1, 20 + 1), QColor(Qt::red));
            QCOMPARE(seen(20 + 9, 20 + 9), QColor(Qt::red));
            QCOMPARE(seen(20 + 12, 20 + 12), QColor(Qt::blue));
            QCOMPARE(seen(20 + 95, 20 + 5), QColor(Qt::yellow));
            QCOMPARE(seen(20 + 5, 20 + 75), QColor(Qt::cyan));
            QCOMPARE(seen(20 + 91, 20 + 71), QColor(Qt::green));
            QCOMPARE(seen(20 + 99, 20 + 79), QColor(Qt::green));
            // The rest of the box is empty until the reflowed picture comes.
            QCOMPARE(seen(20 + 102, 20 + 75), QColor(Qt::white));
            QCOMPARE(seen(20 + 95, 20 + 82), QColor(Qt::white));
            QCOMPARE(seen(20 + 195, 20 + 145), QColor(Qt::white));
        }
    }

    void atAPhonesWidthTheFrameDrawsThePhoneLayoutWhole()
    {
        // A 390 by 300 box and the page Chromium sends at that size, 2 px per CSS px: every corner lands on the box's.
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {20, 20, 390, 300});
        QImage page = markedPage(390, 300);
        {
            // The phone layout: a 20 CSS px bar down the page's middle, which only the narrow layout has. The painter
            // works in CSS px, since the page has 2 px per CSS px.
            QPainter painter(&page);
            painter.fillRect(QRect(185, 20, 20, 260), Qt::magenta);
        }
        QImage target(500, 400, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        VectorRenderer::Options options;
        options.livePicture = [&](const QUuid &id) { return id == frame ? page : QImage(); };
        VectorRenderer::draw(painter, document, options);
        painter.end();
        QCOMPARE(target.pixelColor(20 + 5, 20 + 5), QColor(Qt::red));
        QCOMPARE(target.pixelColor(20 + 385, 20 + 5), QColor(Qt::yellow));
        QCOMPARE(target.pixelColor(20 + 5, 20 + 295), QColor(Qt::cyan));
        QCOMPARE(target.pixelColor(20 + 385, 20 + 295), QColor(Qt::green));
        QCOMPARE(target.pixelColor(20 + 195, 20 + 150), QColor(Qt::magenta));
        QCOMPARE(target.pixelColor(20 + 150, 20 + 150), QColor(Qt::blue));
        QCOMPARE(target.pixelColor(20 + 393, 20 + 150), QColor(Qt::white));
        QCOMPARE(target.pixelColor(20 + 195, 20 + 303), QColor(Qt::white));
    }

    void aStaleDensePictureIsDrawnAtItsCssSizeUnderTheCanvasZoom()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {10, 10, 60, 80});
        // Two pixels per CSS px, for a page 100 CSS px wide (red for its first 50), zoomed 2x on the canvas.
        constexpr double zoom = 2;
        const QImage target = drawWith(document, frame, pageAt(100, 100, 2.0), zoom);
        const auto seen = [&](double x, double y) { return target.pixelColor(QPointF(x * zoom, y * zoom).toPoint()); };
        // 1:1 in CSS px from the frame's corner: red to 10 + 50, blue after it, never stretched to 60 or shrunk by the density.
        QCOMPARE(seen(10 + 48, 50), QColor(Qt::red));
        QCOMPARE(seen(10 + 52, 50), QColor(Qt::blue));
        QCOMPARE(seen(11, 11), QColor(Qt::red));
        // Clipped to the frame, which is shorter than the page.
        QCOMPARE(seen(10 + 52, 10 + 82), QColor(Qt::white));
        QCOMPARE(seen(10 + 62, 50), QColor(Qt::white));
    }

    void aPictureThatShowsTheFramesSizeFillsItWhateverItsDensity()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame);
        // 90 px for 100 CSS px: what a screencast at 0.9 gives.
        const QImage image = drawWith(document, frame, pageAt(100, 100, 0.9));
        QCOMPARE(image.pixelColor(20 + 40, 20 + 50), QColor(Qt::red));
        QCOMPARE(image.pixelColor(20 + 60, 20 + 50), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(20 + 98, 20 + 98), QColor(Qt::blue));
    }

    void aNullLivePictureFallsBackToTheStoredOne()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame);
        QImage target(200, 200, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        VectorRenderer::Options options;
        options.livePicture = [](const QUuid &) { return QImage(); };
        VectorRenderer::draw(painter, document, options);
        painter.end();
        QCOMPARE(target.pixelColor(30, 30), QColor(Qt::green));
    }

    void outlineViewDrawsNoPicture()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame);
        QImage target(200, 200, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        VectorRenderer::Options options;
        options.outlineMode = true;
        VectorRenderer::draw(painter, document, options);
        painter.end();
        QCOMPARE(target.pixelColor(60, 60), QColor(Qt::white));
    }
};

QTEST_MAIN(VectorRendererTests)
#include "VectorRendererTests.moc"
