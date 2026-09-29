#include "Document/DocumentCodec.h"
#include "Document/VectorDocument.h"
#include <QJsonDocument>
#include <QTest>

// A Browser View in the file format (docs/BROWSER-VIEW.md, section 1).
namespace {
QImage picture(QSize size, QColor color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

struct Fixture {
    VectorDocument document = VectorDocument::blank({2000, 1500});
    QUuid frame;

    Fixture()
    {
        VectorObject view = VectorObject::frame({10, 20, 400, 300}, QStringLiteral("Site"));
        view.browser = BrowserView{QUrl(QStringLiteral("https://example.com/pricing?a=1")), QPointF(0, 120), picture({400, 300}, Qt::red)};
        frame = view.id;
        document.insert(view, document.layers().front());
        VectorObject note = VectorObject::frame({20, 30, 50, 50}, QStringLiteral("Note"));
        note.layout.previewRule = PreviewRule::fixed;
        document.insert(note, frame);
    }
};

QJsonObject find(const QJsonArray &objects, const QString &name)
{
    for (const QJsonValue &value : objects) {
        if (value.toObject()["name"].toString() == name)
            return value.toObject();
    }
    return {};
}
}

class DocumentCodecTests : public QObject {
    Q_OBJECT

private slots:
    void aBrowserViewRoundTrips()
    {
        Fixture fixture;
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(fixture.document));
        const VectorObject *view = back.find(fixture.frame);
        QVERIFY(view);
        QVERIFY(view->browser);
        QCOMPARE(view->browser->url, QUrl(QStringLiteral("https://example.com/pricing?a=1")));
        QCOMPARE(view->browser->scroll, QPointF(0, 120));
        QCOMPARE(view->browser->picture.size(), QSize(400, 300));
        QCOMPARE(view->browser->picture.pixelColor(5, 5), QColor(Qt::red));
        QCOMPARE(back, fixture.document);
    }

