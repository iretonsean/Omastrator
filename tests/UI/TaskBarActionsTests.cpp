#include "Canvas/TaskBar.h"
#include "Document/PathOperations.h"
#include "UI/AgentPanels.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/TaskBarActions.h"
#include "../Agent/FakeAgents.h"
#include <QHelpEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

// The contextual task bar's contents: the right actions per selection, and Ask AI… reaching the agent.
namespace {
// Stands in for `omarchy`: names $FAKE_AGENT as the default and records the prompt.
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT/prompt\"; exit 0; fi\n"
                                    "exit 2\n";

struct Window {
    ProjectWorkspace workspace;
    ProjectWorkspaceView view{workspace};

    Window()
    {
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            qWarning("window never exposed");
        workspace.createDocument(QSizeF(400, 300));
        session().usesSmartGuides = false;
        session().actualSize();
    }
    EditorSession &session() { return workspace.current().session; }
    EditorCanvas &canvas() { return view.content()->canvas(); }
    TaskBar *bar() { return canvas().findChild<TaskBar *>(); }
    QUuid box(double x, double y = 20) { return session().addPath(Shapes::rectangle(QRectF(x, y, 60, 40)), QStringLiteral("Box")); }

    // The bar's contents in order, the grip left out.
    QStringList shown()
    {
        QStringList names;
        QLayout *row = bar()->layout();
        for (int index = 1; index < row->count(); ++index) {
            if (QWidget *widget = row->itemAt(index)->widget(); widget && widget->objectName() != QLatin1String("taskBarSeparator"))
                names << widget->objectName();
        }
        return names;
    }
    template<typename T = QWidget>
    T *item(const QString &name)
    {
        return bar()->findChild<T *>(name);
    }
};

QString tip(QWidget *widget)
{
    QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), widget->mapToGlobal(QPoint(2, 2)));
    QApplication::sendEvent(widget, &help);
    return widget->toolTip();
}
}

class TaskBarActionsTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString prompt() const { return FakeAgents::read(m_directory.filePath(QStringLiteral("prompt"))); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        const QString script = m_directory.filePath(QStringLiteral("omarchy"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fakeOmarchy);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_OMARCHY", script.toUtf8());
        qputenv("FAKE_OUT", m_directory.path().toUtf8());
        qputenv("FAKE_AGENT", "sh");
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("omastrator.sock")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
    }

    void init()
    {
        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        for (const char *key : {"view/contextualTaskBar", "view/taskBarOffset", "view/taskBarPinned", "view/taskBarPinnedAt"})
            QSettings().remove(QString::fromLatin1(key));
        QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
        ShortcutSettings::shared().reload();
    }

