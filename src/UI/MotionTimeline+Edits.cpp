#include "UI/MotionTimeline.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/AgentBridge.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionTrackView.h"
#include <QTimer>
#include <QUrl>
#include <algorithm>

// What the timeline does to the page and the code (docs/MOTION.md, sections 3 to 5): token, keyframe and timing edits, reduced motion,
// Starts, Animate's preview, and groups. Each is a command on the frame's Live session, which answers on the pool's thread.
namespace {
// scheme://host:port of a page's address.
QString originOf(const QString &address)
{
    const QUrl url(address);
    return url.isValid() && !url.host().isEmpty() ? QStringLiteral("%1://%2:%3").arg(url.scheme(), url.host()).arg(url.port(-1)) : QString();
}
}

MotionCode::Bindings MotionTimeline::bindings() const
{
    const Motion::Track *track = selectedTrack();
    if (!track)
        return {};
    for (const MotionCode::Block &block : codeBlocks()) {
        const MotionCode::Bindings found = MotionCode::bindings(block, track->name);
        if (!found.duration.isEmpty() || !found.easing.isEmpty() || !found.stagger.isEmpty())
            return found;
    }
    return {};
}

std::optional<MotionCode::Block> MotionTimeline::blockOfSelection() const
{
    const Motion::Track *track = selectedTrack();
    if (!track)
        return std::nullopt;
    for (const MotionCode::Block &block : codeBlocks()) {
        if (!MotionCode::bindings(block, track->name).duration.isEmpty() || block.text.contains(track->name))
            return block;
    }
    return std::nullopt;
}

void MotionTimeline::setToken(const QString &property, const QString &value, bool preview)
{
    if (!m_live || m_frame.isNull() || !property.startsWith(QLatin1String("--")))
        return;
    const QUuid frame = m_frame;
    m_live->run(frame, [property, value, preview](LiveSession &session) {
        return preview ? session.motionPreviewProperty(QStringLiteral(":root"), property, value)
                       : session.motionSetProperty(QStringLiteral(":root"), property, value);
    }, [this, frame](const QString &error) {
        if (frame == m_frame && !error.isEmpty())
            emit notice(error);
    });
}

