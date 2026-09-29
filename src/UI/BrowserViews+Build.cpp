#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Registry.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include <cmath>

// Build It from a Browser View (docs/LIVE-IN-FRAME.md, section 5): the frame's design children, the page under them
// and the page's own words go to the agent through the window's one hand-off, and the result waits as a review.

namespace {
BrowserViews::NoteChooser &noteChooser()
{
    static BrowserViews::NoteChooser responder;
    return responder;
}

QString listed(const QList<int> &widths)
{
    QStringList words;
    for (const int width : widths)
        words.append(QString::number(width));
    if (words.size() < 2)
        return words.join(QString());
    const QString last = words.takeLast();
    return QStringLiteral("%1 and %2").arg(words.join(QStringLiteral(", ")), last);
}

// Where each shape sits on the page: one entry per shape that is a frame child or was lifted from an element.
QJsonArray selectorsOf(const VectorDocument &art, const QUuid &layer, QPointF scroll)
{
    QJsonArray list;
    for (const VectorObject &object : art.objects) {
        if (object.kind == ObjectKind::layer)
            continue;
        const bool lifted = !object.liftedFrom.isEmpty() && object.liftedFrom != QLatin1String("trace");
        const bool top = object.parentID && *object.parentID == layer;
        if (!lifted && !top)
            continue;
        const VectorObject *parent = object.parentID ? art.find(*object.parentID) : nullptr;
        if (lifted && parent && parent->liftedFrom == object.liftedFrom)
            continue;
        const QRectF box = art.bounds(object.id).translated(scroll);
        list.append(QJsonObject{{"name", object.name},
                                {"selector", lifted ? object.liftedFrom : QString()},
                                {"bounds", QJsonArray{std::round(box.x()), std::round(box.y()), std::round(box.width()), std::round(box.height())}}});
        if (list.size() >= 400)
            break;
    }
    return list;
}
}

void BrowserViews::setNoteChooser(NoteChooser chooser)
{
    ::noteChooser() = std::move(chooser);
}

bool BrowserViews::hasDesign(const QUuid &frame) const
{
    return m_session.hasDocument() && !m_session.document()->children(frame).empty();
}

void BrowserViews::fillBuild(const QUuid &frame, Bar &bar) const
{
    if (!m_agent || !hasDesign(frame))
        return;
    const QUuid building = m_agent->buildingFrame();
    if (building == frame) {
        bar.build = QStringLiteral("Building with %1…").arg(m_agent->buildingAgent());
        bar.buildBusy = true;
        bar.buildTip = QStringLiteral("The agent is changing the code to match your design. Click to stop it.");
        return;
    }
    if (!building.isNull())
        return;
    if (m_agent->builtAt(frame) > 0) {
        bar.build = QStringLiteral("Built. Review changes");
        bar.buildDone = true;
        bar.buildTip = QStringLiteral("The agent's change is ready to review. Click to see it.");
        return;
    }
    bar.build = QStringLiteral("Build It");
    bar.buildTip = QStringLiteral("Have your agent change the code to match the design on this page");
}

void BrowserViews::runBuildAction(const QUuid &frame, Action action)
{
    if (!m_agent) {
        emit notice(QStringLiteral("Open the project's window to build."));
        return;
    }
    // A held breakpoint preview has the frame resized in the document: Build It is for the design, not the preview.
    if (m_session.isPreviewOnly())
        m_session.cancelInteraction();
    if (action == Action::buildButton) {
        if (m_agent->buildingFrame() == frame)
            action = Action::stopBuild;
        else if (m_agent->builtAt(frame) > 0) {
            const QString project = projectOf(frame);
            m_agent->clearBuilt(frame);
            if (!project.isEmpty())
                m_agent->useProject(project);
            m_agent->showLivePanel(true);
            return;
        } else
            action = Action::buildIt;
    }
    if (action == Action::stopBuild) {
        if (m_agent->buildingFrame() == frame)
            m_agent->stopWaiting();
        return;
    }
    if (!hasDesign(frame)) {
        emit notice(QStringLiteral("Draw or place something on the page first, then Build It."));
        return;
    }
    if (!m_session.isSelected(frame) && !(m_canvas && m_canvas->editPageFrame() == frame))
        m_session.select({frame});
    QString note;
    if (action == Action::buildItWithNote) {
        if (noteChooser()) {
            note = noteChooser()();
        } else {
            bool ok = false;
            note = QInputDialog::getMultiLineText(m_canvas ? m_canvas->window() : nullptr, QStringLiteral("Build It"),
                                                  QStringLiteral("Anything the agent should know:"), QString(), &ok)
                       .trimmed();
            if (!ok)
                return;
        }
        if (note.isEmpty())
            return;
    }
    const QString project = projectOf(frame);
    if (!project.isEmpty()) {
        const QString failure = startBuild(frame, project, note);
        if (!failure.isEmpty())
            emit notice(failure);
        return;
    }
    // A site that isn't yours has no folder yet, so the Hand to Agent sheet asks for it.
    const VectorObject *object = m_session.document()->find(frame);
    const QUrl page = object && object->browser ? object->browser->url : QUrl();
    AgentSheets::handoffFrom(m_canvas ? m_canvas->window() : nullptr,
                             QStringLiteral("the design on %1").arg(page.host().isEmpty() ? QStringLiteral("this page") : page.host()), QString(),
                             [this, frame, note](const QString &folder, const QString &notes) {
                                 return startBuild(frame, folder, notes.isEmpty() ? note : notes);
                             });
}

