#include "UI/NumberField.h"
#include <QMouseEvent>
#include <QtTest>

// Number fields: arithmetic, units, relative input, Mixed, stepping and scrubbing.
class NumberFieldTests : public QObject {
    Q_OBJECT
private slots:
    void arithmeticAndUnits();
    void garbageIsRefused();
    void relativeInputChangesTheValue();
    void typingAppliesAndRestores();
    void mixedIsBlankUntilAValueArrives();
    void arrowsStepWithShiftAndAlt();
    void theLabelScrubsAsOneGesture();
    void clearingCanMeanAuto();
};

namespace {
bool near(std::optional<double> value, double expected)
{
    return value && std::abs(*value - expected) < 1e-9;
}

struct Recorder {
    std::vector<double> values;
    NumberField field{QStringLiteral("W"), QStringLiteral("pt"), [this](double value) {
                          values.push_back(value);
                          field.sync(value);
                      }};
    void type(const QString &text)
    {
        field.field->setText(text);
        QTest::keyClick(field.field, Qt::Key_Return);
    }
};
}

void NumberFieldTests::arithmeticAndUnits()
{
    QVERIFY(near(NumberField::evaluate("10+5", 0), 15));
    QVERIFY(near(NumberField::evaluate("50%", 200), 100));
    QVERIFY(near(NumberField::evaluate("1in", 0), 72));
    QVERIFY(near(NumberField::evaluate("2*(3+1)", 0), 8));
    QVERIFY(near(NumberField::evaluate("25.4mm", 0), 72));
    QVERIFY(near(NumberField::evaluate("2.54 cm", 0), 72));
    QVERIFY(near(NumberField::evaluate("1pc", 0), 12));
    QVERIFY(near(NumberField::evaluate("12px", 0), 12));
    QVERIFY(near(NumberField::evaluate("1in - 2pt", 0), 70));
    QVERIFY(near(NumberField::evaluate("-5", 10), -5));
    QVERIFY(near(NumberField::evaluate("100 / 8", 0), 12.5));
    QVERIFY(near(NumberField::evaluate("3 × 4", 0), 12));
    // Units belong to lengths: a percentage field refuses them.
    QVERIFY(!NumberField::evaluate("1in", 0, false));
    QVERIFY(near(NumberField::evaluate("150%", 80, false), 120));
}

void NumberFieldTests::garbageIsRefused()
{
    for (const char *text : {"wide", "1+", "(2", "2)", "5 apples", "1/0", "", "  ", "1in2", "%"})
        QVERIFY2(!NumberField::evaluate(QString::fromLatin1(text), 10), text);
}

void NumberFieldTests::relativeInputChangesTheValue()
{
    QVERIFY(near(NumberField::evaluate("+10", 5), 15));
    QVERIFY(near(NumberField::evaluate("*2", 5), 10));
    QVERIFY(near(NumberField::evaluate("/4", 10), 2.5));
    QVERIFY(near(NumberField::evaluate("+1in", 0), 72));
    QVERIFY(NumberField::relative("+10").has_value());
    QVERIFY(!NumberField::relative("10").has_value());
    QVERIFY(!NumberField::relative("-10").has_value());
    QVERIFY(!NumberField::relative("/0").has_value());
    QCOMPARE((*NumberField::relative("*3"))(4), 12.0);
    // With changeEach set, relative input goes to each object instead.
    Recorder recorder;
    recorder.field.sync(100);
    std::vector<double> each;
    recorder.field.changeEach = [&](const std::function<double(double)> &change) {
        for (const double value : {1.0, 2.0, 3.0})
            each.push_back(change(value));
    };
    recorder.type("+10");
    QCOMPARE(each, (std::vector<double>{11, 12, 13}));
    QVERIFY(recorder.values.empty());
}

void NumberFieldTests::typingAppliesAndRestores()
{
    Recorder recorder;
    recorder.field.sync(200);
    recorder.type("50%");
    QCOMPARE(recorder.values, std::vector<double>{100});
    QCOMPARE(recorder.field.field->text(), QString("100"));
    // Garbage restores the old value and applies nothing.
    recorder.type("lots");
    QCOMPARE(recorder.values.size(), size_t(1));
    QCOMPARE(recorder.field.field->text(), QString("100"));
    // Limits clamp what applies.
    recorder.field.minimum = 1;
    recorder.type("-40");
    QCOMPARE(recorder.values.back(), 1.0);
}

