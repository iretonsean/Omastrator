#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Cli.h"
#include "Agent/Island.h"
#include "Logging.h"
#include "UI/DesignController.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SliderSnap.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QTimer>

int main(int argc, char **argv)
{
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{category}: %{message}"));
    // `omastrator agent …`, `--mcp`, `status`, `island`, `design` and the rest start no GUI.
    if (argc > 1 && Cli::handles(argv[1])) {
        QCoreApplication application(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("Omastrator"));
        QCoreApplication::setApplicationVersion(QStringLiteral(OMASTRATOR_VERSION));
        return Cli::run(QCoreApplication::arguments().mid(1));
    }
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Omastrator"));
    QApplication::setApplicationVersion(QStringLiteral(OMASTRATOR_VERSION));
    // The desktop entry's name: icons and windows find each other.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.iretonsean.Omastrator"));
    QStringList files = QApplication::arguments().mid(1);
    // `--daemon`: in the background, owning documents, the agent socket and the overlays; no window until asked.
    const bool background = files.removeAll(QStringLiteral("--daemon")) > 0;
    // One Omastrator: when one is running, it shows its window with the files, and this one ends.
    if (Island::appIsRunning()) {
        if (background)
            return 0;
        QJsonArray paths;
        for (const QString &file : files)
            paths.append(QFileInfo(file).absoluteFilePath());
        try {
            AgentClient::Connection connection;
            connection.call(QStringLiteral("show_window"), {{"files", paths}}, 5000);
            return 0;
        } catch (const AgentProtocol::Error &) {
            // An older Omastrator without show_window: this one opens on its own, as before.
        }
    }
    qCInfo(lcApp).noquote() << "Omastrator" << OMASTRATOR_VERSION << "on Qt" << qVersion() << "platform" << QGuiApplication::platformName()
                            << (background ? "in the background" : "");
    // The desktop's colours, retinted when the theme switches.
    OmarchyTheme theme;
    // Slider knobs snap to a click on the track.
    SliderSnap::install();
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    // Agents reach the open document here; a second Omastrator reports why it can't in Help ▸ Connect an Agent….
    window.agent()->startServer();
    // Design mode everywhere: the overlays and the island's design mode (docs/ANYWHERE.md).
    window.agent()->designMode().start();
    if (background) {
        // Closing the window hides it; the overlays and the Desk stay.
        window.setProperty("background", true);
        QApplication::setQuitOnLastWindowClosed(false);
    } else {
        window.show();
    }
    // `OMASTRATOR_SNAPSHOT=file.png`: a picture of the window once it has settled, then quit. For checking the
    // look without a screen (with QT_QPA_PLATFORM=offscreen and XDG_RUNTIME_DIR apart from a running Omastrator).
    if (const QString snapshot = qEnvironmentVariable("OMASTRATOR_SNAPSHOT"); !snapshot.isEmpty()) {
        window.resize(1440, 900);
        window.show();
        QTimer::singleShot(1500, &window, [&window, snapshot] {
            const bool saved = window.grab().save(snapshot);
            qCInfo(lcApp).noquote() << "snapshot" << (saved ? "saved to" : "failed:") << snapshot;
            QApplication::exit(saved ? 0 : 1);
        });
    }
    // Connected cloud storage, listed in the background for Open and Save As.
    workspace.cloud().refreshRemotes();
    // Files named at launch: documents, SVGs and pictures.
    if (!files.isEmpty()) {
        workspace.receive(files);
        window.show();
    }
    return QApplication::exec();
}