void MotionTimeline::setKeyframe(const QString &frame, const QString &property, const QString &value)
{
    const Motion::Track *track = selectedTrack();
    if (!m_live || m_frame.isNull() || !track || track->kind != QLatin1String("css-animation"))
        return;
    const QUuid key = m_frame;
    const QString name = track->name;
    m_live->run(key, [name, frame, property, value](LiveSession &session) { return session.motionSetKeyframe(name, frame, property, value); },
                [this, key](const QString &error) {
                    if (key == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}

void MotionTimeline::setTiming(const QString &property, const QString &value)
{
    const Motion::Track *track = selectedTrack();
    if (!m_live || m_frame.isNull() || !track)
        return;
    const QUuid key = m_frame;
    const QString name = track->kind == QLatin1String("css-animation") ? track->name : QString();
    const QStringList selectors = track->selectors;
    if (name.isEmpty())
        return;
    m_live->run(key, [name, selectors, property, value](LiveSession &session) { return session.motionSetTiming(name, selectors, property, value); },
                [this, key](const QString &error) {
                    if (key == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}

bool MotionTimeline::reducedMotionOn() const
{
    const std::optional<MotionCode::Block> block = blockOfSelection();
    if (!block || !m_live || m_frame.isNull())
        return false;
    bool on = block->reducedMotion;
    for (const LiveEdit &edit : m_live->snapshot(m_frame).edits)
        if (edit.selector == QLatin1String("motion:") + block->name && edit.property == QLatin1String("reduced-motion"))
            on = !edit.after.isEmpty();
    return on;
}

void MotionTimeline::setReducedMotion(bool on)
{
    const std::optional<MotionCode::Block> block = blockOfSelection();
    if (!m_live || m_frame.isNull() || !block || on == reducedMotionOn())
        return;
    // Taking it out remembers its text; putting it back uses that text, or the rule the block's animations need.
    QString removed;
    QString added;
    if (!on) {
        removed = MotionCode::reducedRule(*block);
        if (removed.isEmpty()) {
            // The file has none: what is pending is the one that put it back, so this cancels it.
            for (const LiveEdit &edit : m_live->snapshot(m_frame).edits)
                if (edit.selector == QLatin1String("motion:") + block->name && edit.property == QLatin1String("reduced-motion"))
                    removed = edit.after;
        }
        if (removed.isEmpty())
            return;
    } else {
        for (const LiveEdit &edit : m_live->snapshot(m_frame).edits)
            if (edit.selector == QLatin1String("motion:") + block->name && edit.property == QLatin1String("reduced-motion") && !edit.before.isEmpty())
                added = edit.before;
        if (added.isEmpty())
            added = MotionCode::defaultReducedRule(*block);
        if (added.isEmpty())
            return;
    }
    const QUuid key = m_frame;
    const QString name = block->name;
    m_live->run(key, [name, removed, added](LiveSession &session) { return session.motionSetReducedMotion(name, removed, added); });
}

QString MotionTimeline::pendingValue(const QString &property, const QString &fallback) const
{
    if (!m_live || m_frame.isNull())
        return fallback;
    QString value = fallback;
    for (const LiveEdit &edit : m_live->snapshot(m_frame).edits)
        if (edit.selector == QLatin1String(":root") && edit.property == property)
            value = edit.after;
    return value;
}

QString MotionTimeline::openAndPlay(const QUuid &frame)
{
    // The page shown now is the one before the preview; the one from another origin is what plays.
    LiveFrames *frames = m_live ? m_live.data() : LiveFrames::of(m_session);
    const QString before = !frame.isNull() && frames->active(frame) ? originOf(frames->snapshot(frame).url.toString()) : QString();
    const QString failure = open(frame);
    if (!failure.isEmpty())
        return failure;
    m_replayOrigin = before;
    m_replayWhen = true;
    return {};
}

bool MotionTimeline::previewing() const
{
    return !m_frame.isNull() && BrowserViews::of(m_session)->previewing(m_frame);
}

void MotionTimeline::saveToCode()
{
    if (m_frame.isNull())
        return;
    const QString failure = BrowserViews::of(m_session)->savePreview(m_frame);
    if (!failure.isEmpty())
        emit notice(failure);
}

void MotionTimeline::discardPreview()
{
    if (!m_frame.isNull())
        BrowserViews::of(m_session)->discardPreview(m_frame);
}

void MotionTimeline::setPreviewReduced(bool on)
{
    if (!m_live || m_frame.isNull() || on == m_previewReduced)
        return;
    m_previewReduced = on;
    const QUuid frame = m_frame;
    m_live->run(frame, [on](LiveSession &session) { return session.motionEmulateReduced(on); }, [this, frame](const QString &error) {
        if (frame == m_frame && !error.isEmpty())
            emit notice(error);
    });
    emit changed();
}

void MotionTimeline::setStarts(const QString &trigger)
{
    const Motion::Track *track = selectedTrack();
    if (!m_live || m_frame.isNull() || !track || track->kind != QLatin1String("css-animation") || trigger == track->trigger)
        return;
    const QUuid frame = m_frame;
    const QString name = track->name;
    const QStringList selectors = track->selectors;
    const QString from = track->trigger;
    m_live->run(frame, [name, selectors, from, trigger](LiveSession &session) { return session.motionSetTrigger(name, selectors, from, trigger); },
                [this, frame](const QString &error) {
                    if (frame == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}

QString MotionTimeline::askAgent(const QString &prompt)
{
    if (!m_live || m_frame.isNull())
        return tr("Open the timeline on a page first.");
    AgentBridge *agent = BrowserViews::of(m_session)->agent();
    const LiveFrames::Snapshot snapshot = m_live->snapshot(m_frame);
    if (!agent)
        return tr("Open the project's window to ask.");
    if (snapshot.project.isEmpty() || snapshot.mockup)
        return tr("This page isn't one of your sites, so there's no code to change.");
    return agent->liveAsk(prompt, snapshot.selection, nullptr, snapshot.project);
}

QString MotionTimeline::previewNotice() const
{
    AgentBridge *agent = BrowserViews::of(m_session)->agent();
    const auto preview = agent && !m_frame.isNull() ? agent->previewOfFrame(m_frame) : std::nullopt;
    return preview ? preview->notice : QString();
}

void MotionTimeline::setExpanded(const QString &id, bool open)
{
    if (open == m_expanded.contains(id))
        return;
    if (open)
        m_expanded.insert(id);
    else
        m_expanded.remove(id);
    fit();
    m_tracks->update();
    emit changed();
}

QString MotionTimeline::setOrder(Motion::Order mode)
{
    const Motion::Track *track = selectedTrack();
    if (!track || !m_live || m_frame.isNull())
        return tr("Pick a group first.");
    QStringList selectors;
    for (const Motion::Bar &bar : track->bars)
        if (!bar.selector.isEmpty() && !selectors.contains(bar.selector))
            selectors << bar.selector;
    if (selectors.size() < 2)
        return tr("That is one element, not a group.");
    // The order they were picked in comes first: it is what "As picked" means, and the numbers on the page say it.
    QStringList ordered;
    for (const QJsonValue &each : m_live->snapshot(m_frame).selection) {
        const QString selector = each.toObject()["selector"].toString();
        if (selectors.contains(selector) && !ordered.contains(selector))
            ordered << selector;
    }
    for (const QString &selector : std::as_const(selectors))
        if (!ordered.contains(selector))
            ordered << selector;
    // A shuffle is fixed by the elements and by how often it was pressed, so a reload gives the same order.
    const quint32 seed = quint32(qHash(selectors.join(QLatin1Char('|')))) + 7919u * quint32(mode == Motion::Order::shuffle ? m_shuffles[track->id]++ : 0);
    const QUuid frame = m_frame;
    m_live->run(frame, [ordered, mode, seed](LiveSession &session) {
        const QJsonObject boxes = session.motionBoxes(ordered);
        QList<Motion::Element> elements;
        for (const QString &selector : ordered) {
            const QJsonObject box = boxes[selector].toObject();
            elements.append({selector, QRectF(box["x"].toDouble(), box["y"].toDouble(), box["width"].toDouble(), box["height"].toDouble())});
        }
        const QList<int> ranks = Motion::order(mode, elements, seed);
        QList<QPair<QString, int>> indices;
        for (int i = 0; i < ordered.size(); ++i)
            indices.append({ordered[i], ranks[i]});
        return session.motionSetIndices(indices);
    }, [this, frame](const QString &error) {
        if (frame == m_frame && !error.isEmpty())
            emit notice(error);
    });
    return {};
}

void MotionTimeline::setExtraDelay(double ms)
{
    const Motion::Track *track = selectedTrack();
    if (!track || m_selectedBar < 0 || m_selectedBar >= track->bars.size() || !m_live || m_frame.isNull())
        return;
    const QString selector = track->bars[m_selectedBar].selector;
    const QString value = QStringLiteral("%1ms").arg(qRound(std::max(0.0, ms)));
    const QUuid frame = m_frame;
    m_live->run(frame, [selector, value](LiveSession &session) { return session.motionSetProperty(selector, QStringLiteral("--delay-extra"), value); },
                [this, frame](const QString &error) {
                    if (frame == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}

void MotionTimeline::useGroupTiming()
{
    const Motion::Track *track = selectedTrack();
    if (!track || m_selectedBar < 0 || m_selectedBar >= track->bars.size() || !m_live || m_frame.isNull())
        return;
    const QString selector = track->bars[m_selectedBar].selector;
    const QUuid frame = m_frame;
    // An empty value takes `--delay-extra` off the element and out of its rule: the group's timing applies again.
    m_live->run(frame, [selector](LiveSession &session) { return session.motionSetProperty(selector, QStringLiteral("--delay-extra"), QString()); },
                [this, frame](const QString &error) {
                    if (frame == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}

QString MotionTimeline::effectDeclarations(const QString &effect)
{
    if (effect == QLatin1String("rise"))
        return QStringLiteral("opacity: 0; translate: 0 44px;");
    if (effect == QLatin1String("grow"))
        return QStringLiteral("opacity: 0; scale: 0.85;");
    if (effect == QLatin1String("flip"))
        return QStringLiteral("opacity: 0; rotate: y 70deg;");
    return {};
}

QString MotionTimeline::effectOf() const
{
    const Motion::Track *track = selectedTrack();
    if (!track || track->keyframes.isEmpty())
        return {};
    const QJsonObject props = track->keyframes.first().toObject()["props"].toObject();
    if (props.contains(QStringLiteral("rotate")) && props["rotate"].toString().startsWith(QLatin1String("y")))
        return QStringLiteral("flip");
    if (props.contains(QStringLiteral("scale")))
        return QStringLiteral("grow");
    if (props.contains(QStringLiteral("translate")))
        return QStringLiteral("rise");
    return QStringLiteral("custom");
}

void MotionTimeline::setEffect(const QString &effect)
{
    const Motion::Track *track = selectedTrack();
    const QString declarations = effectDeclarations(effect);
    if (!track || track->kind != QLatin1String("css-animation") || declarations.isEmpty() || !m_live || m_frame.isNull())
        return;
    const QString name = track->name;
    const QUuid frame = m_frame;
    m_live->run(frame, [name, declarations](LiveSession &session) { return session.motionSetEffect(name, QStringLiteral("from"), declarations); },
                [this, frame](const QString &error) {
                    if (frame == m_frame && !error.isEmpty())
                        emit notice(error);
                });
}
