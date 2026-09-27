#include "ContentView.h"
#include "IO/ProjectStore.h"
#include "Logging.h"
#include "UI/AgentBridge.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspace.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QSettings>

const QString ContentView::layersKey = QStringLiteral("showsLayersPanel");
const QString ContentView::propertiesKey = QStringLiteral("showsPropertiesPanel");

namespace {
// The suffixes Open reads, from its dialog's filters.
bool readable(const QString &path)
{
    static const QRegularExpression pattern(QStringLiteral("\\*\\.(\\w+)"));
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const QString &filter : ProjectWorkspace::openFilters()) {
        for (const QRegularExpressionMatch &match : pattern.globalMatch(filter)) {
            if (match.captured(1).toLower() == suffix)
                return true;
        }
    }
    return false;
}

QStringList droppedFiles(const QMimeData &data)
{
    QStringList paths;
    for (const QUrl &url : data.urls()) {
        if (url.isLocalFile() && readable(url.toLocalFile()))
            paths << url.toLocalFile();
    }
    return paths;
}
}

bool ContentView::showsPanel(const QString &key)
{
    return QSettings().value(key, true).toBool();
}

void ContentView::setShowsPanel(const QString &key, bool shown)
{
    QSettings().setValue(key, shown);
}

double ContentView::panelWidth()
{
    const double stored = QSettings().value(QStringLiteral("panelWidth"), defaultPanelWidth).toDouble();
    // A stored width out of range is the default, logged.
    if (stored >= minimumPanelWidth && stored <= maximumPanelWidth)
        return stored;
    qCWarning(lcApp) << "panelWidth" << stored << "is out of range, using" << defaultPanelWidth;
    return defaultPanelWidth;
}

void ContentView::setPanelWidth(double width)
{
    QSettings().setValue(QStringLiteral("panelWidth"), width);
}

bool ContentView::eventFilter(QObject *watched, QEvent *event)
{
    const bool key = event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease;
    if (watched == m_canvas && key && !m_forwarding)
        return canvasKey(static_cast<QKeyEvent *>(event));
    return QWidget::eventFilter(watched, event);
}

// True when handled here, so the canvas never sees it.
bool ContentView::canvasKey(QKeyEvent *event)
{
    // A proposal takes Enter and Esc; tool keys would commit it, so they rest.
    if (hasProposal()) {
        if (event->type() == QEvent::KeyPress && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            m_agent->keepProposal();
            return true;
        }
        if (event->type() == QEvent::KeyPress && event->key() == Qt::Key_Escape) {
            m_agent->discardProposal();
            return true;
        }
        return false;
    }
    const bool typing = m_canvas->isEditingText();
    const std::unique_ptr<QKeyEvent> typed = typing ? ShortcutSettings::shared().textEvent(*event) : ShortcutSettings::shared().canvasEvent(*event);
    if (!typed)
        return true;
    if (!typing && typed->type() == QEvent::KeyPress) {
        const ShortcutChord chord(*typed);
        if (const std::optional<Tool> tool = ShortcutDefinition::tool(chord)) {
            m_session.selectTool(*tool);
            return true;
        }
        if (chord == ShortcutChord(QStringLiteral("x")) || chord == ShortcutChord(QStringLiteral("d"))) {
            if (!typed->isAutoRepeat())
                chord.key == QLatin1String("x") ? m_session.swapFillAndStroke() : m_session.resetDefaultColors();
            return true;
        }
    }
    if (typed->key() == event->key() && typed->modifiers() == event->modifiers())
        return false;
    // A remapped key reaches the canvas as its original.
    m_forwarding = true;
    QCoreApplication::sendEvent(m_canvas, typed.get());
    m_forwarding = false;
    return true;
}

bool ContentView::acceptsDrop(const QMimeData &data) const
{
    return m_workspace && !m_workspace->isManaging() && !hasProposal() && !droppedFiles(data).isEmpty();
}

bool ContentView::hasProposal() const
{
    return m_agent && m_agent->hasProposalIn(m_session);
}

void ContentView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    m_dropRing->raise();
    m_dropRing->show();
}

void ContentView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!acceptsDrop(*event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

void ContentView::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropRing->hide();
}

// Documents open in tabs; pictures and SVGs join this artboard.
void ContentView::dropEvent(QDropEvent *event)
{
    m_dropRing->hide();
    if (!acceptsDrop(*event->mimeData()))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    // Read first: an open may replace this editor.
    const QString native = QLatin1String(ProjectStore::extension);
    const bool drawn = m_session.hasDocument();
    const QPointer<ProjectWorkspace> workspace = m_workspace;
    for (const QString &path : droppedFiles(*event->mimeData())) {
        if (!workspace)
            return;
        if (!drawn || QFileInfo(path).suffix().compare(native, Qt::CaseInsensitive) == 0)
            workspace->openFile(path);
        else
            workspace->placeFile(path);
    }
}
