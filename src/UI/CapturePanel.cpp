#include "UI/CapturePanel.h"
#include "Agent/Capture.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QJsonObject>
#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace {
QString appProgram()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_APP");
    return overridden.isEmpty() ? QCoreApplication::applicationFilePath() : overridden;
}

constexpr QSize thumbnailSize{150, 94};
constexpr int mapMs = 250;

bool isOurs(const Hyprland::Window &window)
{
    return window.pid == QCoreApplication::applicationPid() || window.className == QLatin1String("io.github.iretonsean.Omastrator");
}

QString geometryOf(const QRect &rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

// grim's picture of a window, or a null image; the file is only a hand-off, so it goes again.
QImage grabThumbnail(const Hyprland::Window &window)
{
    const QString folder = Capture::capturesDirectory();
    QDir().mkpath(folder);
    const QString path = QDir(folder).filePath(QStringLiteral("thumb-%1.png").arg(QString(window.address).remove(QStringLiteral("0x"))));
    const Capture::Run shot = Capture::run(QStringLiteral("grim"), {QStringLiteral("-g"), geometryOf(window.rect), QStringLiteral("-s"), QStringLiteral("0.5"), path}, 15'000);
    QImage image;
    if (shot.error.isEmpty() && shot.exitCode == 0)
        image.load(path);
    QFile::remove(path);
    return image.isNull() ? image : image.scaled(thumbnailSize * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QPixmap placeholder(const Hyprland::Window &window, const QPalette &palette, qreal ratio)
{
    QPixmap pixmap = QIcon::fromTheme(window.className.toLower()).pixmap(QSize(48, 48));
    if (!pixmap.isNull())
        return pixmap;
    // A neutral tile with the app's initial when the theme has no icon for it.
    pixmap = QPixmap(thumbnailSize * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(palette.color(QPalette::AlternateBase));
    QPainter painter(&pixmap);
    painter.setPen(palette.color(QPalette::PlaceholderText));
    QFont font = painter.font();
    font.setPixelSize(28);
    painter.setFont(font);
    painter.drawText(QRect(QPoint(), thumbnailSize), Qt::AlignCenter, window.className.left(1).toUpper());
    return pixmap;
}
}

CapturePanel::CapturePanel(QWidget *parent) : QWidget(parent), m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("capturePanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(8);
    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    struct Action {
        QString name, label, tip;
        QStringList args;
        bool stepAside;
    };
    const std::vector<Action> actions{
        {QStringLiteral("captureRegion"), QStringLiteral("Region"), QStringLiteral("Drag over any part of the screen; it opens here, traced"),
         {QStringLiteral("screenshot")}, true},
        {QStringLiteral("captureFill"), QStringLiteral("Fill Colour"), QStringLiteral("Pick a colour anywhere on screen for the selection's fill"),
         {QStringLiteral("color"), QStringLiteral("fill")}, true},
        {QStringLiteral("captureStroke"), QStringLiteral("Stroke Colour"), QStringLiteral("Pick a colour anywhere on screen for the selection's stroke"),
         {QStringLiteral("color"), QStringLiteral("stroke")}, true},
        {QStringLiteral("captureSwatch"), QStringLiteral("Swatch"), QStringLiteral("Pick a colour anywhere on screen and keep it as a swatch"),
         {QStringLiteral("color"), QStringLiteral("swatch")}, true},
        {QStringLiteral("capturePasteSvg"), QStringLiteral("Paste SVG"), QStringLiteral("The clipboard's SVG as editable paths"),
         {QStringLiteral("paste-svg")}, false},
        {QStringLiteral("captureTheme"), QStringLiteral("Theme Swatches"), QStringLiteral("The Omarchy theme's colours as a swatch group"),
         {QStringLiteral("theme-swatches")}, false},
    };
    for (size_t i = 0; i < actions.size(); ++i) {
        const Action &action = actions[i];
        auto *button = new QPushButton(action.label, this);
        button->setObjectName(action.name);
        button->setToolTip(action.tip);
        connect(button, &QPushButton::clicked, this, [this, action] { capture(action.args, action.stepAside); });
        grid->addWidget(button, int(i / 2), int(i % 2));
        m_buttons << button;
    }
    column->addLayout(grid);

    auto *heading = new QHBoxLayout;
    auto *windowsLabel = new QLabel(QStringLiteral("Windows"), this);
    windowsLabel->setForegroundRole(QPalette::PlaceholderText);
    m_refresh = new QPushButton(QStringLiteral("Refresh"), this);
    m_refresh->setObjectName(QStringLiteral("captureRefresh"));
    m_refresh->setToolTip(QStringLiteral("Read the open windows again"));
    connect(m_refresh, &QPushButton::clicked, this, &CapturePanel::refresh);
    heading->addWidget(windowsLabel, 1);
    heading->addWidget(m_refresh);
    column->addLayout(heading);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("captureWindows"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *cells = new QWidget;
    m_windowGrid = new QGridLayout(cells);
    m_windowGrid->setContentsMargins(0, 0, 0, 0);
    m_windowGrid->setSpacing(6);
    m_windowGrid->setColumnStretch(0, 1);
    m_windowGrid->setColumnStretch(1, 1);
    scroll->setWidget(cells);
    column->addWidget(scroll, 1);
    m_empty = new QLabel(QStringLiteral("No other windows are open."), this);
    m_empty->setObjectName(QStringLiteral("captureNoWindows"));
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    m_empty->hide();
    column->addWidget(m_empty);

    m_status->setObjectName(QStringLiteral("captureStatus"));
    m_status->setWordWrap(true);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(m_status);
    connect(&m_thumbnails, &QFutureWatcher<QList<QImage>>::finished, this, &CapturePanel::thumbnailsReady);
    connect(&m_grab, &QFutureWatcher<Grab>::finished, this, &CapturePanel::windowGrabbed);
    connect(&m_process, &QProcess::finished, this, &CapturePanel::finished);
}

// A picker left open would hold the screen after the window goes.
CapturePanel::~CapturePanel()
{
    if (isCapturing()) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    // The workers only run programs; wait so none outlives the panel.
    m_thumbnails.waitForFinished();
    m_grab.waitForFinished();
}

void CapturePanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void CapturePanel::refresh()
{
    ++m_generation;
    for (QToolButton *cell : std::as_const(m_cells))
        delete cell;
    m_cells.clear();
    std::vector<Hyprland::Window> windows = Hyprland::parseClients(Hyprland::query(QStringLiteral("clients")));
    const std::vector<Hyprland::Monitor> monitors = Hyprland::parseMonitors(Hyprland::query(QStringLiteral("monitors")));
    // Ours, hidden and unmapped windows, and special-workspace ones that aren't showing, aren't offered.
    std::erase_if(windows, [&](const Hyprland::Window &window) {
        return isOurs(window) || !window.mapped || window.hidden || window.rect.width() <= 0 || window.rect.height() <= 0
            || (window.workspace < 0 && !Hyprland::isShown(window, monitors));
    });
    std::stable_sort(windows.begin(), windows.end(), [](const auto &a, const auto &b) { return a.focusHistory < b.focusHistory; });
    std::vector<Hyprland::Window> visible;
    const qreal ratio = devicePixelRatioF();
    for (size_t i = 0; i < windows.size(); ++i) {
        const Hyprland::Window window = windows[i];
        auto *cell = new QToolButton;
        cell->setObjectName(QStringLiteral("captureWindow"));
        cell->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        cell->setIconSize(thumbnailSize);
        cell->setIcon(QIcon(placeholder(window, palette(), ratio)));
        const QString title = window.title.isEmpty() ? window.className : window.title;
        const QString label = window.className.isEmpty() || title == window.className ? title : window.className + QStringLiteral(" · ") + title;
        cell->setText(cell->fontMetrics().elidedText(label, Qt::ElideRight, thumbnailSize.width() + 10));
        cell->setToolTip(window.className + QLatin1Char('\n') + window.title);
        cell->setProperty("address", window.address);
        cell->setProperty("label", label);
        cell->setProperty("thumbnail", false);
        cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(cell, &QToolButton::clicked, this, [this, window] { captureWindow(window); });
        m_windowGrid->addWidget(cell, int(i / 2), int(i % 2));
        m_cells << cell;
        if (Hyprland::isShown(window, monitors))
            visible.push_back(window);
    }
    m_empty->setVisible(windows.empty());
    for (QToolButton *cell : std::as_const(m_cells))
        cell->setEnabled(!isCapturing());
    // The picture of a window that is on screen; the others keep their icon.
    m_thumbnails.setFuture(QtConcurrent::run([visible] {
        QList<QImage> images;
        for (const Hyprland::Window &window : visible)
            images << grabThumbnail(window);
        return images;
    }));
    m_thumbnailAddresses.clear();
    for (const Hyprland::Window &window : visible)
        m_thumbnailAddresses << window.address;
    m_thumbnailGeneration = m_generation;
}

void CapturePanel::thumbnailsReady()
{
    if (m_thumbnailGeneration != m_generation)
        return;
    const QList<QImage> images = m_thumbnails.result();
    for (qsizetype i = 0; i < images.size() && i < m_thumbnailAddresses.size(); ++i) {
        if (images[i].isNull())
            continue;
        for (QToolButton *cell : std::as_const(m_cells)) {
            if (cell->property("address").toString() == m_thumbnailAddresses[i]) {
                cell->setIcon(QIcon(QPixmap::fromImage(images[i])));
                cell->setProperty("thumbnail", true);
            }
        }
    }
}

void CapturePanel::captureWindow(const Hyprland::Window &window)
{
    if (isCapturing())
        return;
    m_grabbing = true;
    m_grabName = window.title.isEmpty() ? window.className : window.title;
    setBusy(true);
    const std::vector<Hyprland::Window> all = Hyprland::parseClients(Hyprland::query(QStringLiteral("clients")));
    const std::vector<Hyprland::Monitor> monitors = Hyprland::parseMonitors(Hyprland::query(QStringLiteral("monitors")));
    QString ours;
    for (const Hyprland::Window &other : all) {
        if (isOurs(other))
            ours = other.address;
    }
    const QJsonObject active = Hyprland::query(QStringLiteral("activeworkspace")).toObject();
    const QString returnTo = active.isEmpty() ? QString() : Hyprland::workspaceSelector(active[QStringLiteral("id")].toInt(), active[QStringLiteral("name")].toString());
    const bool shown = Hyprland::isShown(window, monitors);
    // Hyprland's calls and grim wait on other programs: off the UI thread.
    m_grab.setFuture(QtConcurrent::run([window, shown, returnTo, ours]() -> Grab {
        Grab grab;
        if (!shown)
            Hyprland::focusWorkspace(Hyprland::workspaceSelector(window.workspace, window.workspaceName));
        if (const QString why = Hyprland::focusWindow(window.address); !why.isEmpty())
            grab.error = QStringLiteral("Couldn't switch to that window.");
        // The window has to be drawn before grim can see it.
        QThread::msleep(mapMs);
        const QString folder = Capture::capturesDirectory();
        QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("window-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
        if (grab.error.isEmpty()) {
            const Capture::Run shot = Capture::run(QStringLiteral("grim"), {QStringLiteral("-g"), geometryOf(window.rect), path}, 30'000);
            if (!shot.error.isEmpty())
                grab.error = shot.error;
            else if (shot.exitCode != 0 || !QFileInfo::exists(path))
                grab.error = QStringLiteral("grim could not capture the window.");
            else
                grab.path = path;
        }
        // Back where the user was: the workspace, or focus on us when the window was on this one.
        if (!shown && !returnTo.isEmpty())
            Hyprland::focusWorkspace(returnTo);
        else if (shown && !ours.isEmpty())
            Hyprland::focusWindow(ours);
        return grab;
    }));
}

void CapturePanel::windowGrabbed()
{
    m_grabbing = false;
    const Grab grab = m_grab.result();
    if (!grab.error.isEmpty()) {
        setBusy(false);
        m_status->setText(grab.error);
        emit notice(grab.error);
        return;
    }
    // The CLI's own path opens and traces it, as for every other capture.
    capture({QStringLiteral("image"), grab.path}, false);
}

void CapturePanel::capture(const QStringList &args, bool stepAside)
{
    if (isCapturing())
        return;
    m_returnTo.clear();
    if (stepAside) {
        // The window usually covers what's to be picked: go to the previous workspace, and come back after.
        const QJsonObject active = Hyprland::query(QStringLiteral("activeworkspace")).toObject();
        if (!active.isEmpty()) {
            m_returnTo = Hyprland::workspaceSelector(active[QStringLiteral("id")].toInt(), active[QStringLiteral("name")].toString());
            if (!Hyprland::focusWorkspace(QStringLiteral("previous")).isEmpty())
                m_returnTo.clear();
        }
    }
    setBusy(true);
    m_process.start(appProgram(), QStringList{QStringLiteral("island"), QStringLiteral("capture")} + args);
    if (!m_process.waitForStarted(3000)) {
        setBusy(false);
        if (!m_returnTo.isEmpty())
            Hyprland::focusWorkspace(m_returnTo);
        emit notice(QStringLiteral("Couldn't start the capture."));
    }
}

void CapturePanel::finished()
{
    if (!m_returnTo.isEmpty())
        Hyprland::focusWorkspace(m_returnTo);
    m_returnTo.clear();
    setBusy(false);
    // The command prints its outcome: the last line, success or not.
    const QString out = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    const QString err = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    const QString text = (m_process.exitCode() == 0 ? out : err).section(QLatin1Char('\n'), -1);
    m_status->setText(text);
    if (!text.isEmpty())
        emit notice(text);
}

void CapturePanel::setBusy(bool busy)
{
    for (QPushButton *button : m_buttons)
        button->setEnabled(!busy);
    m_refresh->setEnabled(!busy);
    for (QToolButton *cell : std::as_const(m_cells))
        cell->setEnabled(!busy);
    if (busy)
        m_status->setText(QStringLiteral("Capturing… Esc cancels."));
}
