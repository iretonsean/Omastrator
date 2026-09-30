#include "TemporaryConfig.h"
#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "Document/EditorSession.h"
#include "UI/ColorPickerSheet.h"
#include "UI/ElementBarActions.h"
#include "UI/NumberField.h"
#include <QApplication>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
#include <QTest>
#include <QToolButton>

// The element bar (docs/LIVE-IN-FRAME.md, section 3), against a fake host: where it sits, that it keeps one height at every
// zoom, what it shows for mixed values, and what its controls send. The page itself is LiveFrameEditTests'.
namespace {
class FakeHost : public BrowserViewHost {
public:
    struct Edit {
        QStringList properties;
        QString value;
        bool preview = false;
    };
    QImage picture(const QUuid &) const override { return {}; }
    QString message(const QUuid &) const override { return {}; }
    QString beginEditPage(const QUuid &) override { return {}; }
    EditBoxes editBoxes(const QUuid &) const override { return boxes; }
    ElementState elementState(const QUuid &) const override { return state; }
    QString editElements(const QUuid &, const QStringList &properties, const QString &value, bool preview) override
    {
        edits.push_back({properties, value, preview});
        return {};
    }
    QString editElementText(const QUuid &, const QString &selector, const QString &text) override
    {
        texts.push_back({selector, text});
        return {};
    }
    bool canUndoPageEdit(const QUuid &) const override { return undoable; }
    bool canRedoPageEdit(const QUuid &) const override { return redoable; }
    void act(const QUuid &, Action action) override { acts.push_back(action); }
    void undoPageEdit(const QUuid &) override { ++undone; }
    void redoPageEdit(const QUuid &) override { ++redone; }

    EditBoxes boxes;
    ElementState state;
    QList<Edit> edits;
    QList<QPair<QString, QString>> texts;
    QList<Action> acts;
    bool undoable = false;
    bool redoable = false;
    int undone = 0;
    int redone = 0;
};

QJsonObject element(const QString &selector, const QRectF &rect, const QJsonObject &styles, const QString &text = {}, bool textOnly = false)
{
    return QJsonObject{{"selector", selector},
                       {"tag", "div"},
                       {"text", text},
                       {"textOnly", textOnly},
                       {"rect", QJsonObject{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}}},
                       {"styles", styles}};
}

QJsonObject styles(const QString &paddingLeft, const QString &paddingRight = {})
{
    return QJsonObject{{"padding-left", paddingLeft}, {"padding-right", paddingRight.isEmpty() ? paddingLeft : paddingRight},
                       {"padding-top", "8px"}, {"padding-bottom", "8px"}, {"width", "200px"}, {"height", "100px"},
                       {"color", "rgb(0, 0, 0)"}, {"background-color", "rgb(255, 255, 255)"}, {"border-radius", "4px"},
                       {"font-size", "16px"}, {"font-weight", "400"}, {"margin-left", "4px"}, {"margin-right", "4px"},
                       {"margin-top", "0px"}, {"margin-bottom", "0px"}, {"border-top-left-radius", "4px"},
                       {"border-top-right-radius", "4px"}, {"border-bottom-right-radius", "4px"}, {"border-bottom-left-radius", "12px"}};
}

struct Rig {
    EditorSession session;
    EditorCanvas canvas{session};
    FakeHost host;
    QUuid frame;
    ElementBar *bar = nullptr;

    Rig()
    {
        session.loadDocument(VectorDocument::blank({4000, 3000}));
        canvas.resize(1000, 800);
        canvas.show();
        session.zoomToRect(QRectF(50, 100, 900, 700));
        canvas.setBrowserViewHost(&host);
        frame = session.addBrowserView({100, 200, 600, 400}, QUrl(QStringLiteral("https://example.com/a")));
        session.deselectAll();
        session.selectTool(Tool::select);
        bar = ElementBarActions::attach(nullptr, canvas);
    }
    void pick(const QList<QJsonObject> &elements)
    {
        QJsonArray picked;
        host.boxes.selection.clear();
        for (const QJsonObject &each : elements) {
            picked.append(each);
            const QJsonObject rect = each["rect"].toObject();
            host.boxes.selection.push_back({QRectF(rect["x"].toDouble(), rect["y"].toDouble(), rect["width"].toDouble(), rect["height"].toDouble()), "div"});
        }
        host.state.selection = picked;
        canvas.noteEditPageHostChanged();
    }
    NumberField *field(const QString &name) const { return bar->findChild<NumberField *>(name); }
};
}

class ElementBarTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        // The picker keeps its recent colours in the settings; a test must not write the user's.
        useTemporaryConfig();
    }

    void placeGoesBelowThenAboveThenStaysInside()
    {
        const QRectF visible(0, 0, 1000, 800);
        const QSize bar(200, 28);
        const QRect below = ElementBar::place(QRectF(400, 100, 100, 50), bar, visible);
        QCOMPARE(below.top(), 158);
        QVERIFY(std::abs(below.center().x() - 450) <= 1);
        const QRect above = ElementBar::place(QRectF(400, 760, 100, 30), bar, visible);
        QCOMPARE(above.bottom() + 1 + ElementBar::defaultGap, 760);
        const QRect clamped = ElementBar::place(QRectF(-50, 100, 40, 20), bar, visible);
        QVERIFY(clamped.left() >= 0);
        QVERIFY(visible.toAlignedRect().contains(clamped));
        // A selection filling the frame leaves no room either side: the bar stays inside it.
        const QRect covering = ElementBar::place(QRectF(0, 0, 1000, 800), bar, visible);
        QVERIFY(visible.toAlignedRect().contains(covering));
    }

    void showsOnlyWithAPickInEditPage()
    {
        Rig rig;
        rig.pick({element("#a", {110, 210, 100, 60}, styles("8px"))});
        QVERIFY(!rig.bar->isVisible());
        rig.host.state = {};
        rig.host.boxes = {};
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        QVERIFY(!rig.bar->isVisible());
        rig.pick({element("#a", {110, 210, 100, 60}, styles("8px"))});
        QVERIFY(rig.bar->isVisible());
        rig.pick({});
        QVERIFY(!rig.bar->isVisible());
        rig.pick({element("#a", {110, 210, 100, 60}, styles("8px"))});
        rig.canvas.leaveEditPage();
        QVERIFY(!rig.bar->isVisible());
    }

    void keepsOneHeightAtEveryZoom()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        QVERIFY(rig.bar->isVisible());
        const int height = rig.bar->height();
        QVERIFY(height > 0);
        for (const QRectF &area : {QRectF(0, 0, 4000, 3000), QRectF(150, 250, 200, 150), QRectF(100, 200, 600, 400)}) {
            rig.session.zoomToRect(area);
            rig.canvas.noteEditPageHostChanged();
            QVERIFY(rig.bar->isVisible());
            QCOMPARE(rig.bar->height(), height);
            const QRectF selection = *rig.canvas.editPageSelectionRect();
            const QRectF visible = *rig.canvas.editPageVisibleRect();
            // Inside the frame's visible part, or the canvas where the frame is narrower than the bar.
            const QRectF plate(rig.bar->geometry().adjusted(3, 2, -3, -5));
            QVERIFY(visible.adjusted(-1, -1, 1, 1).contains(plate) || QRectF(rig.canvas.rect()).adjusted(-1, -1, 1, 1).contains(plate));
            // Below the pick with the gap, or above it, or inside the frame when neither fits.
            const int plateTop = rig.bar->geometry().top() + 2;
            const bool below = plateTop >= selection.bottom() + ElementBar::defaultGap - 1;
            const bool above = plateTop + ElementBar::barHeight <= selection.top() - ElementBar::defaultGap + 1;
            const bool inside = selection.intersects(QRectF(rig.bar->geometry()));
            QVERIFY(below || above || inside);
        }
    }

    void mixedValuesShowMixed()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px", "8px")), element("#b", {60, 140, 200, 80}, styles("16px", "16px"))});
        QVERIFY(rig.bar->isVisible());
        QVERIFY(rig.field("elementPaddingX")->isMixed());
        QVERIFY(!rig.field("elementPaddingY")->isMixed());
        QCOMPARE(rig.field("elementPaddingY")->value(), 8.0);
        QVERIFY(!rig.field("elementWidth")->isMixed());
        // One element whose left and right padding differ is mixed too.
        rig.pick({element("#c", {60, 40, 200, 80}, styles("8px", "12px"))});
        QVERIFY(rig.field("elementPaddingX")->isMixed());
        rig.pick({element("#c", {60, 40, 200, 80}, styles("12px", "12px"))});
        QVERIFY(!rig.field("elementPaddingX")->isMixed());
        QCOMPARE(rig.field("elementPaddingX")->value(), 12.0);
    }

    void aTypedValueEditsEveryOneAtOnce()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"), "A"), element("#b", {60, 140, 200, 80}, styles("16px"), "B")});
        NumberField *padding = rig.field("elementPaddingX");
        padding->field->setText(QStringLiteral("20"));
        padding->commit();
        QCOMPARE(rig.host.edits.size(), 1);
        QCOMPARE(rig.host.edits[0].properties, (QStringList{"padding-left", "padding-right"}));
        QCOMPARE(rig.host.edits[0].value, QStringLiteral("20px"));
        QVERIFY(!rig.host.edits[0].preview);
        NumberField *weight = rig.field("elementFontWeight");
        QVERIFY(weight);
        weight->field->setText(QStringLiteral("700"));
        weight->commit();
        QCOMPARE(rig.host.edits.last().properties, QStringList{"font-weight"});
        QCOMPARE(rig.host.edits.last().value, QStringLiteral("700"));
    }

    void aScrubPreviewsThenCommitsOnce()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        NumberField *radius = rig.field("elementRadius");
        radius->gesture(true);
        for (const char *value : {"6", "9", "12"}) {
            radius->field->setText(QString::fromLatin1(value));
            radius->commit();
        }
        QCOMPARE(rig.host.edits.size(), 3);
        for (const FakeHost::Edit &each : rig.host.edits)
            QVERIFY(each.preview);
        radius->gesture(false);
        QCOMPARE(rig.host.edits.size(), 4);
        QVERIFY(!rig.host.edits.last().preview);
        QCOMPARE(rig.host.edits.last().value, QStringLiteral("12px"));
        QCOMPARE(rig.host.edits.last().properties, QStringList{"border-radius"});
    }

    void controlsRebuildOnlyWhenTheKindChanges()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        NumberField *before = rig.field("elementPaddingX");
        rig.pick({element("#a", {60, 40, 200, 80}, styles("12px"))});
        QCOMPARE(rig.field("elementPaddingX"), before);
        QCOMPARE(before->value(), 12.0);
        // Text arrives: the bar gains Edit Text and font controls.
        QVERIFY(!rig.bar->findChild<QToolButton *>("elementEditText"));
        rig.pick({element("#t", {60, 40, 200, 40}, styles("12px"), "Hello", true)});
        QVERIFY(rig.bar->findChild<QToolButton *>("elementEditText"));
        QVERIFY(rig.field("elementFontSize"));
    }

    void editTextTypesOverThePickAndEditsThePage()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#t", {60, 40, 200, 40}, styles("12px"), "Hello", true)});
        auto *edit = rig.bar->findChild<QToolButton *>("elementEditText");
        QVERIFY(edit);
        edit->click();
        QVERIFY(rig.canvas.isEditingPageText());
        QVERIFY(!rig.bar->isVisible());
        auto *line = rig.canvas.findChild<QLineEdit *>("pageTextEdit");
        QVERIFY(line);
        QCOMPARE(line->text(), QStringLiteral("Hello"));
        line->setText(QStringLiteral("Goodbye"));
        QTest::keyClick(line, Qt::Key_Return);
        QVERIFY(!rig.canvas.isEditingPageText());
        QCOMPARE(rig.host.texts.size(), 1);
        QCOMPARE(rig.host.texts[0].first, QStringLiteral("#t"));
        QCOMPARE(rig.host.texts[0].second, QStringLiteral("Goodbye"));
    }

    void escapeLeavesTheTextAsItWas()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#t", {60, 40, 200, 40}, styles("12px"), "Hello", true)});
        QVERIFY(rig.canvas.editPageText());
        auto *line = rig.canvas.findChild<QLineEdit *>("pageTextEdit");
        line->setText(QStringLiteral("Nope"));
        QTest::keyClick(line, Qt::Key_Escape);
        QVERIFY(!rig.canvas.isEditingPageText());
        QVERIFY(rig.host.texts.isEmpty());
        QVERIFY(rig.bar->isVisible());
        // Several picks, or an element with children, have no text to type over.
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"), "x", false)});
        QVERIFY(!rig.canvas.editPageText());
    }

    void pageUndoAsksTheHost()
    {
        Rig rig;
        QVERIFY(!rig.canvas.canUndoPageEdit());
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.host.undoable = true;
        rig.host.redoable = true;
        QVERIFY(rig.canvas.canUndoPageEdit());
        rig.canvas.undoPageEdit();
        rig.canvas.redoPageEdit();
        QCOMPARE(rig.host.undone, 1);
        QCOMPARE(rig.host.redone, 1);
        rig.host.undoable = false;
        rig.canvas.undoPageEdit();
        QCOMPARE(rig.host.undone, 1);
    }

    void enterAndEscapeHandTheKeyboardBackToTheCanvas()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        QVERIFY(QTest::qWaitForWindowActive(&rig.canvas));
        NumberField *padding = rig.field("elementPaddingX");
        QCOMPARE(padding->field->focusPolicy(), Qt::ClickFocus);
        for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Escape}) {
            padding->field->setFocus(Qt::MouseFocusReason);
            QTRY_VERIFY(padding->field->hasFocus());
            QTest::keyClick(padding->field, key);
            QTRY_VERIFY(rig.canvas.hasFocus());
            QVERIFY(!padding->field->hasFocus());
        }
    }

    void askIsOnlyOnASiteThatIsTheUsers()
    {
        struct Reset {
            ~Reset() { ElementBarActions::setProjectResolver({}); }
        } reset;
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        // Build It offers Hand to Agent for a site that has no folder, so the bar doesn't ask about the wrong project.
        QVERIFY(!rig.bar->findChild<QToolButton *>("elementAsk"));
        QVERIFY(rig.bar->findChild<QToolButton *>("elementMore"));
        ElementBarActions::setProjectResolver([](const QUuid &) { return QStringLiteral("/tmp/mine"); });
        rig.pick({element("#a", {60, 40, 200, 80}, styles("12px"))});
        QVERIFY(rig.bar->findChild<QToolButton *>("elementAsk"));
    }

    void customColourIsTheAppsOwnPickerAndAppliesOnOKOnly()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        auto *well = rig.bar->findChild<QToolButton *>("element:color");
        QVERIFY(well && well->menu());
        emit well->menu()->aboutToShow();
        QAction *custom = nullptr;
        for (QAction *action : well->menu()->actions())
            if (action->objectName() == QLatin1String("elementCustomColor"))
                custom = action;
        QVERIFY(custom);
        auto sheet = [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == QLatin1String("colorPickerPanel") && widget->isVisible())
                    return widget->findChild<ColorPickerSheet *>();
            return static_cast<ColorPickerSheet *>(nullptr);
        };
        custom->trigger();
        QTRY_VERIFY(sheet());
        // It starts on the element's own colour, and Cancel changes nothing.
        QCOMPARE(sheet()->color(), QColor(0, 0, 0));
        sheet()->findChild<QPushButton *>("pickerCancel")->click();
        QVERIFY(rig.host.edits.isEmpty());
        custom->trigger();
        QTRY_VERIFY(sheet());
        sheet()->setHSB(PickerHSB::from(QColor(255, 0, 0)));
        sheet()->findChild<QPushButton *>("pickerOK")->click();
        QCOMPARE(rig.host.edits.size(), 1);
        QCOMPARE(rig.host.edits[0].properties, QStringList{"color"});
        QCOMPARE(rig.host.edits[0].value, QStringLiteral("#ff0000"));
    }

    void customColourCanBeTranslucent()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        auto *well = rig.bar->findChild<QToolButton *>("element:color");
        QVERIFY(well && well->menu());
        emit well->menu()->aboutToShow();
        for (QAction *action : well->menu()->actions())
            if (action->objectName() == QLatin1String("elementCustomColor"))
                action->trigger();
        ColorPickerSheet *sheet = nullptr;
        QTRY_VERIFY((sheet = [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == QLatin1String("colorPickerPanel") && widget->isVisible())
                    return widget->findChild<ColorPickerSheet *>();
            return static_cast<ColorPickerSheet *>(nullptr);
        }()));
        QVERIFY(sheet->findChild<PickerField *>("alpha"));
        sheet->setHSB(PickerHSB::from(QColor(255, 0, 0)));
        sheet->setAlphaPercent(50);
        sheet->findChild<QPushButton *>("pickerOK")->click();
        QCOMPARE(rig.host.edits.size(), 1);
        QCOMPARE(rig.host.edits[0].value, QStringLiteral("rgba(255, 0, 0, 0.5)"));
    }

    void theBoxShowsPaddingForEachSideAndBack()
    {
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        QVERIFY(rig.field("elementPaddingX"));
        auto *box = rig.bar->findChild<QToolButton *>("elementPaddingBox");
        QVERIFY(box);
        box->click();
        QTRY_VERIFY(rig.field("elementPaddingTop") && !rig.field("elementPaddingX"));
        for (const char *name : {"elementPaddingRight", "elementPaddingBottom", "elementPaddingLeft"})
            QVERIFY(rig.field(name));
        NumberField *left = rig.field("elementPaddingLeft");
        QCOMPARE(left->value(), 8.0);
        left->field->setText(QStringLiteral("20"));
        left->commit();
        QCOMPARE(rig.host.edits.last().properties, QStringList{"padding-left"});
        QCOMPARE(rig.host.edits.last().value, QStringLiteral("20px"));
        // Picking again keeps the box open; the box button closes it.
        rig.pick({element("#b", {60, 40, 200, 80}, styles("8px"))});
        QVERIFY(rig.field("elementPaddingTop"));
        rig.bar->findChild<QToolButton *>("elementPaddingBox")->click();
        QTRY_VERIFY(rig.field("elementPaddingX") && !rig.field("elementPaddingTop"));
    }

    void theMoreMenuHasMarginAndEachCornerAndKeepEditsOnASiteThatIsNotTheUsers()
    {
        struct Reset {
            ~Reset() { ElementBarActions::setProjectResolver({}); }
        } reset;
        Rig rig;
        QVERIFY(rig.canvas.enterEditPage(rig.frame));
        rig.pick({element("#a", {60, 40, 200, 80}, styles("8px"))});
        auto *more = rig.bar->findChild<QToolButton *>("elementMore");
        QVERIFY(more && more->menu());
        NumberField *marginX = rig.bar->findChild<NumberField *>("elementMarginX");
        NumberField *marginY = rig.bar->findChild<NumberField *>("elementMarginY");
        NumberField *corner = rig.bar->findChild<NumberField *>("elementRadiusBottomLeft");
        QVERIFY(marginX && marginY && corner);
        QCOMPARE(marginX->value(), 4.0);
        QCOMPARE(corner->value(), 12.0);
        corner->field->setText(QStringLiteral("2"));
        corner->commit();
        QCOMPARE(rig.host.edits.last().properties, QStringList{"border-bottom-left-radius"});
        marginY->field->setText(QStringLiteral("10"));
        marginY->commit();
        QCOMPARE(rig.host.edits.last().properties, (QStringList{"margin-top", "margin-bottom"}));
        QCOMPARE(rig.host.edits.last().value, QStringLiteral("10px"));

        // Not the user's site: Keep Edits… is here, and goes to the host.
        QAction *keep = more->menu()->findChild<QAction *>("elementKeepEdits");
        QVERIFY(keep);
        keep->trigger();
        QCOMPARE(rig.host.acts, QList<BrowserViewHost::Action>{BrowserViewHost::Action::keepEdits});

        // The user's own site has the code instead.
        ElementBarActions::setProjectResolver([](const QUuid &) { return QStringLiteral("/tmp/mine"); });
        rig.pick({element("#a", {60, 40, 200, 80}, styles("12px"))});
        // The old buttons go with the refill, on the next turn of the loop.
        QTRY_VERIFY(rig.bar->findChild<QToolButton *>("elementAsk"));
        QTRY_VERIFY(!rig.bar->findChild<QToolButton *>("elementMore")->menu()->findChild<QAction *>("elementKeepEdits"));
    }

    void parsesComputedValues()
    {
        QCOMPARE(ElementBarActions::numberOf("16px").value_or(-1), 16.0);
        QCOMPARE(ElementBarActions::numberOf("700").value_or(-1), 700.0);
        QCOMPARE(ElementBarActions::numberOf("1.5px").value_or(-1), 1.5);
        QVERIFY(!ElementBarActions::numberOf("auto"));
        QCOMPARE(ElementBarActions::colorOf("rgb(255, 0, 0)"), QColor(255, 0, 0));
        QVERIFY(std::abs(ElementBarActions::colorOf("rgba(0, 0, 255, 0.5)").alphaF() - 0.5) < 0.01);
        QCOMPARE(ElementBarActions::colorOf("#00ff00"), QColor(0, 255, 0));
        QCOMPARE(ElementBarActions::colorOf("rgba(0, 0, 0, 0)").alpha(), 0);
    }
};

QTEST_MAIN(ElementBarTests)
#include "ElementBarTests.moc"
