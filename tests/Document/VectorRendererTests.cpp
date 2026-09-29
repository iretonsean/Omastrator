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

QImage drawWith(const VectorDocument &document, const QUuid &frame, const QImage &live)
{
    QImage target(300, 200, QImage::Format_ARGB32_Premultiplied);
    target.fill(Qt::white);
    QPainter painter(&target);
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
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {20, 20, 60, 100});
        // The 100 px page, red for its first 50: squeezed into 60 it would turn blue at 20 + 30.
        const QImage image = drawWith(document, frame, pageAt(100, 100));
        QCOMPARE(image.pixelColor(20 + 45, 60), QColor(Qt::red));
        QCOMPARE(image.pixelColor(20 + 5, 60), QColor(Qt::red));
        QCOMPARE(image.pixelColor(20 + 62, 60), QColor(Qt::white));
    }

    void aStalePictureInAWiderBoxKeepsItsSizeAndTopLeftCorner()
    {
        QUuid frame;
        const VectorDocument document = viewDocument(frame, {20, 20, 200, 150});
        const QImage image = drawWith(document, frame, pageAt(100, 100));
        QCOMPARE(image.pixelColor(20 + 40, 20 + 50), QColor(Qt::red));
        QCOMPARE(image.pixelColor(20 + 80, 20 + 50), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(20 + 150, 20 + 50), QColor(Qt::white));
        QCOMPARE(image.pixelColor(20 + 40, 20 + 120), QColor(Qt::white));
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
