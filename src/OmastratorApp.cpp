#include "Agent/AgentClient.h"
#include "Logging.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SliderSnap.h"
#include <QApplication>
#include <QCoreApplication>
#include <cstring>

int main(int argc, char **argv)
{
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{category}: %{message}"));
    // `omastrator agent …` and `omastrator --mcp` talk to a running app and start no GUI.
    if (argc > 1 && (std::strcmp(argv[1], "agent") == 0 || std::strcmp(argv[1], "--mcp") == 0)) {
        QCoreApplication application(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("Omastrator"));
        QCoreApplication::setApplicationVersion(QStringLiteral(OMASTRATOR_VERSION));
        if (std::strcmp(argv[1], "--mcp") == 0)
            return AgentClient::runMcp();
        return AgentClient::runCli(QCoreApplication::arguments().mid(2));
    }
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Omastrator"));
    QApplication::setApplicationVersion(QStringLiteral(OMASTRATOR_VERSION));
    // The desktop entry's name: icons and windows find each other.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.iretonsean.Omastrator"));
    qCInfo(lcApp).noquote() << "Omastrator" << OMASTRATOR_VERSION << "on Qt" << qVersion() << "platform" << QGuiApplication::platformName();
    // The desktop's colours, retinted when the theme switches.
    OmarchyTheme theme;
    // Slider knobs snap to a click on the track.
    SliderSnap::install();
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    // Agents reach the open document here; a second Omastrator reports why it can't in Help ▸ Connect an Agent….
    window.agent()->startServer();
    window.show();
    // Files named at launch: documents, SVGs and pictures.
    workspace.receive(QApplication::arguments().mid(1));
    return QApplication::exec();
}
