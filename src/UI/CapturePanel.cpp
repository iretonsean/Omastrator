#include "UI/CapturePanel.h"
#include "Agent/Hyprland.h"
#include <QCoreApplication>
#include <QGridLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString appProgram()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_APP");
    return overridden.isEmpty() ? QCoreApplication::applicationFilePath() : overridden;
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
    m_status->setObjectName(QStringLiteral("captureStatus"));
    m_status->setWordWrap(true);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(m_status);
    column->addStretch(1);
    connect(&m_process, &QProcess::finished, this, &CapturePanel::finished);
}

// A picker left open would hold the screen after the window goes.
CapturePanel::~CapturePanel()
{
    if (isCapturing()) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
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
    if (busy)
        m_status->setText(QStringLiteral("Capturing… Esc cancels."));
}
