#include "Document/ImageTrace.h"
#include <QPainter>
#include <QTest>

class ImageTraceTests : public QObject {
    Q_OBJECT

private:
    static QImage paper(int width, int height)
    {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(Qt::white);
        return image;
    }

    static double area(const Contour &contour)
    {
        double sum = 0;
        const size_t n = contour.nodes.size();
        for (size_t i = 0; i < n; ++i) {
            const QPointF a = contour.nodes[i].anchor, b = contour.nodes[(i + 1) % n].anchor;
            sum += a.x() * b.y() - b.x() * a.y();
        }
        return std::abs(sum / 2);
    }

private slots:
    void tracesABlackSquare()
    {
        QImage image = paper(32, 32);
        QPainter(&image).fillRect(8, 8, 16, 16, Qt::black);
        const auto traced = ImageTrace::trace(image);
        QCOMPARE(traced.size(), size_t(1));
        const VectorObject &object = traced.front();
        QCOMPARE(object.kind, ObjectKind::path);
        QCOMPARE(object.fill.kind, PaintKind::solid);
        QVERIFY(!object.stroke.isVisible());
        QCOMPARE(object.path.fillRule, Qt::OddEvenFill);
        QCOMPARE(object.path.bounds(), QRectF(8, 8, 16, 16));
        QCOMPARE(object.path.contours.size(), size_t(1));
        const Contour &contour = object.path.contours.front();
        QVERIFY(contour.closed);
        // The square collapses to its corners.
        QVERIFY(contour.nodes.size() <= 6);
        QVERIFY(std::abs(area(contour) - 256) < 8);
    }

    void ignoresWhitePaper()
    {
        QVERIFY(ImageTrace::trace(paper(16, 16)).empty());
        QVERIFY(ImageTrace::trace(QImage()).empty());
    }

    void transparentPixelsAreNotInk()
    {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QVERIFY(ImageTrace::trace(image).empty());
        QPainter(&image).fillRect(4, 4, 8, 8, Qt::black);
        QCOMPARE(ImageTrace::trace(image).size(), size_t(1));
    }

    void colourTraceSplitsRedAndBlue()
    {
        QImage image = paper(32, 16);
        {
            QPainter painter(&image);
            painter.fillRect(0, 0, 16, 16, QColor(200, 20, 20));
            painter.fillRect(16, 0, 16, 16, QColor(20, 20, 200));
        }
        ImageTrace::Options options;
        options.colors = 2;
        options.ignoreWhite = false;
        options.smoothness = 1;
        options.minimumArea = 4;
        const auto traced = ImageTrace::trace(image, options);
        QCOMPARE(traced.size(), size_t(2));
        int reds = 0, blues = 0;
        for (const VectorObject &object : traced) {
            const QColor c = object.fill.color;
            reds += c.red() > c.blue();
            blues += c.blue() > c.red();
        }
        QCOMPARE(reds, 1);
        QCOMPARE(blues, 1);
    }

    void ringKeepsItsHole()
    {
        QImage image = paper(24, 24);
        {
            QPainter painter(&image);
            painter.fillRect(4, 4, 16, 16, Qt::black);
            painter.fillRect(9, 9, 6, 6, Qt::white);
        }
        const auto traced = ImageTrace::trace(image);
        QCOMPARE(traced.size(), size_t(1));
        const VectorPath &path = traced.front().path;
        QVERIFY(path.contours.size() >= 2);
        std::vector<double> areas;
        for (const Contour &contour : path.contours)
            areas.push_back(area(contour));
        std::sort(areas.rbegin(), areas.rend());
        QVERIFY(areas[0] > areas[1] * 1.5);
        const QPainterPath painter = path.painterPath();
        QVERIFY(painter.contains(QPointF(6, 6)));
        QVERIFY(!painter.contains(QPointF(12, 12)));
    }

    void diagonalPixelsTraceCleanly()
    {
        // Two squares touching at one corner.
        QImage image = paper(20, 20);
        {
            QPainter painter(&image);
            painter.fillRect(2, 2, 8, 8, Qt::black);
            painter.fillRect(10, 10, 8, 8, Qt::black);
        }
        const auto traced = ImageTrace::trace(image);
        QCOMPARE(traced.size(), size_t(1));
        double total = 0;
        for (const Contour &contour : traced.front().path.contours)
            total += area(contour);
        QVERIFY(std::abs(total - 128) < 4);
    }

    void largeImagesComeBackAtFullSize()
    {
        QImage image = paper(4000, 1000);
        QPainter(&image).fillRect(1000, 200, 2000, 600, Qt::black);
        const auto traced = ImageTrace::trace(image);
        QCOMPARE(traced.size(), size_t(1));
        const QRectF bounds = traced.front().path.bounds();
        QVERIFY2(std::abs(bounds.left() - 1000) < 5 && std::abs(bounds.right() - 3000) < 5, qPrintable(QStringLiteral("%1 %2").arg(bounds.left()).arg(bounds.right())));
        QVERIFY(std::abs(bounds.top() - 200) < 5 && std::abs(bounds.bottom() - 800) < 5);
    }
};

QTEST_MAIN(ImageTraceTests)
#include "ImageTraceTests.moc"