    void thePreviewRuleRoundTripsAndDefaultsAreNotWritten()
    {
        Fixture fixture;
        const QJsonArray objects = DocumentCodec::encode(fixture.document)["objects"].toArray();
        QCOMPARE(find(objects, QStringLiteral("Note"))["layout"].toObject()["preview"].toString(), QStringLiteral("fixed"));
        QVERIFY(!find(objects, QStringLiteral("Site"))["layout"].toObject().contains("preview"));
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(fixture.document));
        const auto children = back.children(fixture.frame);
        QCOMPARE(children.size(), size_t(1));
        QCOMPARE(back.find(children.front())->layout.previewRule, PreviewRule::fixed);
    }

    void thePictureIsALockedAbsoluteImageChildAndComesBackOut()
    {
        Fixture fixture;
        const QJsonArray objects = DocumentCodec::encode(fixture.document)["objects"].toArray();
        const QJsonObject child = find(objects, QStringLiteral("Last picture of example.com"));
        QVERIFY(!child.isEmpty());
        QCOMPARE(child["kind"].toString(), QStringLiteral("image"));
        QCOMPARE(child["parent"].toString(), fixture.frame.toString(QUuid::WithoutBraces));
        QVERIFY(child["locked"].toBool());
        QVERIFY(child["layout"].toObject()["absolute"].toBool());
        QVERIFY(child["browserPicture"].toBool());
        QVERIFY(!child["image"].toString().isEmpty());
        // It's the frame's bottom child, right after the frame.
        int frameAt = -1, childAt = -1;
        for (int i = 0; i < objects.size(); ++i) {
            frameAt = objects[i].toObject()["name"] == "Site" ? i : frameAt;
            childAt = objects[i].toObject()["name"] == child["name"] ? i : childAt;
        }
        QCOMPARE(childAt, frameAt + 1);
        // Read back, it isn't in the tree.
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(fixture.document));
        QCOMPARE(back.children(fixture.frame).size(), size_t(1));
        QCOMPARE(back.objects.size(), fixture.document.objects.size());
    }

    void aLongPictureIsScaledDownToTheStoredLimit()
    {
        Fixture fixture;
        fixture.document.find(fixture.frame)->browser->picture = picture({4000, 1000}, Qt::blue);
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(fixture.document));
        QCOMPARE(back.find(fixture.frame)->browser->picture.width(), 2048);
    }

    void anOlderBuildsViewOfTheFileIsAFrameWithAnImageChild()
    {
        Fixture fixture;
        QJsonObject json = DocumentCodec::encode(fixture.document);
        QJsonArray objects = json["objects"].toArray();
        for (qsizetype i = 0; i < objects.size(); ++i) {
            QJsonObject object = objects[i].toObject();
            object.remove("browserPicture");
            objects[i] = object;
        }
        json["objects"] = objects;
        const VectorDocument back = DocumentCodec::decode(json);
        const auto children = back.children(fixture.frame);
        QCOMPARE(children.size(), size_t(2));
        QCOMPARE(back.find(children.front())->kind, ObjectKind::image);
        QVERIFY(back.find(fixture.frame)->browser);
        QVERIFY(back.find(fixture.frame)->browser->picture.isNull());
    }

    void theClipboardAndTheAgentLeaveThePictureOut()
    {
        Fixture fixture;
        const QJsonArray objects = DocumentCodec::encode(fixture.document.objects, false);
        QVERIFY(find(objects, QStringLiteral("Last picture of example.com")).isEmpty());
        QVERIFY(find(objects, QStringLiteral("Site")).contains("browserView"));
        const QJsonObject json = DocumentCodec::encode(fixture.document, false);
        QVERIFY(find(json["objects"].toArray(), QStringLiteral("Last picture of example.com")).isEmpty());
        const auto back = DocumentCodec::decodeObjects(objects);
        const auto frame = std::find_if(back.begin(), back.end(), [&](const VectorObject &o) { return o.id == fixture.frame; });
        QVERIFY(frame != back.end());
        QVERIFY(frame->browser->picture.isNull());
        QCOMPARE(frame->browser->url.host(), QStringLiteral("example.com"));
    }

    void aBrowserViewKeyOnAnythingButAFrameIsIgnored()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        QJsonObject json = DocumentCodec::encode(document);
        QJsonArray objects = json["objects"].toArray();
        QJsonObject layer = objects[0].toObject();
        layer["browserView"] = QJsonObject{{"url", "https://example.com"}};
        objects[0] = layer;
        json["objects"] = objects;
        QVERIFY(!DocumentCodec::decode(json).objects.front().browser);
    }

    void aBadUrlReadsAsNoPageYet()
    {
        Fixture fixture;
        QJsonObject json = DocumentCodec::encode(fixture.document);
        QJsonArray objects = json["objects"].toArray();
        for (qsizetype i = 0; i < objects.size(); ++i) {
            QJsonObject object = objects[i].toObject();
            if (object.contains("browserView"))
                object["browserView"] = QJsonObject{{"url", "http://exa mple.com/"}};
            objects[i] = object;
        }
        json["objects"] = objects;
        const VectorDocument back = DocumentCodec::decode(json);
        QVERIFY(back.find(fixture.frame)->browser->url.isEmpty());
    }

    void onlyHttpAndHttpsPagesAreRead_data()
    {
        QTest::addColumn<QString>("url");
        for (const char *url : {"file:///home/someone/.ssh/config", "FILE:///etc/passwd", "javascript:alert(1)", "data:text/html,hi",
                                "chrome://settings", "view-source:https://example.com/", "about:srcdoc", "https:///nohost", "ftp://example.com/"})
            QTest::newRow(url) << QString::fromLatin1(url);
    }

    void onlyHttpAndHttpsPagesAreRead()
    {
        QFETCH(QString, url);
        Fixture fixture;
        // The file's own frame and a paste (decodeObjects, which the clipboard and the agent's update_object read through).
        QJsonObject object = DocumentCodec::encode(*fixture.document.find(fixture.frame));
        object["browserView"] = QJsonObject{{"url", url}};
        QVERIFY(DocumentCodec::decodeObject(object).browser->url.isEmpty());
        const auto pasted = DocumentCodec::decodeObjects(QJsonArray{object});
        QCOMPARE(pasted.size(), size_t(1));
        QVERIFY(pasted.front().browser->url.isEmpty());
    }

    void anUpperCaseSchemeOfAWebPageStays()
    {
        Fixture fixture;
        QJsonObject object = DocumentCodec::encode(*fixture.document.find(fixture.frame));
        object["browserView"] = QJsonObject{{"url", "HTTPS://example.com/a"}};
        QVERIFY(!DocumentCodec::decodeObject(object).browser->url.isEmpty());
    }
};

QTEST_MAIN(DocumentCodecTests)
#include "DocumentCodecTests.moc"