    void eachSelectionKindGetsItsActions()
    {
        Window w;
        QVERIFY(w.bar());
        QVERIFY(!w.bar()->isVisible());
        const QUuid a = w.box(20);
        QVERIFY(w.bar()->isVisible());
        QCOMPARE(TaskBarActions::kind(w.session()), QStringLiteral("path"));
        QCOMPARE(w.shown(), (QStringList{"taskBarFill", "taskBarStroke", "taskBarEditPath", "taskBarPath", "taskBarAsk", "taskBarMore"}));

        const QUuid b = w.box(60, 40);
        w.session().select({a, b});
        QCOMPARE(w.shown(), (QStringList{"taskBarFill", "taskBarStroke", "taskBarPathfinder", "taskBarShapeBuilder", "taskBarAlign", "taskBar:group",
                                         "taskBarAsk", "taskBarMore"}));

        const QUuid text = w.session().addText(QPointF(200, 200), QStringLiteral("Hello"));
        QCOMPARE(TaskBarActions::kind(w.session()), QStringLiteral("text:point"));
        QCOMPARE(w.shown(), (QStringList{"taskBarFont", "taskBarSize", "taskBar:createOutlines", "taskBar:convertToAreaType", "taskBarAsk", "taskBarMore"}));
        QCOMPARE(w.item<QToolButton>(QStringLiteral("taskBar:convertToAreaType"))->text(), QStringLiteral("Area Type"));
        // The size field edits the type.
        auto *size = w.item<QLineEdit>(QStringLiteral("taskBarSizeField"));
        size->setText(QStringLiteral("36"));
        QTest::keyClick(size, Qt::Key_Return);
        QCOMPARE(w.session().document()->find(text)->text.size, 36.0);
        // Point to area: the button turns into its opposite.
        w.item<QToolButton>(QStringLiteral("taskBar:convertToAreaType"))->click();
        QCOMPARE(TaskBarActions::kind(w.session()), QStringLiteral("text:area"));
        QVERIFY(w.shown().contains(QStringLiteral("taskBar:convertToPointType")));

        w.session().select({a, text});
        QCOMPARE(TaskBarActions::kind(w.session()), QStringLiteral("objects"));
        QCOMPARE(w.shown().mid(0, 2), (QStringList{"taskBarAlign", "taskBar:group"}));

        QImage pixels(20, 20, QImage::Format_ARGB32_Premultiplied);
        pixels.fill(Qt::red);
        w.session().placeImage(pixels, QStringLiteral("Photo"), QPointF(300, 100));
        QCOMPARE(w.shown(), (QStringList{"taskBar:imageTraceMake", "taskBar:vectorizeWithAI", "taskBarAsk", "taskBarMore"}));
        QCOMPARE(w.item<QToolButton>(QStringLiteral("taskBar:imageTraceMake"))->text(), QStringLiteral("Image Trace"));
        QCOMPARE(w.item<QToolButton>(QStringLiteral("taskBar:vectorizeWithAI"))->text(), QStringLiteral("Vectorize with AI"));

        // Group runs the menu's Group, and the bar turns into the group's.
        w.session().select({a, b});
        w.item<QToolButton>(QStringLiteral("taskBar:group"))->click();
        QCOMPARE(w.session().undoName(), QStringLiteral("Group"));
        QCOMPARE(w.shown(), (QStringList{"taskBar:ungroup", "taskBarIsolate", "taskBarFill", "taskBarStroke", "taskBarAsk", "taskBarMore"}));
        const QUuid group = w.session().selection().front();
        w.item<QToolButton>(QStringLiteral("taskBarIsolate"))->click();
        QCOMPARE(w.canvas().isolatedGroup(), std::optional(group));
        w.canvas().exitIsolation();

        w.session().select({a, b});
        w.session().ungroupSelection();
        w.session().select({a, b});
        w.session().makeClippingMask();
        QCOMPARE(TaskBarActions::kind(w.session()), QStringLiteral("clipGroup"));
        QCOMPARE(w.shown().front(), QStringLiteral("taskBar:releaseClippingMask"));
    }

