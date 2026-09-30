#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Motion.h"
#include "UI/AgentBridge.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionTimeline.h"
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>

// Animate from a Browser View (docs/MOTION.md, section 4): what the agent is told comes from the frame's Live, and what it wrote is
// shown in the frame from its worktree, on a server of its own, with the document keeping its own address.
namespace {
BrowserViews::PreviewChooser &previewChooser()
{
    static BrowserViews::PreviewChooser chooser;
    return chooser;
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

// "a, b and c".
QString named(const QStringList &files)
{
    if (files.size() < 2)
        return files.join(QString());
    QStringList head = files;
    const QString last = head.takeLast();
    return QStringLiteral("%1 and %2").arg(head.join(QStringLiteral(", ")), last);
}

// The element as the designer would call it: h1#headline, or the selector.
QString labelOf(const QJsonObject &element)
{
    const QString tag = element["tag"].toString();
    const QString id = element["id"].toString();
    if (!tag.isEmpty() && !id.isEmpty())
        return tag + QLatin1Char('#') + id;
    return element["selector"].toString();
}
}

AgentBridge *BrowserViews::agent() const
{
    return m_agent;
}

void BrowserViews::setPreviewChooser(PreviewChooser chooser)
{
    ::previewChooser() = std::move(chooser);
}

QString BrowserViews::animate(const QUuid &frame, const QString &instruction, bool reducedMotion)
{
    if (!m_agent)
        return QStringLiteral("Open the project's window to animate.");
    LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (!live || !live->active(frame) || live->snapshot(frame).selection.isEmpty())
        return QStringLiteral("Pick the element to animate first: double-click the page, then click it.");
    const QString project = projectOf(frame);
    if (project.isEmpty())
        return QStringLiteral("This page isn't one of your sites, so there's no code to write motion into.");
    // One preview per project: a second ask says what to do with the one that is open.
    if (m_agent->previewOf(project)) {
        PreviewAnswer answer = PreviewAnswer::cancel;
        if (previewChooser()) {
            answer = previewChooser()();
        } else {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Animate"), QStringLiteral("Keep or discard the motion you're previewing first?"),
                            QMessageBox::NoButton, m_canvas ? m_canvas->window() : nullptr);
            QPushButton *keep = box.addButton(QStringLiteral("Keep"), QMessageBox::AcceptRole);
            QPushButton *discard = box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(keep);
            box.exec();
            answer = box.clickedButton() == static_cast<QAbstractButton *>(discard) ? PreviewAnswer::discard
                : box.clickedButton() == static_cast<QAbstractButton *>(keep)   ? PreviewAnswer::keep
                                                                                : PreviewAnswer::cancel;
        }
        if (answer != PreviewAnswer::discard)
            return {};
        m_agent->discardPreview(project);
    }
    if (const QString busy = m_agent->busyMessage(); !busy.isEmpty())
        return busy;
    // The page's motion is read fresh, so the agent hears what the elements do now; the ask goes when it has been.
    live->run(frame, [](LiveSession &session) { return session.motionRefresh(); }, [this, frame, project, instruction, reducedMotion](const QString &) {
        LiveFrames *again = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
        const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
        if (!again || !again->active(frame) || !object || !object->browser || !m_agent)
            return;
        const LiveFrames::Snapshot snapshot = again->snapshot(frame);
        AgentBridge::AnimateRequest request;
        request.frame = frame;
        request.folder = project;
        request.instruction = instruction;
        request.elements = snapshot.selection;
        request.tokens = snapshot.tokens;
        request.reducedMotion = reducedMotion;
        QSet<QString> picked;
        for (const QJsonValue &each : snapshot.selection)
            picked.insert(each.toObject()["selector"].toString());
        // The motion on the picked elements, and every @keyframes name in use so a new one doesn't clash.
        const Motion::Timeline timeline = Motion::parse(snapshot.motion);
        for (const Motion::Track &track : timeline.tracks) {
            if (track.kind == QLatin1String("css-animation") && !track.name.isEmpty() && !request.keyframeNames.contains(track.name))
                request.keyframeNames << track.name;
            const bool mine = std::any_of(track.selectors.cbegin(), track.selectors.cend(), [&](const QString &selector) { return picked.contains(selector); });
            if (mine)
                request.motion.append(QJsonObject{{"selector", track.selectors.value(0)}, {"name", track.name}, {"kind", track.kind}, {"trigger", track.trigger},
                                                  {"duration", track.duration}, {"delay", track.delay}, {"easing", track.easing},
                                                  {"properties", QJsonArray::fromStringList(track.properties)}});
        }
        const auto found = m_entries.constFind(frame);
        request.picture = found != m_entries.constEnd() && !found->image.isNull() ? found->image : object->browser->picture;
        const QRectF box = m_session.document()->bounds(frame);
        const QList<int> widths = breakpoints(frame);
        request.width = QStringLiteral("The page is drawn %1 px wide%2").arg(qRound(box.width())).arg(
            widths.isEmpty() ? QStringLiteral(".") : QStringLiteral("; the site's breakpoints are %1.").arg(listed(widths)));
        request.url = toTabUrl(frame, object->browser->url).toString();
        QString host = object->browser->url.host();
        if (host.startsWith(QLatin1String("www.")))
            host = host.mid(4);
        request.siteName = host.section(QLatin1Char('.'), 0, 0).isEmpty() ? QDir(project).dirName() : host.section(QLatin1Char('.'), 0, 0);
        request.title = QStringLiteral("Animate: %1").arg(snapshot.selection.size() > 1 ? QStringLiteral("%1 elements").arg(snapshot.selection.size())
                                                                                        : labelOf(snapshot.selection.first().toObject()));
        const QString failure = m_agent->liveAnimate(request);
        if (!failure.isEmpty())
            emit notice(failure);
    });
    return {};
}

