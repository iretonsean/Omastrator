#include "Document/VectorDocument.h"
#include "Rendering/VectorRenderer.h"
#include <QTest>

// A Browser View draws its last picture (docs/BROWSER-VIEW.md, section 3) under the frame's clip.
namespace {
VectorDocument viewDocument(QUuid &frame)
{
    VectorDocument document = VectorDocument::blank({200, 200});
    VectorObject view = VectorObject::frame({20, 20, 100, 100}, QStringLiteral("Site"));
    QImage picture(50, 50, QImage::Format_ARGB32_Premultiplied);
    picture.fill(Qt::green);
    view.browser = BrowserView{QUrl(QStringLiteral("https://example.com")), {}, picture};
    frame = view.id;
    document.insert(view, document.layers().front());
    return document;
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
        QImage target(200, 200, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        VectorRenderer::Options options;
        options.livePicture = [&](const QUuid &id) { return id == frame ? live : QImage(); };
        VectorRenderer::draw(painter, document, options);
        painter.end();
        QCOMPARE(target.pixelColor(30, 30), QColor(Qt::blue));
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