    void buttonsNameTheirKeysAsRemapped()
    {
        Window w;
        w.session().select({w.box(20), w.box(100)});
        auto *group = w.item<QToolButton>(QStringLiteral("taskBar:group"));
        QCOMPARE(group->accessibleName(), QStringLiteral("Group"));
        QCOMPARE(tip(group), QStringLiteral("Group (%1)").arg(QKeySequence(Qt::CTRL | Qt::Key_G).toString(QKeySequence::NativeText)));
        QHash<QString, ShortcutChord> values;
        values.insert(QStringLiteral("Menus:Group"), ShortcutChord(QStringLiteral("g"), 11));
        QVERIFY(ShortcutSettings::shared().save(values));
        QCOMPARE(tip(group), QStringLiteral("Group (%1)").arg(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_G).toString(QKeySequence::NativeText)));
        QCOMPARE(tip(w.item(QStringLiteral("taskBarShapeBuilder"))), QStringLiteral("Shape Builder (Shift+M)"));
        // A button follows its entry's state.
        QVERIFY(group->isEnabled());
        w.view.menus()->action(QStringLiteral("group"))->setEnabled(false);
        QVERIFY(!group->isEnabled());
    }

    void sitsInsideTheViewNearItsEdge()
    {
        Window w;
        // Low on the artboard, near the view's bottom right: the bar goes above, inside the canvas.
        w.session().zoomToFit();
        w.box(330, 250);
        TaskBar *bar = w.bar();
        QVERIFY(bar->isVisible());
        const QRect canvas = w.canvas().rect();
        QVERIFY(canvas.contains(bar->geometry()));
        QVERIFY(!bar->geometry().intersects(w.canvas().selectionViewRect()->toAlignedRect()));
    }

    void askAIEditsTheSelectionAsAProposal()
    {
        Window w;
        const QUuid a = w.box(20);
        auto *ask = w.item<QLineEdit>(QStringLiteral("taskBarAsk"));
        QVERIFY(ask && ask->isEnabled());
        QCOMPARE(ask->placeholderText(), QStringLiteral("Ask AI…"));
        QCOMPARE(ask->accessibleName(), QStringLiteral("Ask AI"));
        ask->setFocus();
        QTest::keyClicks(ask, QStringLiteral("Make it teal"));
        QTest::keyClick(ask, Qt::Key_Return);
        QVERIFY(prompt().contains(QStringLiteral("Edit with Instruction")));
        QVERIFY(prompt().contains(QStringLiteral("Make it teal")));
        QVERIFY(prompt().contains(QStringLiteral("It applies to the current selection only")));
        AgentBridge &bridge = *w.view.agent();
        QVERIFY(bridge.waiting());
        // While the agent works, and while its proposal waits, the bar steps aside.
        QVERIFY(!w.bar()->isVisible());
        bridge.tools().call(QStringLiteral("set_style"), {{"fill", "#008080"}});
        bridge.tools().call(QStringLiteral("proposal_finish"), {{"title", "Teal"}});
        QVERIFY(!bridge.waiting());
        QVERIFY(!w.bar()->isVisible());
        w.view.findChild<ProposalBar *>(QStringLiteral("proposalBar"))->findChild<QPushButton *>(QStringLiteral("proposalKeep"))->click();
        QCOMPARE(w.session().undoName(), QStringLiteral("AI: Teal"));
        QCOMPARE(w.session().document()->find(a)->fill, Paint::solid(QColor(0x00, 0x80, 0x80)));
        QVERIFY(w.bar()->isVisible());
    }

    // Letters typed into Ask AI are words, never the canvas's tool keys.
    void askAIKeepsItsKeys()
    {
        Window w;
        w.box(20);
        auto *ask = w.item<QLineEdit>(QStringLiteral("taskBarAsk"));
        QTest::mouseClick(ask, Qt::LeftButton, {}, QPoint(20, ask->height() / 2));
        QTRY_COMPARE(QApplication::focusWidget(), ask);
        for (const QChar letter : QStringLiteral("pvmx1")) {
            QWidget *target = QApplication::focusWidget();
            QTest::keyClick(target ? target : ask, letter.toLatin1());
        }
        QCOMPARE(ask->text(), QStringLiteral("pvmx1"));
        QCOMPARE(w.session().tool(), Tool::select);
        QCOMPARE(w.session().selection().size(), size_t(1));
    }

    void askAISaysWhyItCouldNot()
    {
        qputenv("FAKE_AGENT", "");
        Window w;
        w.box(20);
        auto *ask = w.item<QLineEdit>(QStringLiteral("taskBarAsk"));
        ask->setText(QStringLiteral("Make it teal"));
        QTest::keyClick(ask, Qt::Key_Return);
        QVERIFY(ask->property("failure").toString().startsWith(QLatin1String("Choose an agent")));
        QCOMPARE(ask->text(), QStringLiteral("Make it teal"));
        QVERIFY(w.bar()->isVisible());
        qputenv("FAKE_AGENT", "sh");
    }

    void moreOpensTheSelectionsMenu()
    {
        Window w;
        w.box(20);
        w.item<QToolButton>(QStringLiteral("taskBarMore"))->click();
        QMenu *menu = nullptr;
        QTRY_VERIFY((menu = w.canvas().findChild<QMenu *>(QStringLiteral("canvasContextMenu"))) && menu->isVisible());
        QCOMPARE(menu->actions().front()->objectName(), QStringLiteral("askAI"));
        menu->close();
    }

    void viewMenuTurnsItOffAndItStaysOff()
    {
        {
            Window w;
            w.box(20);
            QAction *toggle = w.view.menus()->action(QStringLiteral("contextualTaskBar"));
            QVERIFY(toggle->isCheckable() && toggle->isChecked());
            QCOMPARE(toggle->text(), QStringLiteral("Contextual Task Bar"));
            toggle->trigger();
            QVERIFY(!toggle->isChecked());
            QVERIFY(!w.bar()->isVisible());
        }
        Window again;
        again.box(20);
        QVERIFY(!again.bar()->isVisible());
        QVERIFY(!again.view.menus()->action(QStringLiteral("contextualTaskBar"))->isChecked());
        again.view.menus()->action(QStringLiteral("contextualTaskBar"))->trigger();
        QVERIFY(again.bar()->isVisible());
    }
};

QTEST_MAIN(TaskBarActionsTests)
#include "TaskBarActionsTests.moc"