void BrowserViews::onPreviewChanged(const QString &folder)
{
    if (!m_agent)
        return;
    const auto preview = m_agent->previewOf(folder);
    if (preview && m_entries.contains(preview->frame) && !m_previewed.contains(preview->frame)) {
        const QUuid frame = preview->frame;
        if (!preview->failure.isEmpty()) {
            emit notice(QStringLiteral("Couldn't start the preview: %1").arg(preview->failure));
            m_agent->discardPreview(folder);
            return;
        }
        if (!preview->ready)
            return;
        m_previewed.insert(frame, folder);
        // The session first: its project is still this frame's project on the preview's address.
        LiveFrames::of(m_session)->setPreview(frame, preview->url);
        useDevServer(frame, preview->url);
        emit notice(QStringLiteral("%1 wrote the motion into %2. Preview only until you save.")
                        .arg(preview->agent.isEmpty() ? QStringLiteral("Your agent") : preview->agent, named(preview->files)));
        if (MotionTimeline *timeline = MotionTimeline::of(m_session)) {
            const QString failure = timeline->openAndPlay(frame);
            if (!failure.isEmpty())
                emit notice(failure);
        }
        emit previewStateChanged(frame);
        scheduleRepaint(frame);
        return;
    }
    // Accepted, discarded or failed: the frames that showed it go back to the project.
    if (!preview) {
        const QList<QUuid> frames = m_previewed.keys(folder);
        for (const QUuid &frame : frames)
            endPreview(frame);
    }
}

void BrowserViews::endPreview(const QUuid &frame)
{
    if (!m_previewed.remove(frame))
        return;
    LiveFrames *live = m_session.findChild<LiveFrames *>(QString(), Qt::FindDirectChildrenOnly);
    if (live && live->active(frame)) {
        live->setPreview(frame, {});
        // Back on the project's dev server where it has one, else on the address the document keeps.
        useDevServer(frame, live->snapshot(frame).serverUrl);
    } else {
        useDevServer(frame, {});
    }
    emit previewStateChanged(frame);
    scheduleRepaint(frame);
}

QString BrowserViews::savePreview(const QUuid &frame)
{
    const QString folder = m_previewed.value(frame);
    if (folder.isEmpty() || !m_agent)
        return QStringLiteral("There's no motion being previewed here.");
    return m_agent->acceptPreview(folder);
}

void BrowserViews::discardPreview(const QUuid &frame)
{
    const QString folder = m_previewed.value(frame);
    if (!folder.isEmpty() && m_agent)
        m_agent->discardPreview(folder);
}
