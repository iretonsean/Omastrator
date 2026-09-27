#include "Canvas/EditorCanvas.h"
#include "UI/ColorPickerSheet.h"
#include "UI/FloatingPanel.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QWindow>
#include <QtTest>

// Tool panels: placement, keys, closing, and the colour picker's.
namespace {
QWidget *visiblePanel(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return widget;
    }
    return nullptr;
}

// Replaced content lingers hidden until the event loop.
template <typename Widget> Widget *shownChild(QWidget &root, const QString &name)
{
    for (Widget *widget : root.findChildren<Widget *>(name)) {
        if (widget->isVisible())
            return widget;
    }
    return nullptr;
}

// A window holding a canvas beside a margin.
struct Shown {
    EditorSession session;
    QWidget window;
    EditorCanvas *const canvas;
    Shown() : canvas(new EditorCanvas(session, &window))
    {
        session.createDocument(QSizeF(100, 100));
        window.resize(400, 300);
        canvas->setGeometry(QRect(0, 0, 300, 300));
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            throw std::runtime_error("the window never became active");
    }
};
}

class FloatingPanelTests : public QObject {
    Q_OBJECT
private slots:
    void colorPickerPanelAppliesOnOKAndNotOnCancel();
    void aPanelOpensOverTheCanvasThenWhereItWasLeft();
    void itsCloseButtonAndEscapeCancel();
    void aPanelRemembersWhereItIsMovedAndCentresWithoutACanvas();
    void aPanelNeverOpensOverTheDock();
};

void FloatingPanelTests::colorPickerPanelAppliesOnOKAndNotOnCancel()
{
    Shown shown;
    FloatingPanel panel(QStringLiteral("colorPickerPanel"), shown.window);
    std::optional<QColor> applied;
    ColorPickerSheet::showIn(panel, QStringLiteral("Fill Color"), QColor(255, 0, 0), [&applied](QColor color) { applied = color; });
    QWidget *window = visiblePanel(QStringLiteral("colorPickerPanel"));
    QVERIFY(window);
    QCOMPARE(window->windowTitle(), QString("Fill Color"));
    // A layout pass keeps the sheet at its own size.
    window->adjustSize();
    QCOMPARE(window->size(), window->findChild<ColorPickerSheet *>()->sizeHint());
    shownChild<QPushButton>(*window, "pickerCancel")->click();
    QVERIFY(!panel.isVisible() && !applied);
    ColorPickerSheet::showIn(panel, QStringLiteral("Stroke Color"), QColor(0, 0, 255), [&applied](QColor color) { applied = color; });
    shownChild<QPushButton>(*visiblePanel(QStringLiteral("colorPickerPanel")), "pickerOK")->click();
    QCOMPARE(applied.value(), QColor(0, 0, 255));
    QVERIFY(!panel.isVisible());
    // Its close button cancels.
    applied.reset();
    ColorPickerSheet::showIn(panel, QStringLiteral("Fill Color"), QColor(0, 255, 0), [&applied](QColor color) { applied = color; });
    visiblePanel(QStringLiteral("colorPickerPanel"))->close();
    QVERIFY(!applied && !panel.isVisible());
}

void FloatingPanelTests::aPanelOpensOverTheCanvasThenWhereItWasLeft()
{
    Shown shown;
    // Wider than the canvas: the canvas's middle, not the window's.
    shown.window.resize(600, 400);
    FloatingPanel panel(QStringLiteral("testPlacedPanel"), shown.window);
    QVERIFY(!panel.isVisible());
    panel.close();
    // First over the canvas's middle.
    panel.show(QStringLiteral("First"), new QLabel(QStringLiteral("One")));
    QWidget *window = visiblePanel(QStringLiteral("testPlacedPanel"));
    QVERIFY(window);
    const QPoint middle = shown.canvas->mapToGlobal(shown.canvas->rect().center());
    QVERIFY(std::abs(window->pos().x() + window->width() / 2 - middle.x()) <= 1);
    QVERIFY(std::abs(window->pos().y() + window->height() / 2 - middle.y()) <= 1);
    // It takes the keys, fixed to its content's size.
    QTRY_COMPARE(QApplication::activeWindow(), window);
    QCOMPARE(window->size(), window->findChild<QLabel *>()->sizeHint());
    QCOMPARE(window->minimumSize(), window->maximumSize());
    // Moved, it keeps its place through content and a close.
    const QPoint spot(30, 20);
    window->move(spot);
    // Shown again, it takes the keys back.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    auto *old = new QLabel(QStringLiteral("Old"));
    panel.show(QStringLiteral("Second"), old);
    QTRY_COMPARE(QApplication::activeWindow(), window);
    QCOMPARE(window->pos(), spot);
    QCOMPARE(window->windowTitle(), QString("Second"));
    QTRY_VERIFY(old->isVisible());
    QPointer<QLabel> gone(old);
    auto *wider = new QLabel(QStringLiteral("Wider content than before"));
    panel.show(QStringLiteral("Third"), wider);
    QVERIFY(gone && !gone->isVisible());
    QCOMPARE(window->size(), wider->sizeHint());
    QTRY_VERIFY(!gone);
    panel.close();
    QVERIFY(!panel.isVisible());
    panel.show(QStringLiteral("Fourth"), new QLabel(QStringLiteral("Four")));
    QCOMPARE(window->pos(), spot);
    // Another panel of that name, later, opens there too.
    panel.close();
    FloatingPanel again(QStringLiteral("testPlacedPanel"), shown.window);
    again.show(QStringLiteral("Again"), new QLabel(QStringLiteral("Five")));
    QCOMPARE(visiblePanel(QStringLiteral("testPlacedPanel"))->pos(), spot);
}

