#include "Logging.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SliderSnap.h"
#include <QApplication>

int main(int argc, char **argv)
{
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{category}: %{message}"));
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
    window.show();
    // Files named at launch: documents, SVGs and pictures.
    workspace.receive(QApplication::arguments().mid(1));
    return QApplication::exec();
}
