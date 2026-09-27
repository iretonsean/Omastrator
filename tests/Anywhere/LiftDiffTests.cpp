#include "Anywhere/LiftDiff.h"
#include <QTemporaryDir>
#include <QtTest>

// Apply to Source for lifted page vectors (docs/ANYWHERE.md): what changed
// since a lift landed, as edits to the elements it came from.
namespace {
struct Lifted {
    VectorDocument document = VectorDocument::blank(QSizeF(800, 600));
    QUuid card, background, title, button;
};

// A card (a box around a heading) and a button on its own, as Lift makes them.
Lifted lifted()
{
    Lifted made;
    VectorDocument &document = made.document;
    const QUuid layer = document.layers().front();
    VectorObject card;
    card.kind = ObjectKind::group;
    card.name = QStringLiteral("div.card");
    card.liftedFrom = QStringLiteral("#card");
    made.card = card.id;
    document.insert(card, layer);

    LiveRectangle shape;
    shape.rect = QRectF(20, 20, 300, 120);
    shape.radii = {8, 8, 8, 8};
    VectorObject background;
    background.name = QStringLiteral("Background");
    background.liftedFrom = QStringLiteral("#card");
    background.path = shape.path();
    background.shape = shape;
    background.fill = Paint::solid(QColor(0xf1, 0xf5, 0xf9));
    made.background = background.id;
    document.insert(background, made.card);

    VectorObject title;
    title.kind = ObjectKind::text;
    title.name = QStringLiteral("h1#title");
    title.liftedFrom = QStringLiteral("#title");
    title.text.text = QStringLiteral("Hello from\na plain site");
    title.text.family = QStringLiteral("Sans Serif");
    title.text.size = 32;
    title.fill = Paint::solid(QColor(0x0f, 0x17, 0x2a));
    title.transform = QTransform::fromTranslate(40, 70);
    made.title = title.id;
    document.insert(title, made.card);

    LiveRectangle buttonShape;
    buttonShape.rect = QRectF(20, 200, 120, 40);
    VectorObject button;
    button.name = QStringLiteral("button.buy");
    button.liftedFrom = QStringLiteral("button.buy");
    button.path = buttonShape.path();
    button.shape = buttonShape;
    button.fill = Paint::solid(QColor(0x33, 0x55, 0xff));
    made.button = button.id;
    document.insert(button, layer);
    return made;
}

QStringList described(const std::vector<LiftDiff::Change> &changes)
{
    QStringList list;
    for (const LiftDiff::Change &change : changes)
        list << (change.relative ? QStringLiteral("%1 %2 %3").arg(change.selector, change.property).arg(change.delta)
                                 : QStringLiteral("%1 %2 %3").arg(change.selector, change.property, change.value));
    return list;
}
}

class LiftDiffTests : public QObject {
    Q_OBJECT

private slots:
    void nothingChangedIsNothingToApply()
    {
        Lifted made = lifted();
        const QJsonObject baseline = LiftDiff::snapshot(made.document, made.document.layers().front());
        QVERIFY(baseline.contains(made.title.toString(QUuid::WithoutBraces)));
        QVERIFY(LiftDiff::changes(made.document, {made.card, made.button}, baseline, nullptr).empty());
        // Nothing from a lift that has no snapshot.
        made.document.find(made.title)->fill = Paint::solid(Qt::red);
        QVERIFY(LiftDiff::changes(made.document, {made.card}, {}, nullptr).empty());
    }

    void textColourTypeRadiusAndSize()
    {
        Lifted made = lifted();
        const QJsonObject baseline = LiftDiff::snapshot(made.document, made.document.layers().front());
        VectorDocument &document = made.document;
        document.find(made.title)->text.text = QStringLiteral("Hello from\na site that isn't plain");
        document.find(made.title)->fill = Paint::solid(QColor(0xe1, 0x1d, 0x48));
        document.find(made.title)->text.size = 40;
        document.find(made.background)->fill = Paint::solid(QColor(255, 255, 255, 128));
        LiveRectangle rounder = *document.find(made.background)->shape;
        rounder.radii = {16, 16, 16, 16};
        document.find(made.background)->shape = rounder;
        document.find(made.background)->path = rounder.path();
        // The button on its own grows: its size changes.
        LiveRectangle wider = *document.find(made.button)->shape;
        wider.rect.setWidth(160);
        wider.radii = {4, 4, 0, 0};
        document.find(made.button)->shape = wider;
        document.find(made.button)->path = wider.path();

        QStringList notes;
        const QStringList changes = described(LiftDiff::changes(document, {made.card, made.button}, baseline, &notes));
        QVERIFY2(changes.contains(QStringLiteral("#title text Hello from a site that isn't plain")), qPrintable(changes.join('\n')));
        QVERIFY(changes.contains(QStringLiteral("#title color #e11d48")));
        QVERIFY(changes.contains(QStringLiteral("#title font-size 40px")));
        QVERIFY(changes.contains(QStringLiteral("#card background-color rgba(255, 255, 255, 0.502)")));
        QVERIFY(changes.contains(QStringLiteral("#card border-radius 16px")));
        QVERIFY(changes.contains(QStringLiteral("button.buy width 40")));
        QVERIFY(changes.contains(QStringLiteral("button.buy border-radius 4px 4px 0px 0px")));
        // The card's box didn't move around its content, so no padding changes.
        QVERIFY(!changes.join(' ').contains(QLatin1String("padding")));
        QVERIFY(notes.isEmpty());
    }

    void aBoxMovedAroundItsContentIsSpacing()
    {
        Lifted made = lifted();
        const QJsonObject baseline = LiftDiff::snapshot(made.document, made.document.layers().front());
        VectorDocument &document = made.document;
        // 12 more above and 8 less on the right: padding, not size.
        LiveRectangle box = *document.find(made.background)->shape;
        box.rect = box.rect.adjusted(0, -12, -8, 0);
        document.find(made.background)->shape = box;
        document.find(made.background)->path = box.path();
        const QStringList changes = described(LiftDiff::changes(document, {made.card}, baseline, nullptr));
        QCOMPARE(changes, (QStringList{"#card padding-top 12", "#card padding-right -8"}));

        // Moving the whole card is layout, which is said rather than guessed.
        Lifted moved = lifted();
        const QJsonObject movedBaseline = LiftDiff::snapshot(moved.document, moved.document.layers().front());
        moved.document.transform(moved.card, QTransform::fromTranslate(30, 0));
        QStringList notes;
        QVERIFY(LiftDiff::changes(moved.document, {moved.card}, movedBaseline, &notes).empty());
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.front().contains(QLatin1String("#card was moved")));
    }

    void baselinesAreKept()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("lifted.json"));
        QVERIFY(LiftDiff::readBaselines(path).isEmpty());
        Lifted made = lifted();
        const QJsonObject baseline = LiftDiff::snapshot(made.document, made.card);
        QVERIFY(LiftDiff::writeBaselines(path, baseline).isEmpty());
        QCOMPARE(LiftDiff::readBaselines(path), baseline);
        QCOMPARE(LiftDiff::cssColor(QColor(0x12, 0x34, 0x56)), QStringLiteral("#123456"));
    }
};

QTEST_MAIN(LiftDiffTests)
#include "LiftDiffTests.moc"