QString BrowserViews::startBuild(const QUuid &frame, const QString &folder, const QString &note)
{
    if (!m_agent)
        return QStringLiteral("Open the project's window to build.");
    if (!m_session.hasDocument())
        return {};
    const VectorDocument &document = *m_session.document();
    const VectorObject *object = document.find(frame);
    if (!object || !object->browser || document.children(frame).empty())
        return QStringLiteral("Draw or place something on the page first, then Build It.");
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return QStringLiteral("Choose the folder with the app's source.");

    // Only the frame's own children, as the page's CSS px: the frame's corner is the origin.
    const QRectF box = document.bounds(frame);
    VectorDocument art = VectorDocument::blank(box.size());
    const QUuid layer = art.layers().front();
    for (const QUuid &child : document.children(frame)) {
        if (!document.isEffectivelyVisible(child))
            continue;
        std::vector<VectorObject> copies = document.copySubtree(child);
        if (copies.empty())
            continue;
        copies.front().parentID = layer;
        const QUuid copyId = copies.front().id;
        for (VectorObject &copy : copies)
            art.objects.push_back(std::move(copy));
        art.transform(copyId, QTransform::fromTranslate(-box.x(), -box.y()), false, false);
    }
    if (art.children(layer).empty())
        return QStringLiteral("Everything on this page is hidden. Show something first.");

    AgentBridge::HandOff handOff;
    handOff.folder = folder;
    handOff.instruction = note;
    handOff.source = object->name.isEmpty() ? QStringLiteral("a Browser View") : object->name;
    handOff.frame = frame;
    handOff.title = QStringLiteral("Build it: %1").arg(handOff.source);
    const QUrl documentUrl = object->browser->url;
    const QUrl tabUrl = toTabUrl(frame, documentUrl);
    handOff.url = tabUrl.toString();
    if (tabUrl != documentUrl)
        handOff.production = documentUrl.toString();
    const int width = int(std::lround(box.width()));
    const QList<int> widths = breakpoints(frame);
    handOff.breakpoints = QStringLiteral("Designed at %1 px wide%2").arg(width).arg(widths.isEmpty() ? QStringLiteral(".") : QStringLiteral("; the site's breakpoints are %1.").arg(listed(widths)));
    handOff.selectors = selectorsOf(art, layer, object->browser->scroll);
    handOff.pending = LiveFrames::pendingEdits(folder);
    handOff.art = std::move(art);

    // The frame's last picture is the page under the design; the tab can't be captured while it is a frame's.
    const auto found = m_entries.constFind(frame);
    handOff.backdrop = found != m_entries.constEnd() && !found->image.isNull() ? found->image : object->browser->picture;
    const QString failure = m_agent->handOff(handOff);
    if (!failure.isEmpty())
        return failure;
    // Everything pending went in the package just now, so all of it is done.
    LiveFrames::clearPending(folder);
    scheduleRepaint(frame);
    emit frameChanged(frame);
    return {};
}

void BrowserViews::addBuildActions(const QUuid &frame, QMenu *menu)
{
    const bool building = m_agent && !m_agent->buildingFrame().isNull();
    const bool ready = m_agent && hasDesign(frame) && !m_agent->waiting();
    const bool mine = !projectOf(frame).isEmpty();
    const auto item = [this, menu, frame](const QString &title, Action action, bool enabled) {
        QAction *added = menu->addAction(title);
        added->setEnabled(enabled);
        connect(added, &QAction::triggered, menu, [this, frame, action] { runBuildAction(frame, action); });
    };
    item(mine ? QStringLiteral("Build It") : QStringLiteral("Build It…"), Action::buildIt, ready);
    item(QStringLiteral("Build It with a Note…"), Action::buildItWithNote, ready);
    item(QStringLiteral("Stop Build"), Action::stopBuild, building && m_agent->buildingFrame() == frame);
}