void FloatingPanelTests::itsCloseButtonAndEscapeCancel()
{
    Shown shown;
    // Without a listener, its close button just closes.
    FloatingPanel quiet(QStringLiteral("testQuietPanel"), shown.window);
    quiet.show(QStringLiteral("Quiet"), new QLabel(QStringLiteral("Content")));
    visiblePanel(QStringLiteral("testQuietPanel"))->close();
    QVERIFY(!quiet.isVisible());
    // A hidden panel is never handed the keys.
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel::refocus(QStringLiteral("testQuietPanel"));
    QTest::qWait(50);
    QCOMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel panel(QStringLiteral("testClosedPanel"), shown.window);
    int closes = 0;
    panel.onClose = [&closes] { ++closes; };
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    QWidget *window = visiblePanel(QStringLiteral("testClosedPanel"));
    QVERIFY(window);
    window->close();
    QCOMPARE(closes, 1);
    QVERIFY(!panel.isVisible());
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(closes, 2);
    QVERIFY(!panel.isVisible());
    // Hidden by its owner, it reports nothing.
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    panel.close();
    QCOMPARE(closes, 2);
    // A click on the canvas hands it the keys back.
    panel.show(QStringLiteral("Panel"), new QLabel(QStringLiteral("Content")));
    shown.window.activateWindow();
    QTRY_COMPARE(QApplication::activeWindow(), &shown.window);
    FloatingPanel::refocus(QStringLiteral("testClosedPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), window);
    // Only the panel of that name.
    quiet.show(QStringLiteral("Quiet"), new QLabel(QStringLiteral("Content")));
    QWidget *other = visiblePanel(QStringLiteral("testQuietPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), other);
    FloatingPanel::refocus(QStringLiteral("testClosedPanel"));
    QTRY_COMPARE(QApplication::activeWindow(), window);
    // A miss activates no widget; a bare window keeps focus.
    QWindow bare;
    bare.show();
    bare.requestActivate();
    QTRY_COMPARE(QGuiApplication::focusWindow(), &bare);
    FloatingPanel::refocus(QStringLiteral("testMissingPanel"));
    QTest::qWait(50);
    QCOMPARE(QGuiApplication::focusWindow(), &bare);
}

void FloatingPanelTests::aPanelRemembersWhereItIsMovedAndCentresWithoutACanvas()
{
    Shown shown;
    QPointer<QWidget> dropped;
    {
        FloatingPanel moved(QStringLiteral("testMovedPanel"), shown.window);
        moved.show(QStringLiteral("Moved"), new QLabel(QStringLiteral("Content")));
        dropped = visiblePanel(QStringLiteral("testMovedPanel"));
        dropped->move(QPoint(25, 35));
        QTRY_COMPARE(dropped->pos(), QPoint(25, 35));
    }
    // Its window goes with it.
    QVERIFY(!dropped);
    // Gone without a close, it reopens where it was moved.
    FloatingPanel again(QStringLiteral("testMovedPanel"), shown.window);
    again.show(QStringLiteral("Again"), new QLabel(QStringLiteral("Content")));
    QCOMPARE(visiblePanel(QStringLiteral("testMovedPanel"))->pos(), QPoint(25, 35));
    // Shown, never moved, it keeps that place for the next.
    QPoint first;
    {
        FloatingPanel still(QStringLiteral("testStillPanel"), shown.window);
        still.show(QStringLiteral("Still"), new QLabel(QStringLiteral("Content")));
        first = visiblePanel(QStringLiteral("testStillPanel"))->pos();
    }
    const QPoint away = shown.window.pos() + QPoint(40, 30);
    shown.window.move(away);
    QTRY_COMPARE(shown.window.pos(), away);
    FloatingPanel next(QStringLiteral("testStillPanel"), shown.window);
    next.show(QStringLiteral("Next"), new QLabel(QStringLiteral("Content")));
    QCOMPARE(visiblePanel(QStringLiteral("testStillPanel"))->pos(), first);
    // A window without a canvas centres it on itself.
    QWidget plain;
    plain.setGeometry(100, 80, 500, 300);
    plain.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plain));
    FloatingPanel centred(QStringLiteral("testCentredPanel"), plain);
    centred.show(QStringLiteral("Centred"), new QLabel(QStringLiteral("Content")));
    QWidget *window = visiblePanel(QStringLiteral("testCentredPanel"));
    const QPoint middle = plain.geometry().center();
    QVERIFY(std::abs(window->pos().x() + window->width() / 2 - middle.x()) <= 1);
    QVERIFY(std::abs(window->pos().y() + window->height() / 2 - middle.y()) <= 1);
}

void FloatingPanelTests::aPanelNeverOpensOverTheDock()
{
    // A dock covering the canvas's middle, where a panel would open.
    Shown shown;
    auto *dock = new QWidget(&shown.window);
    dock->setObjectName(QStringLiteral("panelDock"));
    dock->setGeometry(QRect(100, 0, 300, 300));
    dock->show();
    FloatingPanel panel(QStringLiteral("swatchesPanel"), shown.window);
    panel.show(QStringLiteral("Swatches"), new QLabel(QStringLiteral("Swatches")));
    QWidget *opened = visiblePanel(QStringLiteral("swatchesPanel"));
    QVERIFY(opened);
    const QRect docked(dock->mapToGlobal(QPoint(0, 0)), dock->size());
    QVERIFY(!QRect(opened->pos(), opened->frameSize()).intersects(docked));
    QVERIFY(opened->frameGeometry().right() < docked.left());
    panel.close();
}

QTEST_MAIN(FloatingPanelTests)
#include "FloatingPanelTests.moc"
