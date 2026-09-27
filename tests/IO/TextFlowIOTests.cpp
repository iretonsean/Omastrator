#include "Document/DocumentCodec.h"
#include "Document/PathOperations.h"
#include "Document/TextLayout.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include <QTest>

// SVG's <textPath> (P2-3) and the .omai round trip of the additive keys
// (onPath, threadNext, textWrap, hyphenate) it and P2-4/P2-5 add.
class TextFlowIOTests : public QObject {
    Q_OBJECT

private:
    static QUuid add(VectorDocument &document, VectorObject object, std::optional<QUuid> parent = std::nullopt)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent.value_or(document.layers().front()));
        return id;
    }

    static const VectorObject *find(const VectorDocument &document, const QString &name, ObjectKind kind)
    {
        for (const VectorObject &object : document.objects) {
            if (object.name == name && object.kind == kind)
                return &object;
        }
        return nullptr;
    }

private slots:
    void textPathRoundTrips()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Curved");
        text.text.text = QStringLiteral("Round the bend");
        text.text.size = 20;
        text.text.onPath = TextPath{Shapes::ellipse(QRectF(0, 0, 200, 200)), 0.2, false};
        text.fill = Paint::solid(Qt::black);
        text.stroke.paint = Paint::none();
        add(document, text);
        const QByteArray svg = SvgExporter::serialize(document);
        QVERIFY(svg.contains("<textPath"));
        QVERIFY(svg.contains("startOffset"));

        const VectorDocument read = SvgImporter::parse(svg);
        const VectorObject *after = find(read, QStringLiteral("Curved"), ObjectKind::text);
        QVERIFY(after);
        QVERIFY(after->text.onPath.has_value());
        QCOMPARE(QString(after->text.text).simplified(), QStringLiteral("Round the bend"));
        QVERIFY(std::abs(after->text.onPath->start - 0.2) < 0.02);
        // The path it follows is a circle too: its outline stays roughly the same shape.
        const QRectF before = text.text.onPath->path.bounds(), roundTripped = after->text.onPath->path.bounds();
        QVERIFY(std::abs(before.width() - roundTripped.width()) < 1);
        QVERIFY(std::abs(before.height() - roundTripped.height()) < 1);
        // Outlines (Create Outlines' path) still work through the same glyph geometry.
        SvgExporter::Options outlines;
        outlines.textAsOutlines = true;
        const QByteArray asOutlines = SvgExporter::serialize(document, outlines);
        QVERIFY(!QString::fromUtf8(asOutlines).contains(QStringLiteral("<text")));
    }

    void omaiRoundTripsTheNewKeys()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        VectorObject path;
        path.kind = ObjectKind::path;
        path.path = Shapes::rectangle(QRectF(20, 20, 60, 60));
        path.textWrap = 5;
        const QUuid pathID = add(document, path);

        VectorObject onPath;
        onPath.kind = ObjectKind::text;
        onPath.text.text = QStringLiteral("Bend");
        onPath.text.onPath = TextPath{Shapes::ellipse(QRectF(0, 0, 100, 100)), 0.3, true};
        onPath.text.hyphenate = false;
        add(document, onPath);

        VectorObject head;
        head.kind = ObjectKind::text;
        head.text.text = QStringLiteral("Overflowing story text");
        head.text.area = QSizeF(40, 20);
        head.text.hyphenate = true;
        head.text.hyphenMinWord = 5;
        head.text.hyphenMinBefore = 3;
        head.text.hyphenMinAfter = 2;
        VectorObject follower;
        follower.kind = ObjectKind::text;
        follower.text.area = QSizeF(80, 80);
        follower.transform = QTransform::fromTranslate(100, 0);
        head.text.threadNext = follower.id;
        const QUuid headID = add(document, head);
        const QUuid followerID = add(document, follower);
        document.reflowText();

        const QJsonObject json = DocumentCodec::encode(document);
        // Additive keys, no version bump: still version 4.
        QCOMPARE(json["version"].toInt(), 4);
        const VectorDocument back = DocumentCodec::decode(json);

        QCOMPARE(back.find(pathID)->textWrap, path.textWrap);
        const TextContent &onPathBack = back.find(onPath.id)->text;
        QVERIFY(onPathBack.onPath.has_value());
        QCOMPARE(onPathBack.onPath->start, onPath.text.onPath->start);
        QCOMPARE(onPathBack.onPath->flipped, onPath.text.onPath->flipped);
        QCOMPARE(onPathBack.onPath->path, onPath.text.onPath->path);

        const TextContent &headBack = back.find(headID)->text;
        QCOMPARE(headBack.threadNext, followerID);
        QCOMPARE(headBack.hyphenate, true);
        QCOMPARE(headBack.hyphenMinWord, 5);
        QCOMPARE(headBack.hyphenMinBefore, 3);
        QCOMPARE(headBack.hyphenMinAfter, 2);
        // reflowText() ran again on decode: the thread's still there.
        QVERIFY(headBack.flow.story);
        QCOMPARE(back.find(followerID)->text.flow.frame, 1);
    }
};

QTEST_MAIN(TextFlowIOTests)
#include "TextFlowIOTests.moc"
