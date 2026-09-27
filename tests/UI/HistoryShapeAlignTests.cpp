#include "Document/PathOperations.h"
#include "UI/HistoryPanel.h"
#include "UI/IsolationBar.h"
#include "UI/NumberField.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/PropertiesPanel.h"
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolButton>
#include <QtTest>

// The panels for these: History, the Shape section, Align's key object and
// spacing, the isolation bar, and their menu entries.
namespace {
void type(QWidget &panel, const QString &field, const QString &text)
{
    auto *edit = panel.findChild<QLineEdit *>(field);
    QVERIFY2(edit, qPrintable(field));
    edit->setText(text);
    QTest::keyClick(edit, Qt::Key_Return);
}

QUuid box(EditorSession &session, const QRectF &rect)
{
    return session.addPath(Shapes::rectangle(rect), QStringLiteral("Box"));
}

QUuid liveBox(EditorSession &session, const QRectF &rect)
{
    LiveRectangle shape;
    shape.rect = rect;
    VectorObject object = session.pathObject({}, QStringLiteral("Rectangle"));
    EditorSession::reshape(object, shape);
    return session.addObject(object, QStringLiteral("Draw Rectangle"));
}

// Crumbs are replaced later, when the one clicked is done asking.
void settle()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
}

class HistoryShapeAlignTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cleanup() { QSettings().clear(); }

    void historyRowsUndoAndRedo()
    {
        EditorSession session;
        session.createDocument({300, 200});
        for (int index = 0; index < 5; ++index)
            box(session, QRectF(10 * index, 10, 5, 5));
        HistoryPanel panel(session);
        QListWidget *list = panel.list();
        QCOMPARE(list->count(), 6);
        QCOMPARE(list->item(0)->text(), QString("Open"));
        QCOMPARE(list->item(5)->text(), QString("Draw Box"));
        QCOMPARE(list->currentRow(), 5);
        // Three rows up is three Ctrl+Z.
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(2)).center());
        QCOMPARE(session.undoNames().size(), size_t(2));
        QCOMPARE(session.redoNames().size(), size_t(3));
        QCOMPARE(list->currentRow(), 2);
        QCOMPARE(list->count(), 6);
        QVERIFY(list->item(3)->font().italic());
        // Forward again, then a new edit drops the redo rows.
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(4)).center());
        QCOMPARE(session.undoNames().size(), size_t(4));
        session.addGuide({Qt::Horizontal, 20});
        QCOMPARE(list->count(), 6);
        QCOMPARE(list->item(5)->text(), QString("Add Guide"));
        QCOMPARE(list->currentRow(), 5);
    }

    void windowHistoryOpensThePanel()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        Menus &menus = *window.menus();
        QVERIFY(!menus.action("showHistory")->isEnabled());
        workspace.createDocument({200, 200});
        box(workspace.current().session, {10, 10, 20, 20});
        menus.action("showHistory")->trigger();
        auto *panel = window.findChild<HistoryPanel *>();
        QVERIFY(panel);
        QCOMPARE(panel->list()->count(), 2);
    }

    void theLimitIsInPreferences()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        window.menus()->action("preferences")->trigger();
        auto *dialog = window.findChild<QDialog *>("preferencesDialog");
        QVERIFY(dialog);
        auto *limit = dialog->findChild<QSpinBox *>("historyLimit");
        QCOMPARE(limit->value(), 100);
        limit->setValue(25);
        dialog->findChild<QPushButton *>("dialogOK")->click();
        QCOMPARE(EditorSession::historyLimit(), 25);
        EditorSession::setHistoryLimit(100);
    }

    void theShapeSectionEditsCorners()
    {
        EditorSession session;
        session.createDocument({300, 200});
        const QUuid plain = box(session, {10, 10, 50, 50});
        const QUuid id = liveBox(session, {100, 50, 120, 80});
        PropertiesPanel panel(session);
        auto *section = panel.findChild<PanelSection *>("shapeSection");
        QVERIFY(section);
        session.select({plain});
        QVERIFY(section->isHidden());
        session.select({id});
        QVERIFY(!section->isHidden());
        type(panel, "shapeCornerRadiusField", "8");
        QCOMPARE(session.document()->find(id)->liveShape()->radii, (std::array<double, 4>{8, 8, 8, 8}));
        QCOMPARE(session.undoName(), QString("Corner Radius"));
        // Unlinked: a field per corner, and one changes only its own.
        auto *linked = panel.findChild<QToolButton *>("shapeCornersLinked");
        QVERIFY(panel.findChild<QWidget *>("shapeCorners")->isHidden());
        linked->click();
        QVERIFY(!panel.findChild<QWidget *>("shapeCorners")->isHidden());
        type(panel, "shapeCorner2Field", "20");
        QCOMPARE(session.document()->find(id)->liveShape()->radii, (std::array<double, 4>{8, 8, 20, 8}));
        QVERIFY(panel.findChild<NumberField *>("shapeCornerRadius")->isMixed());
        // Uneven corners keep the four fields showing.
        linked->click();
        QVERIFY(!linked->isChecked());
        panel.findChild<QAction *>("cornerChamfer")->trigger();
        QCOMPARE(session.document()->find(id)->liveShape()->styles[0], CornerStyle::chamfer);
        // No fill rule for a single contour.
        QVERIFY(panel.findChild<QComboBox *>("fillRule")->parentWidget()->isHidden());
    }

    void theShapeSectionSetsTheFillRule()
    {
        EditorSession session;
        session.createDocument({300, 200});
        const QUuid outer = box(session, {10, 10, 100, 100});
        const QUuid inner = box(session, {30, 30, 40, 40});
        session.select({outer, inner});
        session.makeCompoundPath();
        PropertiesPanel panel(session);
        auto *rule = panel.findChild<QComboBox *>("fillRule");
        QVERIFY(!panel.findChild<PanelSection *>("shapeSection")->isHidden());
        QVERIFY(!rule->parentWidget()->isHidden());
        QVERIFY(panel.findChild<NumberField *>("shapeCornerRadius")->isHidden());
        QCOMPARE(rule->currentIndex(), 1);
        rule->setCurrentIndex(0);
        emit rule->activated(0);
        QCOMPARE(session.document()->find(session.selectedCompoundPaths().front())->path.fillRule, Qt::WindingFill);
        QCOMPARE(session.undoName(), QString("Fill Rule"));
    }

    void alignFollowsTheKeyObjectAndSpaces()
    {
        EditorSession session;
        session.createDocument({400, 200});
        const QUuid a = box(session, {10, 10, 40, 40});
        const QUuid b = box(session, {100, 60, 20, 40});
        const QUuid c = box(session, {300, 30, 30, 40});
        PropertiesPanel panel(session);
        auto *target = panel.findChild<QComboBox *>("alignTarget");
        session.select({a, b, c});
        QCOMPARE(target->currentIndex(), 0);
        session.setKeyObject(b);
        QCOMPARE(target->currentIndex(), 2);
        panel.findChild<QToolButton *>("alignTop")->click();
        QCOMPARE(session.document()->bounds(a).top(), 60.0);
        QCOMPARE(session.document()->bounds(b).top(), 60.0);
        session.setKeyObject(std::nullopt);
        QCOMPARE(target->currentIndex(), 0);
        // Spacing: Auto until a gap is typed.
        auto *spacing = panel.findChild<QToolButton *>("distributeSpacingHorizontal");
        QVERIFY(spacing->isEnabled());
        QCOMPARE(panel.findChild<QLineEdit *>("distributeGapField")->placeholderText(), QString("Auto"));
        type(panel, "distributeGapField", "12");
        spacing->click();
        QCOMPARE(session.undoName(), QString("Distribute Spacing"));
        QCOMPARE(session.document()->bounds(b).left() - session.document()->bounds(a).right(), 12.0);
        QCOMPARE(session.document()->bounds(c).left() - session.document()->bounds(b).right(), 12.0);
        // Edges: the four new buttons and the two centres.
        for (const char *name : {"distributeLeft", "distributeRight", "distributeTop", "distributeBottom", "distributeHorizontal", "distributeVertical"})
            QVERIFY2(panel.findChild<QToolButton *>(name)->isEnabled(), name);
        panel.findChild<QToolButton *>("distributeLeft")->click();
        QCOMPARE(session.undoName(), QString("Distribute"));
        QCOMPARE(session.document()->bounds(b).left(), 52.0);
    }

    void theIsolationBarShowsTheWayBack()
    {
        EditorSession session;
        session.createDocument({300, 200});
        const QUuid a = box(session, {10, 10, 20, 20});
        const QUuid b = box(session, {50, 10, 20, 20});
        session.select({a, b});
        session.groupSelection();
        const QUuid inner = session.selection().front();
        session.rename(inner, QStringLiteral("Eyes"));
        const QUuid c = box(session, {100, 10, 20, 20});
        session.select({inner, c});
        session.groupSelection();
        const QUuid outer = session.selection().front();
        session.rename(outer, QStringLiteral("Face"));
        IsolationBar bar(session);
        QVERIFY(bar.isHidden());
        session.isolate(inner);
        QVERIFY(!bar.isHidden());
        QCOMPARE(bar.findChild<QToolButton *>("isolationCrumb0")->text(), QString("Layer 1"));
        QCOMPARE(bar.findChild<QToolButton *>("isolationCrumb1")->text(), QString("Face"));
        QCOMPARE(bar.findChild<QToolButton *>("isolationCrumb2")->text(), QString("Eyes"));
        QVERIFY(!bar.findChild<QToolButton *>("isolationCrumb2")->isEnabled());
        bar.findChild<QToolButton *>("isolationBack")->click();
        settle();
        QCOMPARE(session.isolation(), std::vector<QUuid>{outer});
        QVERIFY(!bar.findChild<QToolButton *>("isolationCrumb2"));
        bar.findChild<QToolButton *>("isolationCrumb0")->click();
        settle();
        QVERIFY(session.isolation().empty());
        QVERIFY(bar.isHidden());
    }

    void theMenusReachEveryCommand()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        Menus &menus = *window.menus();
        workspace.createDocument({300, 200});
        EditorSession &session = workspace.current().session;
        QCOMPARE(menus.action("join")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_J));
        QCOMPARE(menus.action("average")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J));
        QCOMPARE(menus.action("duplicate")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
        QCOMPARE(menus.action("rulers")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_R));
        QCOMPARE(menus.action("hideGuides")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
        QCOMPARE(menus.action("makeGuides")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_5));
        menus.action("rulers")->trigger();
        QVERIFY(session.showsRulers && menus.action("rulers")->isChecked());
        menus.action("hideGuides")->trigger();
        QVERIFY(!session.showsGuides);
        QCOMPARE(menus.action("hideGuides")->text(), QString("Show Guides"));
        menus.action("lockGuides")->trigger();
        QVERIFY(session.guidesLocked);
        menus.action("snapToPixel")->trigger();
        QVERIFY(session.snapsToPixel);
        // Join waits for open paths.
        const QUuid closed = box(session, {10, 10, 20, 20});
        QVERIFY(!menus.action("join")->isEnabled());
        const QUuid first = session.addPath(Shapes::line({0, 100}, {50, 100}), QStringLiteral("Line"));
        const QUuid second = session.addPath(Shapes::line({60, 100}, {90, 100}), QStringLiteral("Line"));
        session.select({first, second});
        QVERIFY(menus.action("join")->isEnabled());
        menus.action("join")->trigger();
        QCOMPARE(session.undoName(), QString("Join"));
        // Average asks which way.
        session.select({closed});
        menus.action("average")->trigger();
        auto *dialog = window.findChild<QDialog *>("averageDialog");
        QVERIFY(dialog);
        dialog->findChild<QRadioButton *>("averageVertical")->setChecked(true);
        dialog->findChild<QPushButton *>("dialogOK")->click();
        QCOMPARE(session.undoName(), QString("Average"));
        QCOMPARE(session.document()->bounds(closed).width(), 0.0);
        menus.action("makeGuides")->trigger();
        QCOMPARE(session.document()->guides.size(), size_t(4));
        QVERIFY(menus.action("clearGuides")->isEnabled());
        menus.action("clearGuides")->trigger();
        QVERIFY(session.document()->guides.empty());
        QVERIFY(!menus.action("evenOddFillRule")->isEnabled());
    }
};

QTEST_MAIN(HistoryShapeAlignTests)
#include "HistoryShapeAlignTests.moc"
