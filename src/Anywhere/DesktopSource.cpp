#include "Anywhere/DesktopSource.h"
#include "Agent/Capture.h"
#include "Anywhere/Inspect.h"
#include "Anywhere/Lift.h"
#include <QAccessible>
#include <QApplication>
#include <QJsonArray>
#include <QMetaEnum>
#include <QProcess>
#include <QWidget>
#include <QWindow>

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

namespace {

// org.a11y.Status.IsEnabled on the session's accessibility bus, through busctl.
QStringList a11yStatus(const QString &verb)
{
    return {QStringLiteral("--user"), verb, QStringLiteral("org.a11y.Bus"), QStringLiteral("/org/a11y/bus"),
            QStringLiteral("org.a11y.Status"), QStringLiteral("IsEnabled")};
}

std::optional<bool> a11yEnabled()
{
    QProcess busctl;
    busctl.start(QStringLiteral("busctl"), a11yStatus(QStringLiteral("get-property")));
    if (!busctl.waitForFinished(1000) || busctl.exitCode() != 0)
        return std::nullopt;
    return busctl.readAllStandardOutput().trimmed() == "b true";
}

void setA11yEnabled(bool on)
{
    // Fire and forget: Qt apps hear the change and register on their own.
    QProcess::startDetached(QStringLiteral("busctl"), a11yStatus(QStringLiteral("set-property"))
                                                          << QStringLiteral("b") << QLatin1String(on ? "true" : "false"));
}

// "PushButton" → "push button", as AT-SPI names roles.
QString roleName(QAccessible::Role role)
{
    const QString key = QString::fromLatin1(QMetaEnum::fromType<QAccessible::Role>().valueToKey(role));
    QString name;
    for (const QChar c : key) {
        if (c.isUpper() && !name.isEmpty())
            name += QLatin1Char(' ');
        name += c.toLower();
    }
    return name;
}

// Omastrator's own windows, read in-process: this thread is the one that would have to
// answer the AT-SPI helper, so asking over the bus would only time out.
std::optional<QJsonObject> ownAccessible(const Hyprland::Window &window, QPoint windowPoint)
{
    QWidget *top = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && widget->windowHandle() && widget->windowHandle()->title() == window.title) {
            top = widget;
            break;
        }
    }
    if (!top)
        return std::nullopt;
    QWidget *widget = top->childAt(windowPoint);
    if (!widget)
        widget = top;
    QAccessibleInterface *node = QAccessible::queryAccessibleInterface(widget);
    const QRect rect(widget->mapTo(top, QPoint(0, 0)), widget->size());
    QJsonObject answer{{"role", node ? roleName(node->role()) : QStringLiteral("widget")},
                       {"name", node ? node->text(QAccessible::Name) : widget->objectName()},
                       {"rect", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()}},
                       {"app", QCoreApplication::applicationName()},
                       {"fontFamily", widget->font().family()},
                       {"fontSize", widget->font().pointSizeF()},
                       {"fontWeight", QString::number(widget->font().weight())},
                       {"color", widget->palette().color(widget->foregroundRole()).name()},
                       {"background", widget->palette().color(widget->backgroundRole()).name()}};
    if (node && node->textInterface())
        answer["text"] = node->textInterface()->text(0, std::min(node->textInterface()->characterCount(), 80));
    else if (node)
        answer["text"] = node->text(QAccessible::Value);
    return answer;
}

} // namespace

void SystemSource::wantAccessibility(bool on)
{
    if (on) {
        if (a11yEnabled() == false) {
            setA11yEnabled(true);
            m_enabledAccessibility = true;
        }
    } else if (m_enabledAccessibility) {
        setA11yEnabled(false);
        m_enabledAccessibility = false;
    }
}

std::optional<QJsonObject> SystemSource::accessible(const Hyprland::Window &window, QPoint windowPoint)
{
    if (window.pid == QCoreApplication::applicationPid())
        return ownAccessible(window, windowPoint);
    return Inspect::accessibleAt(window.pid, windowPoint);
}

std::vector<Hyprland::Layer> SystemSource::layers()
{
    return Hyprland::parseLayers(Hyprland::query(QStringLiteral("layers")));
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

std::optional<QJsonObject> SystemSource::accessibleTree(const Hyprland::Window &window, int maxNodes, QString *error)
{
    return Lift::accessibleTree(window.pid, maxNodes, error);
}
