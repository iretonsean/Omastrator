#include "Document/EditorSession.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include <QTest>

// A compound path's fill rule, set in Properties, survives SVG export and import.
class FillRuleTests : public QObject {
    Q_OBJECT

private:
    static Qt::FillRule roundTrip(Qt::FillRule rule)
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid outer = session.addPath(Shapes::rectangle({10, 10, 180, 180}), QStringLiteral("Outer"));
        const QUuid inner = session.addPath(Shapes::rectangle({60, 60, 80, 80}), QStringLiteral("Inner"));
        session.select({outer, inner});
        session.makeCompoundPath();
        session.setFillRuleOfSelection(rule);
        const VectorDocument imported = SvgImporter::parse(SvgExporter::serialize(*session.document()));
        for (const VectorObject &object : imported.objects) {
            if (object.kind == ObjectKind::path && object.path.contours.size() == 2)
                return object.path.fillRule;
        }
        return Qt::FillRule(-1);
    }

private slots:
    void evenOddRoundTrips() { QCOMPARE(roundTrip(Qt::OddEvenFill), Qt::OddEvenFill); }
    void nonZeroRoundTrips() { QCOMPARE(roundTrip(Qt::WindingFill), Qt::WindingFill); }
};

QTEST_MAIN(FillRuleTests)
#include "FillRuleTests.moc"