void NumberFieldTests::mixedIsBlankUntilAValueArrives()
{
    Recorder recorder;
    recorder.field.syncMixed();
    QVERIFY(recorder.field.isMixed());
    QVERIFY(recorder.field.field->text().isEmpty());
    QCOMPARE(recorder.field.field->placeholderText(), QString("Mixed"));
    // Return on an untouched Mixed field changes nothing.
    QTest::keyClick(recorder.field.field, Qt::Key_Return);
    QVERIFY(recorder.values.empty());
    recorder.type("12");
    QCOMPARE(recorder.values, std::vector<double>{12});
    QVERIFY(!recorder.field.isMixed());
    QCOMPARE(recorder.field.field->placeholderText(), QString());
}

void NumberFieldTests::arrowsStepWithShiftAndAlt()
{
    Recorder recorder;
    recorder.field.sync(10);
    QTest::keyClick(recorder.field.field, Qt::Key_Up);
    QCOMPARE(recorder.values.back(), 11.0);
    QTest::keyClick(recorder.field.field, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(recorder.values.back(), 1.0);
    QTest::keyClick(recorder.field.field, Qt::Key_Up, Qt::AltModifier);
    QVERIFY(std::abs(recorder.values.back() - 1.1) < 1e-9);
}

void NumberFieldTests::theLabelScrubsAsOneGesture()
{
    Recorder recorder;
    recorder.field.sync(50);
    std::vector<bool> gestures;
    recorder.field.gesture = [&](bool starting) { gestures.push_back(starting); };
    QLabel *handle = recorder.field.handle();
    QVERIFY(handle);
    QCOMPARE(handle->text(), QString("W"));
    QCOMPARE(handle->cursor().shape(), Qt::SizeHorCursor);
    const auto drag = [&](int dx, Qt::KeyboardModifiers modifiers) {
        const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
            QMouseEvent event(type, at, handle->mapToGlobal(at), Qt::LeftButton, buttons, modifiers);
            QCoreApplication::sendEvent(handle, &event);
        };
        send(QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(2 + dx / 2, 2), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(2 + dx, 2), Qt::LeftButton);
        send(QEvent::MouseButtonRelease, QPointF(2 + dx, 2), Qt::NoButton);
    };
    drag(10, Qt::NoModifier);
    QCOMPARE(recorder.values.back(), 60.0);
    QCOMPARE(gestures, (std::vector<bool>{true, false}));
    drag(10, Qt::ShiftModifier);
    QCOMPARE(recorder.values.back(), 160.0);
    drag(-10, Qt::AltModifier);
    QCOMPARE(recorder.values.back(), 159.0);
    // A click without a drag changes nothing and opens no gesture.
    const size_t before = recorder.values.size();
    drag(0, Qt::NoModifier);
    QCOMPARE(recorder.values.size(), before);
    QCOMPARE(gestures.size(), size_t(6));
}

void NumberFieldTests::clearingCanMeanAuto()
{
    Recorder recorder;
    int cleared = 0;
    recorder.field.cleared = [&] { ++cleared; };
    recorder.field.sync(28.8);
    recorder.type("");
    QCOMPARE(cleared, 1);
    recorder.field.syncUnset(28.8, "Auto (28.8)");
    QCOMPARE(recorder.field.field->placeholderText(), QString("Auto (28.8)"));
    QVERIFY(recorder.field.field->text().isEmpty());
    // Already Auto: emptying again is nothing new.
    recorder.type("auto");
    QCOMPARE(cleared, 1);
    // A step from Auto starts at its value.
    QTest::keyClick(recorder.field.field, Qt::Key_Up);
    QVERIFY(std::abs(recorder.values.back() - 29.8) < 1e-9);
}

QTEST_MAIN(NumberFieldTests)
#include "NumberFieldTests.moc"
