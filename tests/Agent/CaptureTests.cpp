#include "Agent/AgentProtocol.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "Agent/Capture.h"
#include "Agent/Island.h"
#include "Document/PathOperations.h"
#include "FakeAgentHost.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

// Phase 2 of docs/OS-SUITE.md: Capture mode, with fakes for hyprpicker, slurp, grim and wl-paste.
namespace {
struct Backend {
    FakeAgentHost host;
    AgentTools tools{host};
    AgentServer server{tools};
};

const QString svg = QStringLiteral(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 50'><rect width='40' height='50' fill='#ff0000'/>"
    "<circle cx='75' cy='25' r='20' fill='#0000ff'/></svg>");

void script(const QString &path, const QByteArray &body)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
}

class CaptureTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QThread m_thread;
    QObject *m_anchor = nullptr;
    Backend *m_backend = nullptr;

    template<typename Work>
    void onBackend(Work work)
    {
        QMetaObject::invokeMethod(m_anchor, work, Qt::BlockingQueuedConnection);
    }

    int capture(const QStringList &args, QString *out = nullptr, QString *err = nullptr)
    {
        QString output, errors;
        QTextStream outStream(&output), errStream(&errors);
        const int code = Island::runCli(QStringList{QStringLiteral("capture")} + args, outStream, errStream);
        outStream.flush();
        errStream.flush();
        if (out)
            *out = output.trimmed();
        if (err)
            *err = errors.trimmed();
        return code;
    }

    QString fake(const QString &name) const { return m_directory.filePath(QStringLiteral("fake-") + name); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_APP", "/bin/true");
        script(fake(QStringLiteral("hyprpicker")), "printf '%s\\n' \"$FAKE_COLOR\"\n");
        script(fake(QStringLiteral("slurp")), "[ -n \"$FAKE_REGION\" ] || exit 1\nprintf '%s\\n' \"$FAKE_REGION\"\n");
        script(fake(QStringLiteral("grim")), "printf '%s' \"$2\" > \"$FAKE_GRIM_ARGS\"\ncp \"$FAKE_PNG\" \"$3\"\n");
        script(fake(QStringLiteral("wl-paste")), "if [ \"$1\" = --list-types ]; then printf \"$FAKE_TYPES\"; else cat \"$FAKE_CLIP\"; fi\n");
        for (const char *name : {"hyprpicker", "slurp", "grim", "wl-paste"}) {
            const QString variable = QStringLiteral("OMASTRATOR_") + QString::fromLatin1(name).toUpper().replace(QLatin1Char('-'), QLatin1Char('_'));
            qputenv(variable.toUtf8().constData(), fake(QLatin1String(name)).toUtf8());
        }
        m_anchor = new QObject;
        m_anchor->moveToThread(&m_thread);
        m_thread.start();
        QString failure;
        onBackend([&] {
            m_backend = new Backend;
            failure = m_backend->server.listen();
        });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void cleanupTestCase()
    {
        onBackend([&] { delete m_backend; });
        QMetaObject::invokeMethod(m_anchor, &QObject::deleteLater);
        m_thread.quit();
        m_thread.wait();
    }

    void pickedColourBecomesFillStrokeOrSwatch()
    {
        QUuid shape;
        onBackend([&] {
            m_backend->host.editor.createDocument({200, 100});
            shape = m_backend->host.editor.addPath(Shapes::rectangle({10, 10, 40, 40}), QStringLiteral("Rectangle"));
        });
        QString out, err;
        qputenv("FAKE_COLOR", "#FF6600");
        QCOMPARE(capture({QStringLiteral("color")}, &out), 0);
        QCOMPARE(out, QStringLiteral("Fill: #ff6600"));
        QCOMPARE(Island::read().activity, QStringLiteral("Fill: #ff6600"));
        onBackend([&] {
            EditorSession &editor = m_backend->host.editor;
            QCOMPARE(editor.document()->find(shape)->fill, Paint::solid(QColor(0xff, 0x66, 0x00)));
            QCOMPARE(editor.undoName(), QStringLiteral("Fill"));
            QVERIFY(!editor.isInteracting());
        });
        qputenv("FAKE_COLOR", "#00aa00");
        QCOMPARE(capture({QStringLiteral("color"), QStringLiteral("stroke")}, &out), 0);
        onBackend([&] {
            const VectorObject *object = m_backend->host.editor.document()->find(shape);
            QCOMPARE(object->stroke.paint, Paint::solid(QColor(0x00, 0xaa, 0x00)));
            QCOMPARE(m_backend->host.editor.undoName(), QStringLiteral("Stroke"));
            // Nothing selected: the colour waits for the next shape.
            m_backend->host.editor.deselectAll();
        });
        qputenv("FAKE_COLOR", "#123456");
        QCOMPARE(capture({QStringLiteral("color")}), 0);
        onBackend([&] { QCOMPARE(m_backend->host.editor.defaultFill(), Paint::solid(QColor(0x12, 0x34, 0x56))); });

        QCOMPARE(capture({QStringLiteral("color"), QStringLiteral("swatch")}, &out), 0);
        QCOMPARE(out, QStringLiteral("Swatch added: #123456"));
        QCOMPARE(capture({QStringLiteral("color"), QStringLiteral("swatch")}, &out), 0);
        QVERIFY(out.contains(QLatin1String("already")));
        onBackend([&] {
            const auto &groups = m_backend->host.library.groups();
            QCOMPARE(groups.size(), size_t(1));
            QCOMPARE(groups.front().name, QStringLiteral("Swatches"));
            QCOMPARE(groups.front().swatches.size(), size_t(1));
        });

        // Escape in hyprpicker is no error and changes nothing.
        qputenv("FAKE_COLOR", "");
        QCOMPARE(capture({QStringLiteral("color")}, &out), 0);
        QCOMPARE(out, QStringLiteral("No colour picked."));
        QCOMPARE(capture({QStringLiteral("color"), QStringLiteral("background")}, &out, &err), 1);
    }

    void aMissingProgramIsNamedWithItsPackage()
    {
        const QByteArray saved = qgetenv("OMASTRATOR_HYPRPICKER");
        qputenv("OMASTRATOR_HYPRPICKER", "/nonexistent/hyprpicker");
        QString err;
        QCOMPARE(capture({QStringLiteral("color")}, nullptr, &err), 1);
        QVERIFY(err.contains(QLatin1String("sudo pacman -S hyprpicker")));
        QVERIFY(Island::read().activity.contains(QLatin1String("isn't installed")));
        qputenv("OMASTRATOR_HYPRPICKER", saved);
        QVERIFY(Capture::missing(QStringLiteral("wl-paste")).contains(QLatin1String("wl-clipboard")));
    }

    void screenshotOpensAndTraces()
    {
        QImage image(40, 30, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter(&image).fillRect(QRect(5, 5, 15, 20), Qt::black);
        const QString png = m_directory.filePath(QStringLiteral("region.png"));
        QVERIFY(image.save(png));
        qputenv("FAKE_PNG", png.toUtf8());
        qputenv("FAKE_GRIM_ARGS", m_directory.filePath(QStringLiteral("grim-args")).toUtf8());
        qputenv("FAKE_REGION", "100,200 40x30");
        QString out;
        QCOMPARE(capture({QStringLiteral("screenshot")}, &out), 0);
        QVERIFY2(out.startsWith(QLatin1String("Traced")), qPrintable(out));
        QFile args(m_directory.filePath(QStringLiteral("grim-args")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(args.readAll(), QByteArray("100,200 40x30"));
        // Kept with the app's data, so Vectorize with AI can read it later.
        QCOMPARE(QDir(Capture::capturesDirectory()).entryList(QDir::Files).size(), 1);
        QVERIFY(Capture::capturesDirectory().startsWith(m_directory.path()));
        onBackend([&] {
            EditorSession &editor = m_backend->host.editor;
            QCOMPARE(editor.document()->size, QSizeF(40, 30));
            QCOMPARE(editor.undoName(), QStringLiteral("Image Trace"));
            QVERIFY(!editor.selectedImage());
            QCOMPARE(m_backend->tools.status()["offer"].toString(), QStringLiteral("vectorize"));
            QVERIFY(m_backend->tools.pendingCapture()->imagePath.startsWith(Capture::capturesDirectory()));
            // Undo brings back the screenshot itself.
            editor.undo();
            QVERIFY(!m_backend->tools.status().contains("offer"));
        });

        qputenv("FAKE_REGION", "");
        QCOMPARE(capture({QStringLiteral("screenshot")}, &out), 0);
        QCOMPARE(out, QStringLiteral("No region chosen."));
    }

    void captureWindowTakesTheFocusedApp()
    {
        // hyprctl stands in: the focused window is $FAKE_WINDOW.
        script(fake(QStringLiteral("hyprctl")), "[ \"$1\" = activewindow ] && printf '%s' \"$FAKE_WINDOW\"\n");
        qputenv("OMASTRATOR_HYPRCTL", fake(QStringLiteral("hyprctl")).toUtf8());
        qputenv("FAKE_WINDOW", R"({"at": [10, 20], "size": [40, 30], "class": "org.gnome.Settings", "title": "Settings"})");
        QString out, err;
        QCOMPARE(capture({QStringLiteral("window")}, &out, &err), 0);
        QVERIFY2(out.startsWith(QLatin1String("Captured Settings")), qPrintable(out + err));
        QFile args(m_directory.filePath(QStringLiteral("grim-args")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(args.readAll(), QByteArray("10,20 40x30"));
        onBackend([&] { QCOMPARE(m_backend->host.editor.document()->size, QSizeF(40, 30)); });
        qputenv("FAKE_WINDOW", "{}");
        QCOMPARE(capture({QStringLiteral("window")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("No window has focus")));
        qunsetenv("OMASTRATOR_HYPRCTL");
    }

    void pasteSvgMakesEditablePaths()
    {
        const QString clip = m_directory.filePath(QStringLiteral("clip"));
        QFile file(clip);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(svg.toUtf8());
        file.close();
        qputenv("FAKE_CLIP", clip.toUtf8());
        qputenv("FAKE_TYPES", "text/html\\nimage/svg+xml\\n");
        onBackend([&] { m_backend->host.editor.closeDocument(); });
        QString out, err;
        QCOMPARE(capture({QStringLiteral("paste-svg")}, &out, &err), 0);
        QCOMPARE(out, QStringLiteral("Pasted 2 editable paths."));
        int undoSteps = 0;
        onBackend([&] {
            EditorSession &editor = m_backend->host.editor;
            QVERIFY(editor.hasDocument());
            QCOMPARE(editor.undoName(), QStringLiteral("Paste SVG"));
            QCOMPARE(editor.selection().size(), size_t(1));
            const QRectF bounds = editor.document()->bounds(editor.selection().front());
            QCOMPARE(bounds.center(), QPointF(editor.document()->size.width() / 2, editor.document()->size.height() / 2));
            while (editor.canUndo()) {
                editor.undo();
                ++undoSteps;
            }
        });
        QCOMPARE(undoSteps, 1);

        // Code editors copy SVG as text.
        qputenv("FAKE_TYPES", "text/plain;charset=utf-8\\n");
        QCOMPARE(capture({QStringLiteral("paste-svg")}, &out), 0);
        // Text that isn't SVG is refused plainly.
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("just words");
        file.close();
        QCOMPARE(capture({QStringLiteral("paste-svg")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("no SVG")));
        qputenv("FAKE_TYPES", "image/png\\n");
        QCOMPARE(capture({QStringLiteral("paste-svg")}, &out, &err), 1);
    }

    void pasteWaitsForAProposal()
    {
        onBackend([&] {
            EditorSession &editor = m_backend->host.editor;
            editor.createDocument({100, 100});
            m_backend->tools.call(QStringLiteral("insert_svg"), {{"svg", svg}});
            bool refused = false;
            try {
                m_backend->tools.call(QStringLiteral("paste_svg"), {{"svg", svg}});
            } catch (const AgentProtocol::Error &failure) {
                refused = failure.code == AgentProtocol::busy && failure.message().contains(QLatin1String("Enter"));
            }
            QVERIFY(refused);
            editor.cancelInteraction();
        });
    }

    void themeSwatchesReplaceTheirGroup()
    {
        QTemporaryDir state;
        QDir(state.path()).mkpath(QStringLiteral("theme"));
        QFile name(state.filePath(QStringLiteral("theme.name")));
        QVERIFY(name.open(QIODevice::WriteOnly));
        name.write("tokyo-night\n");
        name.close();
        QFile colors(state.filePath(QStringLiteral("theme/colors.toml")));
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("mode = \"dark\"\n# Accent.\naccent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\nbright_red = '#ff7a93'\nnot_a_colour = \"soon\"\n");
        colors.close();
        qputenv("OMASTRATOR_THEME_DIR", state.filePath(QStringLiteral("theme")).toUtf8());
        QString out;
        QCOMPARE(capture({QStringLiteral("theme-swatches")}, &out), 0);
        QCOMPARE(out, QStringLiteral("Loaded 3 swatches from Tokyo-night."));
        QCOMPARE(capture({QStringLiteral("theme-swatches")}), 0);
        onBackend([&] {
            const auto &groups = m_backend->host.library.groups();
            const auto theme = std::find_if(groups.begin(), groups.end(), [](const SwatchGroup &each) { return each.name == QLatin1String("Omarchy: Tokyo-night"); });
            QVERIFY(theme != groups.end());
            QCOMPARE(theme->swatches.size(), size_t(3));
            QCOMPARE(theme->swatches[2].name, QStringLiteral("Bright Red"));
        });
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("nowhere")).toUtf8());
        QCOMPARE(capture({QStringLiteral("theme-swatches")}), 1);
    }

    void swatchesKeepAcrossLaunches()
    {
        {
            Swatches library(QStringLiteral("testSwatches"));
            QCOMPARE(library.add(QStringLiteral("Brand"), {{QStringLiteral("Ink"), QColor(0x11, 0x22, 0x33)}, {QString(), Qt::red}, {QString(), Qt::red}}), 2);
            QCOMPARE(library.groups().front().swatches[1].name, QStringLiteral("#ff0000"));
        }
        Swatches again(QStringLiteral("testSwatches"));
        QCOMPARE(again.groups().size(), size_t(1));
        QCOMPARE(again.groups().front().swatches.front().color, QColor(0x11, 0x22, 0x33));
        again.remove(QStringLiteral("Brand"), 0);
        QCOMPARE(again.groups().front().swatches.size(), size_t(1));
        again.removeGroup(QStringLiteral("Brand"));
        QVERIFY(again.groups().empty());
        QSettings().remove(QStringLiteral("testSwatches"));
    }
};

QTEST_GUILESS_MAIN(CaptureTests)
#include "CaptureTests.moc"
