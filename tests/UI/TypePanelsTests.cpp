#include "Document/FontFeatures.h"
#include "UI/NumberField.h"
#include "UI/ObjectDialogs.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/PropertiesPanel.h"
#include "UI/TextStylesPanel.h"
#include <QCheckBox>
#include <QFontComboBox>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// The type panels: Paragraph for area type, the text style button, the
// OpenType popover, Window ▸ Type Styles and Type ▸ Find/Replace Font.
namespace {
void type(QWidget &panel, const QString &field, const QString &text)
{
    auto *edit = panel.findChild<QLineEdit *>(field);
    QVERIFY2(edit, qPrintable(field));
    edit->setText(text);
    QTest::keyClick(edit, Qt::Key_Return);
}

QUuid areaText(EditorSession &session, const QString &words)
{
    VectorObject object = session.textObject({20, 20}, words);
    object.text.area = QSizeF(200, 0);
    return session.addObject(object, QStringLiteral("Type"));
}

// A menu's entries as they are when it opens.
QAction *entry(QMenu *menu, const QString &name)
{
    emit menu->aboutToShow();
    return menu->findChild<QAction *>(name);
}
}

class TypePanelsTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }

    void paragraphShowsForAreaType()
    {
        EditorSession session;
        session.createDocument({400, 300});
        PropertiesPanel panel(session);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const QUuid point = session.addText({20, 100}, QStringLiteral("Point"));
        QVERIFY(!panel.findChild<QWidget *>("paragraphSection")->isVisible());
        const QUuid area = areaText(session, QStringLiteral("One\nTwo"));
        QVERIFY(panel.findChild<QWidget *>("paragraphSection")->isVisible());
        type(panel, "paragraphFirstLineIndentField", "-12");
        QCOMPARE(session.document()->find(area)->text.firstLineIndent, -12.0);
        QCOMPARE(session.undoName(), QStringLiteral("First-Line Indent"));
        type(panel, "paragraphSpaceAfterField", "6");
        QCOMPARE(session.document()->find(area)->text.spaceAfter, 6.0);
        QCOMPARE(session.undoName(), QStringLiteral("Space After"));
        type(panel, "paragraphLeftIndentField", "12");
        QCOMPARE(session.document()->find(area)->text.leftIndent, 12.0);
        // The last line's place shows only for justified paragraphs.
        QVERIFY(!panel.findChild<QWidget *>("paragraphLastLine")->isVisible());
        panel.findChild<QToolButton *>("characterJustify")->click();
        auto *last = panel.findChild<QComboBox *>("paragraphLastLine");
        QVERIFY(last->isVisible());
        last->setCurrentIndex(1);
        emit last->activated(1);
        QCOMPARE(session.document()->find(area)->text.alignment, TextAlignment::justifyCenter);
        QVERIFY(panel.findChild<QToolButton *>("characterJustify")->isChecked());
        session.select({point});
        QVERIFY(!panel.findChild<QWidget *>("paragraphSection")->isVisible());
    }

    void theStyleButtonMakesAppliesAndMarksOverrides()
    {
        EditorSession session;
        session.createDocument({400, 300});
        PropertiesPanel panel(session);
        panel.show();
        session.addText({20, 100}, QStringLiteral("Title"));
        auto *button = panel.findChild<QToolButton *>("characterStyles");
        QVERIFY(button);
        QCOMPARE(button->text(), QStringLiteral("No style"));
        QMenu *menu = button->menu();
        entry(menu, "newCharacterStyle")->trigger();
        QCOMPARE(button->text(), QStringLiteral("Character Style 1"));
        QVERIFY(!entry(menu, "redefineStyle")->isEnabled());
        type(panel, "characterTrackingField", "60");
        QCOMPARE(button->text(), QStringLiteral("Character Style 1+"));
        QAction *redefine = entry(menu, "redefineStyle");
        QVERIFY(redefine->isEnabled());
        redefine->trigger();
        QCOMPARE(session.undoName(), QStringLiteral("Redefine Style"));
        QCOMPARE(button->text(), QStringLiteral("Character Style 1"));
        QCOMPARE(session.document()->textStyles.front().character.tracking, 60.0);
        // Another text takes the style from the list.
        const QUuid other = session.addText({20, 200}, QStringLiteral("Other"));
        entry(menu, "textStyle:Character Style 1")->trigger();
        QCOMPARE(session.document()->find(other)->text.tracking, 60.0);
        entry(menu, "detachStyle")->trigger();
        QCOMPARE(button->text(), QStringLiteral("No style"));
    }

    void openTypeDimsWhatTheFontLacks()
    {
        if (!FontFeatures::applicable())
            QSKIP("Qt before 6.7: the OpenType button is left out");
        EditorSession session;
        session.createDocument({400, 300});
        PropertiesPanel panel(session);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const QUuid id = session.addText({20, 100}, QStringLiteral("office 1/2"));
        panel.findChild<QPushButton *>("characterMore")->click();
        auto *button = panel.findChild<QToolButton *>("characterOpenType");
        QVERIFY(button && button->isVisible());
        button->click();
        auto *popover = panel.findChild<QFrame *>("openTypePopover");
        QVERIFY(popover);
        const QSet<QString> supported = FontFeatures::supported(session.document()->find(id)->text.font());
        for (const FontFeatures::Feature &feature : FontFeatures::offered()) {
            if (auto *box = popover->findChild<QCheckBox *>(QStringLiteral("openType:") + QLatin1String(feature.tag)))
                QCOMPARE(box->isEnabled(), supported.contains(QLatin1String(feature.tag)));
        }
        QVERIFY(popover->findChild<QCheckBox *>("openType:liga")->isChecked());
        auto *ss20 = popover->findChild<QToolButton *>("openType:ss20");
        QCOMPARE(ss20->isEnabled(), supported.contains(QStringLiteral("ss20")));
        // Turning a feature off is one named step, whatever the font has.
        popover->findChild<QCheckBox *>("openType:liga")->click();
        QCOMPARE(session.document()->find(id)->text.features.at(QStringLiteral("liga")), 0);
        QCOMPARE(session.undoName(), QStringLiteral("OpenType Features"));
        QVERIFY(!popover->findChild<QCheckBox *>("openType:liga")->isChecked());
        popover->close();
    }

    void typeStylesPanelListsAndApplies()
    {
        EditorSession session;
        session.createDocument({400, 300});
        session.addText({20, 100}, QStringLiteral("Big"));
        session.updateText([](TextContent &text) { text.size = 60; }, QStringLiteral("Font Size"));
        TextStylesPanel panel([&session]() -> EditorSession * { return &session; });
        panel.show();
        auto *list = panel.findChild<QListWidget *>("typeStylesList");
        QCOMPARE(list->count(), 2);
        panel.findChild<QAction *>("typeStylesNewParagraph")->trigger();
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->item(1)->text(), QStringLiteral("Paragraph Style 1"));
        QVERIFY(list->item(1)->font().bold() || list->item(1)->font().weight() > QFont::Normal);
        VectorObject small = session.textObject({20, 200}, QStringLiteral("Small"));
        small.text.size = 24;
        const QUuid other = session.addObject(small, QStringLiteral("Type"));
        QCOMPARE(session.document()->find(other)->text.size, 24.0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualItemRect(list->item(1)).center());
        QTRY_COMPARE(session.document()->find(other)->text.size, 60.0);
        QCOMPARE(session.undoName(), QStringLiteral("Apply Style"));
        // Renamed in place.
        list->item(1)->setText(QStringLiteral("Display"));
        QTRY_COMPARE(session.document()->textStyles.front().name, QStringLiteral("Display"));
        QCOMPARE(session.undoName(), QStringLiteral("Rename Style"));
    }

    void findFontListsMissingFontsAndReplacesThemAll()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject made = session.textObject({20, 40}, QStringLiteral("Lost"));
        made.text.family = QStringLiteral("A Font Nobody Has Installed");
        const QUuid first = session.addObject(made, QStringLiteral("Type"));
        made.id = QUuid::createUuid();
        const QUuid second = session.addObject(made, QStringLiteral("Type"));
        session.addText({20, 200}, QStringLiteral("Here"));
        QWidget window;
        QDialog *dialog = ObjectDialogs::findFont(session, &window);
        auto *list = dialog->findChild<QListWidget *>("findFontList");
        QCOMPARE(list->count(), 2);
        QListWidgetItem *missing = list->findItems(QStringLiteral("A Font Nobody Has Installed (missing)"), Qt::MatchExactly).value(0);
        QVERIFY(missing);
        QVERIFY(!missing->icon().isNull());
        QCOMPARE(list->currentItem(), missing);
        QVERIFY(dialog->findChild<QLabel *>("findFontNote")->text().startsWith(QStringLiteral("One font")));
        auto *replacement = dialog->findChild<QFontComboBox *>("findFontReplacement");
        const QString installed = replacement->currentFont().family();
        dialog->findChild<QPushButton *>("findFontReplaceAll")->click();
        QCOMPARE(session.document()->find(first)->text.family, installed);
        QCOMPARE(session.document()->find(second)->text.family, installed);
        QCOMPARE(session.undoName(), QStringLiteral("Replace Font"));
        QVERIFY(list->findItems(QStringLiteral("(missing)"), Qt::MatchContains).isEmpty());
        session.undo();
        QCOMPARE(session.document()->find(first)->text.family, made.text.family);
        QCOMPARE(session.document()->find(second)->text.family, made.text.family);
        dialog->close();
    }

    void theMenusReachFindFontAndTypeStyles()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        workspace.createDocument({400, 300});
        window.show();
        Menus &menus = *window.menus();
        QVERIFY(menus.action("findFont")->isEnabled());
        QCOMPARE(menus.action("findFont")->text(), QStringLiteral("Find/Replace Font…"));
        menus.action("showTypeStyles")->trigger();
        QTRY_VERIFY(window.findChild<QListWidget *>("typeStylesList") != nullptr);
    }
};

QTEST_MAIN(TypePanelsTests)
#include "TypePanelsTests.moc"
