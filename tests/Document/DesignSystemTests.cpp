#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QJsonArray>
#include <QTest>

// Design systems in the document (docs/DESIGN-SYSTEMS.md): tokens and their
// references, modes, components with instances, overrides, variants and
// detach, and the .omai version 4 round trip with the version 3 migration.
namespace {
VectorObject rectangle(const QRectF &rect, const QColor &color = Qt::black, const QString &name = QStringLiteral("Background"))
{
    LiveRectangle shape;
    shape.rect = rect;
    VectorObject object;
    object.kind = ObjectKind::path;
    object.name = name;
    object.fill = Paint::solid(color);
    EditorSession::reshape(object, shape);
    return object;
}

QUuid addText(EditorSession &session, const QString &words, QPointF at)
{
    VectorObject object = session.textObject(at, words);
    object.name = QStringLiteral("Label");
    return session.addObject(object, QStringLiteral("Type"));
}

// A button component: a rectangle and a label, grouped and made a component.
QUuid makeButton(EditorSession &session)
{
    const QUuid background = session.addObject(rectangle(QRectF(10, 10, 120, 40), QColor("#0a84ff")), QStringLiteral("Draw"));
    const QUuid label = addText(session, QStringLiteral("Buy"), {20, 36});
    session.select({background, label});
    return *session.makeComponent(QStringLiteral("Button"));
}

const VectorObject *childNamed(const VectorDocument &document, const QUuid &parent, const QString &name)
{
    for (const QUuid &child : document.children(parent)) {
        if (document.find(child)->name == name)
            return document.find(child);
    }
    return nullptr;
}
}

