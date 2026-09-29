#include "Canvas/SmartGuides.h"
#include "Document/PathOperations.h"
#include "Document/VectorDocument.h"
#include <QTest>

// Pages (docs/PAGES.md): what the canvas's helpers see of a document with two.
class PagesCanvasTests : public QObject {
    Q_OBJECT

private:
    static VectorObject square(QRectF rect)
    {
        VectorObject object;
        object.kind = ObjectKind::path;
        object.name = QStringLiteral("Rectangle");
        object.path = Shapes::rectangle(rect);
        object.fill = Paint::solid(Qt::red);
        return object;
    }

private slots:
    void smartGuidesSnapOnlyToTheCurrentPagesObjects()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const QUuid layerOne = document.layers().front();
        document.insert(square({10, 10, 30, 30}), layerOne);
        document.ensurePages();
        const QUuid second = QUuid::createUuid();
        document.pages.push_back({second, QStringLiteral("Page 2")});
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = QStringLiteral("Layer 2");
        layer.page = second;
        document.objects.push_back(layer);
        const VectorObject blue = square({50, 50, 30, 30});
        document.insert(blue, layer.id);
        document.ensurePages();

        const SmartGuides one(document, {});
        QCOMPARE(one.objects().size(), size_t(1));
        document.currentPage = second;
        const SmartGuides two(document, {});
        QCOMPARE(two.objects().size(), size_t(1));
        QCOMPARE(two.objects().front(), document.bounds(blue.id));
    }
};

QTEST_MAIN(PagesCanvasTests)
#include "PagesCanvasTests.moc"
