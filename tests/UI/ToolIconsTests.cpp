#include "UI/PanelIcons.h"
#include "UI/ToolIcons.h"
#include <QSet>
#include <QtTest>

// Drawn icons: inked in one colour, each unlike the others.
namespace {
int inked(const QImage &image, QRgb colour)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) > 0) {
                ++count;
                // Premultiplied edges keep the hue.
                if (qAlpha(pixel) == 255 && pixel != colour)
                    return -1;
            }
        }
    }
    return count;
}
}

class ToolIconsTests : public QObject {
    Q_OBJECT
private slots:
    void everyToolHasItsOwnIcon();
    void iconsScaleWithTheRatio();
    void everyPanelIconHasInk();
};

void ToolIconsTests::everyToolHasItsOwnIcon()
{
    QSet<QByteArray> seen;
    for (const Tool tool : allTools) {
        const QImage image = ToolIcons::pixmap(tool, 18, QColor(0x33, 0x66, 0x99), 1).toImage().convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(image.size(), QSize(18, 18));
        const int ink = inked(image, qRgb(0x33, 0x66, 0x99));
        QVERIFY2(ink > 12, qPrintable(rawValue(tool)));
        // Nothing fills the whole square.
        QVERIFY2(ink < 18 * 18 * 3 / 4, qPrintable(rawValue(tool)));
        const QByteArray bytes(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
        QVERIFY2(!seen.contains(bytes), qPrintable(rawValue(tool)));
        seen.insert(bytes);
    }
    QCOMPARE(seen.size(), int(allTools.size()));
}

void ToolIconsTests::iconsScaleWithTheRatio()
{
    const QPixmap sharp = ToolIcons::pixmap(Tool::pen, 18, Qt::white, 2);
    QCOMPARE(sharp.size(), QSize(36, 36));
    QCOMPARE(sharp.devicePixelRatio(), 2.0);
    // The selection arrow is solid; the direct one hollow.
    const QImage solid = ToolIcons::pixmap(Tool::select, 18, Qt::black, 4).toImage();
    const QImage hollow = ToolIcons::pixmap(Tool::directSelect, 18, Qt::black, 4).toImage();
    QCOMPARE(qAlpha(solid.pixel(6 * 4, 9 * 4)), 255);
    QCOMPARE(qAlpha(hollow.pixel(6 * 4, 9 * 4)), 0);
}

void ToolIconsTests::everyPanelIconHasInk()
{
    for (int icon = int(PanelIcon::eye); icon <= int(PanelIcon::tracking); ++icon) {
        const QImage image = PanelIcons::pixmap(PanelIcon(icon), 18, Qt::red, 1).toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(inked(image, qRgb(255, 0, 0)) > 8, qPrintable(QString::number(icon)));
    }
    // Unite fills both squares; intersect only their overlap.
    const QImage unite = PanelIcons::pixmap(PanelIcon::unite, 18, Qt::black, 1).toImage();
    const QImage intersect = PanelIcons::pixmap(PanelIcon::intersect, 18, Qt::black, 1).toImage();
    QCOMPARE(qAlpha(unite.pixel(4, 4)), 255);
    QCOMPARE(qAlpha(intersect.pixel(4, 4)), 0);
    QCOMPARE(qAlpha(intersect.pixel(9, 9)), 255);
}

QTEST_MAIN(ToolIconsTests)
#include "ToolIconsTests.moc"