class DesignSystemTests : public QObject {
    Q_OBJECT

private slots:
    void aColourTokenRecoloursEveryUseInOneStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), QColor("#ff0000")));
        const QUuid a = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        const QUuid b = session.addObject(rectangle(QRectF(60, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({a, b});
        QVERIFY(session.applyToken(brand).isEmpty());
        QCOMPARE(session.document()->find(a)->fill.color, QColor("#ff0000"));
        QCOMPARE(session.document()->find(b)->fill.token, brand);
        QCOMPARE(session.linkedToken(QStringLiteral("fill")), brand);
        TokenValue value;
        value.color = QColor("#00ff00");
        session.setTokenValue(brand, value);
        QCOMPARE(session.undoName(), QStringLiteral("Edit Token"));
        QCOMPARE(session.document()->find(a)->fill.color, QColor("#00ff00"));
        QCOMPARE(session.document()->find(b)->fill.color, QColor("#00ff00"));
        session.undo();
        QCOMPARE(session.document()->find(a)->fill.color, QColor("#ff0000"));
        QCOMPARE(session.document()->find(b)->fill.color, QColor("#ff0000"));
    }

    void aColourChangedByHandLeavesItsToken()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const QUuid a = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({a});
        session.applyToken(brand);
        Paint blue = session.document()->find(a)->fill;
        blue.color = Qt::blue;
        session.setFillOfSelection(blue);
        QVERIFY(session.document()->find(a)->fill.token.isEmpty());
        QCOMPARE(session.document()->find(a)->fill.color, QColor(Qt::blue));
    }

    void scalarTokensSetRadiiStrokesTypeAndGaps()
    {
        EditorSession session;
        session.createDocument({600, 300});
        const QString radius = session.addToken(DesignToken::number(TokenKind::radius, QStringLiteral("radius/md"), 8));
        const QString stroke = session.addToken(DesignToken::number(TokenKind::spacing, QStringLiteral("space/1"), 3));
        const QString gap = session.addToken(DesignToken::number(TokenKind::spacing, QStringLiteral("space/4"), 16));
        TypeValue type;
        type.family = QStringLiteral("Sans Serif");
        type.size = 30;
        type.lineHeight = 36;
        type.tracking = 20;
        const QString heading = session.addToken(DesignToken::typography(QStringLiteral("text/heading"), type));
        const QUuid box = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({box});
        QVERIFY(session.applyToken(radius).isEmpty());
        QCOMPARE(session.document()->find(box)->shape->radii[0], 8.0);
        QVERIFY(session.applyToken(stroke, TokenRef::strokeWidth).isEmpty());
        QCOMPARE(session.document()->find(box)->stroke.width, 3.0);
        const QUuid text = addText(session, QStringLiteral("Hello"), {10, 200});
        session.select({text});
        QVERIFY(session.applyToken(heading).isEmpty());
        QCOMPARE(session.document()->find(text)->text.size, 30.0);
        QCOMPARE(session.document()->find(text)->text.leading, std::optional<double>(36));
        QCOMPARE(session.document()->find(text)->text.tracking, 20.0);
        // A group spaced by a token: its children sit 16 apart, and follow the token.
        const QUuid one = session.addObject(rectangle(QRectF(100, 100, 40, 40)), QStringLiteral("Draw"));
        const QUuid two = session.addObject(rectangle(QRectF(300, 100, 40, 40)), QStringLiteral("Draw"));
        session.select({one, two});
        session.groupSelection();
        QVERIFY(session.applyToken(gap).isEmpty());
        QCOMPARE(session.document()->bounds(two).left(), 156.0);
        TokenValue wider;
        wider.number = 24;
        session.setTokenValue(gap, wider);
        QCOMPARE(session.document()->bounds(two).left(), 164.0);
        TokenValue rounder;
        rounder.number = 12;
        session.setTokenValue(radius, rounder);
        QCOMPARE(session.document()->find(box)->shape->radii[2], 12.0);
        // A shadow token has nothing to set on an object.
        const QString shadow = session.addToken(DesignToken::shadowToken(QStringLiteral("shadow/sm"), {}));
        session.select({box});
        QVERIFY(!session.applyToken(shadow).isEmpty());
    }

    void aTextStyleFollowsItsTypeToken()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid text = addText(session, QStringLiteral("Body"), {10, 100});
        const QUuid style = session.newTextStyle(TextStyleKind::paragraph, QStringLiteral("Body"));
        TypeValue type;
        type.size = 18;
        type.lineHeight = 28;
        const QString body = session.addToken(DesignToken::typography(QStringLiteral("text/body"), type));
        session.linkTextStyle(style, body);
        QCOMPARE(session.textStyle(style)->character.size, 18.0);
        QCOMPARE(session.document()->find(text)->text.size, 18.0);
        type.size = 20;
        TokenValue value;
        value.type = type;
        session.setTokenValue(body, value);
        QCOMPARE(session.textStyle(style)->character.size, 20.0);
        QCOMPARE(session.document()->find(text)->text.size, 20.0);
        QCOMPARE(session.textStyle(style)->typeToken, body);
    }

    void modesSwitchEveryUseInOneStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QString surface = session.addToken(DesignToken::color(QStringLiteral("color/surface"), Qt::white));
        const QUuid box = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({box});
        session.applyToken(surface);
        session.addTokenMode(QStringLiteral("dark"));
        QCOMPARE(session.document()->tokenModes, QStringList({QStringLiteral("light"), QStringLiteral("dark")}));
        TokenValue dark;
        dark.color = QColor("#111111");
        session.setTokenValue(surface, dark, QStringLiteral("dark"));
        QCOMPARE(session.document()->find(box)->fill.color, QColor(Qt::white));
        session.setTokenMode(QStringLiteral("dark"));
        QCOMPARE(session.undoName(), QStringLiteral("Switch to Dark Mode"));
        QCOMPARE(session.document()->find(box)->fill.color, QColor("#111111"));
        QCOMPARE(session.document()->find(box)->fill.token, surface);
        session.undo();
        QCOMPARE(session.document()->find(box)->fill.color, QColor(Qt::white));
    }

    void deletingATokenKeepsTheLook()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const QUuid box = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({box});
        session.applyToken(brand);
        session.deleteToken(brand);
        QVERIFY(session.document()->find(box)->fill.token.isEmpty());
        QCOMPARE(session.document()->find(box)->fill.color, QColor(Qt::red));
    }

    void mergingTokensByNameKeepsIdsSoUsesFollow()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const QUuid box = session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        session.select({box});
        session.applyToken(brand);
        const int changed = session.mergeTokens({DesignToken::color(QStringLiteral("color/brand"), Qt::green),
                                                 DesignToken::number(TokenKind::spacing, QStringLiteral("space/2"), 8)},
                                                QStringLiteral("Pull Design System"));
        QCOMPARE(changed, 2);
        QCOMPARE(session.undoName(), QStringLiteral("Pull Design System"));
        QCOMPARE(session.document()->find(box)->fill.color, QColor(Qt::green));
        QCOMPARE(int(session.document()->tokens.size()), 2);
    }

    void instancesFollowTheirComponentInOneStep()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QUuid master = makeButton(session);
        QCOMPARE(session.undoName(), QStringLiteral("Make Component"));
        const QUuid first = session.placeInstance(master, QPointF(300, 300));
        const QUuid second = session.placeInstance(master, QPointF(500, 300));
        QCOMPARE(Components::instancesOf(*session.document(), master).size(), size_t(2));
        const VectorDocument &document = *session.document();
        QCOMPARE(document.children(first).size(), size_t(2));
        QCOMPARE(document.bounds(first).center(), QPointF(300, 300));
        // Recolouring the component's background is one step, and both instances follow.
        const QUuid background = document.children(master).front();
        session.select({background});
        session.setFillOfSelection(Paint::solid(Qt::red));
        QCOMPARE(childNamed(*session.document(), first, QStringLiteral("Background"))->fill.color, QColor(Qt::red));
        QCOMPARE(childNamed(*session.document(), second, QStringLiteral("Background"))->fill.color, QColor(Qt::red));
        session.undo();
        QCOMPARE(childNamed(*session.document(), first, QStringLiteral("Background"))->fill.color, QColor("#0a84ff"));
        // Moving an instance keeps its children in place relative to it.
        session.select({first});
        session.moveSelection({10, 0});
        QCOMPARE(session.document()->bounds(first).center(), QPointF(310, 300));
        // Moving the component doesn't move its instances.
        session.select({master});
        session.moveSelection({0, 50});
        QCOMPARE(session.document()->bounds(first).center(), QPointF(310, 300));
    }

    void overridesSurviveComponentEdits()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QUuid master = makeButton(session);
        const QUuid instance = session.placeInstance(master, QPointF(300, 300));
        // Changing the instance's label and background is an override.
        const QUuid label = childNamed(*session.document(), instance, QStringLiteral("Label"))->id;
        session.updateObject([&] {
            VectorObject edited = *session.document()->find(label);
            edited.text.replace(0, int(edited.text.text.size()), QStringLiteral("Sold out"));
            return edited;
        }(), QStringLiteral("Type"));
        const QUuid background = childNamed(*session.document(), instance, QStringLiteral("Background"))->id;
        session.select({background});
        session.setFillOfSelection(Paint::solid(Qt::gray));
        const InstanceInfo &info = *session.document()->find(instance)->instance;
        QCOMPARE(info.overrides.at(QStringLiteral("Label")).text, std::optional<QString>(QStringLiteral("Sold out")));
        QVERIFY(info.overrides.at(QStringLiteral("Background")).fill.has_value());
        // The component's label moves; the instance keeps its words and colour.
        const QUuid masterLabel = childNamed(*session.document(), master, QStringLiteral("Label"))->id;
        session.select({masterLabel});
        session.moveSelection({5, 0});
        session.select({childNamed(*session.document(), master, QStringLiteral("Background"))->id});
        session.setFillOfSelection(Paint::solid(Qt::green));
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Label"))->text.text, QStringLiteral("Sold out"));
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Background"))->fill.color, QColor(Qt::gray));
        // Hiding a layer of the instance is an override too; resetting clears them all.
        session.select({instance});
        session.resetOverrides();
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Label"))->text.text, QStringLiteral("Buy"));
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Background"))->fill.color, QColor(Qt::green));
        session.setVisible(childNamed(*session.document(), instance, QStringLiteral("Label"))->id, false);
        QCOMPARE(session.document()->find(instance)->instance->overrides.at(QStringLiteral("Label")).visible, std::optional<bool>(false));
    }

    void variantsSwapAndKeepOverrides()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QUuid master = makeButton(session);
        const QUuid large = *session.addVariant(master, QStringLiteral("size"), QStringLiteral("lg"));
        QCOMPARE(session.undoName(), QStringLiteral("Add Variant"));
        QCOMPARE(session.document()->find(master)->component->variant.at(QStringLiteral("size")), QStringLiteral("default"));
        // Make the large one larger.
        session.select({childNamed(*session.document(), large, QStringLiteral("Background"))->id});
        session.scaleSelection(1.5, 1.5);
        const auto properties = Components::properties(*session.document(), QStringLiteral("Button"));
        QCOMPARE(properties.size(), size_t(1));
        QCOMPARE(properties.front().second, QStringList({QStringLiteral("default"), QStringLiteral("lg")}));
        const QUuid instance = session.placeInstance(master, QPointF(300, 400));
        const QUuid label = childNamed(*session.document(), instance, QStringLiteral("Label"))->id;
        session.updateObject([&] {
            VectorObject edited = *session.document()->find(label);
            edited.text.replace(0, int(edited.text.text.size()), QStringLiteral("Go"));
            return edited;
        }(), QStringLiteral("Type"));
        session.select({instance});
        QVERIFY(session.swapVariant(QStringLiteral("size"), QStringLiteral("lg")).isEmpty());
        QCOMPARE(session.document()->find(instance)->instance->master, large);
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Label"))->text.text, QStringLiteral("Go"));
        QVERIFY(session.document()->bounds(childNamed(*session.document(), instance, QStringLiteral("Background"))->id).width() > 170);
        QVERIFY(!session.swapVariant(QStringLiteral("size"), QStringLiteral("xl")).isEmpty());
    }

    void detachingKeepsTheLookAndStopsFollowing()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QUuid master = makeButton(session);
        const QUuid instance = session.placeInstance(master, QPointF(300, 300));
        session.detachInstances();
        QCOMPARE(session.undoName(), QStringLiteral("Detach Instance"));
        QVERIFY(!session.document()->find(instance)->instance);
        QCOMPARE(session.document()->children(instance).size(), size_t(2));
        session.select({childNamed(*session.document(), master, QStringLiteral("Background"))->id});
        session.setFillOfSelection(Paint::solid(Qt::red));
        QCOMPARE(childNamed(*session.document(), instance, QStringLiteral("Background"))->fill.color, QColor("#0a84ff"));
    }

    void duplicatingAComponentMakesAnInstance()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QUuid master = makeButton(session);
        session.select({master});
        session.duplicateSelection();
        const QUuid copy = session.selection().front();
        QVERIFY(!session.document()->find(copy)->component);
        QCOMPARE(session.document()->find(copy)->instance->master, master);
        // Deleting the component leaves its instances as plain groups.
        session.select({master});
        session.deleteSelection();
        QVERIFY(!session.document()->find(copy)->instance);
        QCOMPARE(session.document()->children(copy).size(), size_t(2));
    }

    void componentsFromALibraryArePlacedOnce()
    {
        EditorSession library;
        library.createDocument({800, 600});
        const QUuid master = makeButton(library);
        const std::vector<VectorObject> objects = [&] {
            std::vector<VectorObject> all{*library.document()->find(master)};
            for (const QUuid &id : library.document()->descendants(master))
                all.push_back(*library.document()->find(id));
            return all;
        }();
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid first = session.placeFromLibrary(objects, QStringLiteral("Button"), {});
        QCOMPARE(session.undoName(), QStringLiteral("Place Component"));
        QVERIFY(session.document()->find(master));
        QCOMPARE(session.document()->bounds(first).center(), QPointF(200, 150));
        QVERIFY(session.document()->bounds(master).left() >= 400);
        session.placeFromLibrary(objects, QStringLiteral("Button"), {}, QPointF(100, 100));
        QCOMPARE(Components::masters(*session.document()).size(), size_t(1));
        QCOMPARE(Components::instancesOf(*session.document(), master).size(), size_t(2));
    }

    void version4RoundTripsTheSystem()
    {
        EditorSession session;
        session.createDocument({800, 600});
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        session.addTokenMode(QStringLiteral("dark"));
        const QUuid master = makeButton(session);
        session.select({childNamed(*session.document(), master, QStringLiteral("Background"))->id});
        session.applyToken(brand);
        session.addVariant(master, QStringLiteral("state"), QStringLiteral("hover"));
        const QUuid instance = session.placeInstance(master, QPointF(300, 300));
        session.select({childNamed(*session.document(), instance, QStringLiteral("Label"))->id});
        session.setVisible(session.selection().front(), false);
        const QJsonObject json = DocumentCodec::encode(*session.document());
        QCOMPARE(json["version"].toInt(), 5);
        VectorDocument decoded = DocumentCodec::decode(json);
        Components::sync(decoded);
        QCOMPARE(decoded.tokens, session.document()->tokens);
        QCOMPARE(decoded.tokenModes, session.document()->tokenModes);
        QCOMPARE(decoded.find(master)->component, session.document()->find(master)->component);
        QCOMPARE(decoded.find(instance)->instance->overrides, session.document()->find(instance)->instance->overrides);
        QCOMPARE(decoded.find(instance)->instance->placement, session.document()->find(instance)->instance->placement);
        QCOMPARE(childNamed(decoded, master, QStringLiteral("Background"))->fill.token, brand);
        EditorSession reopened;
        reopened.loadDocument(decoded);
        QVERIFY(!childNamed(*reopened.document(), instance, QStringLiteral("Label"))->isVisible);
    }

    void version3FilesOpenWithNoSystem()
    {
        EditorSession session;
        session.createDocument({400, 300});
        session.addObject(rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("Draw"));
        QJsonObject json = DocumentCodec::encode(*session.document());
        json["version"] = 3;
        const VectorDocument decoded = DocumentCodec::decode(json);
        QVERIFY(decoded.tokens.empty());
        QVERIFY(decoded.tokenModes.isEmpty());
        QCOMPARE(decoded.objects.size(), session.document()->objects.size());
        // A newer file is refused.
        json["version"] = 6;
        QVERIFY_THROWS_EXCEPTION(CodecError, DocumentCodec::decode(json));
    }

    void shadowsReadAndWriteCss()
    {
        const auto shadow = ShadowValue::fromCss(QStringLiteral("0 4px 6px -1px rgb(0 0 0 / 0.1), 0 2px 4px -2px rgb(0 0 0 / 0.1)"));
        QVERIFY(shadow);
        QCOMPARE(shadow->y, 4.0);
        QCOMPARE(shadow->blur, 6.0);
        QCOMPARE(shadow->spread, -1.0);
        QCOMPARE(shadow->color.alpha(), 26);
        const auto again = ShadowValue::fromCss(shadow->css());
        QCOMPARE(again->y, shadow->y);
        QCOMPARE(again->color.rgb(), shadow->color.rgb());
    }
};

QTEST_MAIN(DesignSystemTests)
#include "DesignSystemTests.moc"
