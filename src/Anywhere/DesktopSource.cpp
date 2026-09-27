#include "Anywhere/DesktopSource.h"
#include "Agent/Capture.h"
#include "Anywhere/Inspect.h"

namespace {
QString geometry(const QRect &rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}
}

std::optional<QPoint> SystemSource::cursor()
{
    return Hyprland::parseCursor(Hyprland::query(QStringLiteral("cursorpos")));
}

std::vector<Hyprland::Window> SystemSource::windows()
{
    return Hyprland::parseClients(Hyprland::query(QStringLiteral("clients")));
}

std::vector<Hyprland::Monitor> SystemSource::monitors()
{
    return Hyprland::parseMonitors(Hyprland::query(QStringLiteral("monitors")));
}

std::optional<QJsonObject> SystemSource::accessible(const Hyprland::Window &window, QPoint windowPoint)
{
    return Inspect::accessibleAt(window.pid, windowPoint);
}

std::optional<QColor> SystemSource::pixel(QPoint point)
{
    const Capture::Run run = Capture::run(QStringLiteral("grim"), {QStringLiteral("-g"), geometry(QRect(point, QSize(1, 1))), QStringLiteral("-t"),
                                                                   QStringLiteral("ppm"), QStringLiteral("-")},
                                          2000);
    if (!run.error.isEmpty() || run.exitCode != 0)
        return std::nullopt;
    return Inspect::parsePpm(run.out);
}

QImage SystemSource::grab(const QRect &rect, QString *error)
{
    const Capture::Run run = Capture::run(QStringLiteral("grim"), {QStringLiteral("-g"), geometry(rect), QStringLiteral("-t"), QStringLiteral("png"),
                                                                   QStringLiteral("-")},
                                          10'000);
    if (!run.error.isEmpty() || run.exitCode != 0) {
        if (error)
            *error = run.error.isEmpty() ? QStringLiteral("grim could not capture the screen.") : run.error;
        return {};
    }
    QImage image = QImage::fromData(run.out, "PNG");
    if (image.isNull() && error)
        *error = QStringLiteral("grim's picture couldn't be read.");
    return image;
}
