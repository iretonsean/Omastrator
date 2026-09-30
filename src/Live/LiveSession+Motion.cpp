#include "Live/LiveSession.h"
#include "Live/Tokens.h"
#include <QJsonDocument>
#include <QTimer>

// Motion (docs/MOTION.md): the timeline's calls into the page. The reading, holding and seeking are in motion.js; the
// DevTools Protocol is used only where it alone can act: forcing :hover and :focus.

namespace {
QString literal(const QJsonValue &value)
{
    return QString::fromUtf8(value.isObject() ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
                             : value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                               : QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
}
}

QString LiveSession::motionHold()
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QString hold = QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.hold() : null");
    QJsonObject list = evaluate(hold, &error).toObject();
    // The overlay may have missed this page (it began loading before the session attached): bring it in once.
    if (list.isEmpty() && m_pool && !evaluate(QStringLiteral("!!(window.__oma && window.__oma.motion)")).toBool()) {
        evaluate(frameOverlayScript());
        list = evaluate(hold, &error).toObject();
    }
    if (list.isEmpty())
        return error.isEmpty() ? QStringLiteral("The page isn't ready for the timeline yet.") : error;
    m_motionHeld = true;
    m_motion = list;
    emit motionChanged();
    return {};
}

QString LiveSession::motionRelease()
{
    if (!m_page) {
        m_motionHeld = false;
        return {};
    }
    // Still held while it lets go, or there is nothing to put back.
    motionLetGo(15'000);
    m_motionHeld = false;
    m_motion = {};
    emit motionChanged();
    return {};
}

// The page plays on, and every forced state ends. Best effort: the page may be gone.
void LiveSession::motionLetGo(int timeoutMs)
{
    // Nothing held, forced or emulated: leaving a frame has nothing to put back, and asks the page nothing.
    if (!m_page || (!m_motionHeld && m_forced.isEmpty() && !m_agentsOn && !m_reducedEmulated))
        return;
    // Every wait below runs this thread's events, and the tab may go in one of them: the page is looked at again after each.
    const QString session = m_page->sessionId;
    if (!m_forced.isEmpty()) {
        // The overlay learns first, so it drops the transitions back that letting go starts.
        evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.setForced([])"), nullptr, timeoutMs);
        const QStringList selectors = m_forced.keys();
        m_forced.clear();
        for (const QString &selector : selectors) {
            if (!m_page)
                break;
            forceState(selector, {}, timeoutMs);
        }
    }
    if (m_page && m_agentsOn) {
        call(cdp(), QStringLiteral("CSS.disable"), {}, session, nullptr, timeoutMs);
        call(cdp(), QStringLiteral("DOM.disable"), {}, session, nullptr, timeoutMs);
    }
    m_agentsOn = false;
    m_forcedNodes.clear();
    if (!m_page)
        return;
    // A visitor who asked for less motion was only being played.
    if (m_reducedEmulated) {
        m_reducedEmulated = false;
        call(cdp(), QStringLiteral("Emulation.setEmulatedMedia"), {{"features", QJsonArray()}}, session, nullptr, timeoutMs);
    }
    evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.release()"), nullptr, timeoutMs);
}

QString LiveSession::motionSeek(double ms)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.seek(%1)").arg(ms, 0, 'f', 3), &error);
    return error;
}

QString LiveSession::motionSeekScroll(double px)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.seekScroll(%1)").arg(px, 0, 'f', 2), &error);
    return error;
}

QString LiveSession::motionRefresh()
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QJsonObject list = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.list() : null"), &error).toObject();
    if (list.isEmpty())
        return error;
    m_motion = list;
    emit motionChanged();
    return {};
}

void LiveSession::syncForced()
{
    QJsonArray forced;
    for (auto it = m_forced.constBegin(); it != m_forced.constEnd(); ++it)
        forced.append(QJsonObject{{"selector", it.key()}, {"state", it.value()}});
    evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.setForced(%1)").arg(literal(forced)));
}

QString LiveSession::motionForce(const QString &selector, const QString &state)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    if (state.isEmpty())
        m_forced.remove(selector);
    else
        m_forced.insert(selector, state);
    // Before the state changes: a state let go is watched for the transition back, and one forced is named as its trigger.
    syncForced();
    const QString failure = forceState(selector, state, 15'000);
    if (!failure.isEmpty()) {
        m_forced.remove(selector);
        syncForced();
        return failure;
    }
    return motionRefresh();
}

QString LiveSession::forceState(const QString &selector, const QString &state, int timeoutMs)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QString session = m_page->sessionId;
    if (!m_agentsOn) {
        call(cdp(), QStringLiteral("DOM.enable"), {}, session, &error, timeoutMs);
        if (error.isEmpty())
            call(cdp(), QStringLiteral("CSS.enable"), {}, session, &error, timeoutMs);
        if (!error.isEmpty())
            return QStringLiteral("Couldn't hold the page's state: %1").arg(error);
        // The nodes exist for the protocol only once the document was asked for, and each ask starts them over.
        const QJsonObject document = call(cdp(), QStringLiteral("DOM.getDocument"), {{"depth", 0}}, session, &error, timeoutMs);
        m_domRoot = document["root"].toObject()["nodeId"].toInt();
        if (!error.isEmpty() || m_domRoot == 0)
            return QStringLiteral("Couldn't hold the page's state: %1").arg(error.isEmpty() ? QStringLiteral("no document") : error);
        m_forcedNodes.clear();
        m_agentsOn = true;
    }
    int node = state.isEmpty() ? m_forcedNodes.take(selector) : 0;
    if (node == 0) {
        const QJsonObject found = call(cdp(), QStringLiteral("DOM.querySelector"), {{"nodeId", m_domRoot}, {"selector", selector}}, session, &error, timeoutMs);
        node = found["nodeId"].toInt();
    }
    if (node == 0)
        return state.isEmpty() ? QString() : QStringLiteral("That element is gone from the page.");
    QJsonArray states;
    if (!state.isEmpty())
        states.append(state);
    call(cdp(), QStringLiteral("CSS.forcePseudoState"), {{"nodeId", node}, {"forcedPseudoClasses", states}}, session, &error, timeoutMs);
    if (!state.isEmpty() && error.isEmpty())
        m_forcedNodes.insert(selector, node);
    return error.isEmpty() ? QString() : QStringLiteral("Couldn't hold the page's state: %1").arg(error);
}

QString LiveSession::selectElements(const QStringList &selectors)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    bool first = true;
    for (const QString &selector : selectors) {
        evaluate(QStringLiteral("window.__oma && window.__oma.select(%1, %2)").arg(literal(selector), first ? "false" : "true"), &error);
        first = false;
        if (!error.isEmpty())
            return error;
    }
    return {};
}

namespace {
// The element the edit is about, as write-back and the agent need it.
QJsonObject described(const QString &selector, const QString &classes, const QString &path)
{
    return {{"selector", selector}, {"classes", classes}, {"path", path}};
}
}

QString LiveSession::motionPreviewProperty(const QString &selector, const QString &property, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const bool shown = evaluate(QStringLiteral("!!(window.__oma && window.__oma.motion && window.__oma.motion.previewProperty(%1, %2, %3))")
                                    .arg(literal(selector), literal(property), literal(value)),
                                &error)
                           .toBool();
    if (!shown)
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    return {};
}

QString LiveSession::motionSetProperty(const QString &selector, const QString &property, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QJsonObject applied = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.setProperty(%1, %2, %3) : null")
                                             .arg(literal(selector), literal(property), literal(value)),
                                         &error)
                                    .toObject();
    if (applied.isEmpty())
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    const QString classes = applied["classes"].toString();
    LiveEdit edit;
    edit.selector = selector;
    edit.property = property;
    edit.before = applied["before"].toString();
    edit.after = value;
    edit.classesBefore = classes;
    edit.classesAfter = classes;
    edit.path = EditSets::pathOf(m_url);
    edit.origin = EditSets::originOf(m_url);
    edit.element = described(selector, classes, edit.path);
    UndoStep step;
    step.selector = selector;
    step.property = property;
    step.was = {{"style", applied["styleBefore"].toString()}, {"cls", classes}, {"text", QJsonValue()}};
    step.now = {{"style", applied["styleAfter"].toString()}, {"cls", classes}, {"text", QJsonValue()}};
    step.group = m_group;
    keep(edit, step);
    return motionRefresh();
}

QString LiveSession::motionSetKeyframe(const QString &name, const QString &frame, const QString &property, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QJsonObject applied = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.setKeyframe(%1, %2, %3, %4) : null")
                                             .arg(literal(name), literal(frame), literal(property), literal(value)),
                                         &error)
                                    .toObject();
    if (applied.isEmpty())
        return error.isEmpty() ? QStringLiteral("That keyframe isn't on the page.") : error;
    LiveEdit edit;
    edit.selector = QStringLiteral("@keyframes ") + name;
    edit.property = frame + QLatin1Char(' ') + property;
    edit.before = applied["before"].toString();
    edit.after = value;
    edit.path = EditSets::pathOf(m_url);
    edit.origin = EditSets::originOf(m_url);
    edit.element = described(edit.selector, QString(), edit.path);
    UndoStep step;
    step.selector = edit.selector;
    step.property = edit.property;
    step.was = {{"motion", applied["was"]}};
    step.now = {{"motion", applied["now"]}};
    step.group = m_group;
    keep(edit, step);
    return motionRefresh();
}

QJsonObject LiveSession::motionBoxes(const QStringList &selectors)
{
    if (!m_page)
        return {};
    return evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.boxes(%1) : null").arg(literal(QJsonArray::fromStringList(selectors)))).toObject();
}

QString LiveSession::motionSetIndices(const QList<QPair<QString, int>> &indices)
{
    // One change to the group: one undo step, however many elements took a new index.
    m_group = ++m_lastGroup;
    QString failure;
    for (const auto &[selector, index] : indices) {
        const QString error = motionSetProperty(selector, QStringLiteral("--i"), QString::number(index));
        if (failure.isEmpty())
            failure = error;
    }
    m_group = 0;
    return failure;
}

QString LiveSession::motionSetEffect(const QString &name, const QString &frame, const QString &declarations)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QJsonObject applied = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.setEffect(%1, %2, %3) : null")
                                             .arg(literal(name), literal(frame), literal(declarations)),
                                         &error)
                                    .toObject();
    if (applied.isEmpty())
        return error.isEmpty() ? QStringLiteral("That keyframe isn't on the page.") : error;
    LiveEdit edit;
    edit.selector = QStringLiteral("@keyframes ") + name;
    edit.property = frame + QStringLiteral(" *");
    edit.before = applied["before"].toString();
    edit.after = declarations;
    edit.path = EditSets::pathOf(m_url);
    edit.origin = EditSets::originOf(m_url);
    edit.element = described(edit.selector, QString(), edit.path);
    UndoStep step;
    step.selector = edit.selector;
    step.property = edit.property;
    step.was = {{"motion", applied["was"]}};
    step.now = {{"motion", applied["now"]}};
    step.group = m_group;
    keep(edit, step);
    return motionRefresh();
}

QString LiveSession::motionSetTiming(const QString &name, const QStringList &selectors, const QString &property, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QJsonObject changes;
    if (property.endsWith(QLatin1String("duration")) || property.endsWith(QLatin1String("delay"))) {
        const auto ms = TokenSet::milliseconds(value);
        if (!ms)
            return QStringLiteral("%1 isn't a time.").arg(value);
        changes[property.endsWith(QLatin1String("duration")) ? QStringLiteral("duration") : QStringLiteral("delay")] = *ms;
    } else if (property.endsWith(QLatin1String("timing-function"))) {
        changes["easing"] = value;
    } else {
        return QStringLiteral("%1 isn't something the timeline changes.").arg(property);
    }
    QString error;
    const QJsonObject applied = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.setTiming(%1, %2) : null").arg(literal(name), literal(changes)),
                                         &error)
                                    .toObject();
    if (applied.isEmpty())
        return error.isEmpty() ? QStringLiteral("That motion isn't on the page.") : error;
    // One undo step for the whole row: the first restores every animation of the name, the rest have nothing more to do.
    m_group = ++m_lastGroup;
    bool first = true;
    for (const QString &selector : selectors) {
        LiveEdit edit;
        edit.selector = selector;
        edit.property = property;
        edit.after = value;
        edit.path = EditSets::pathOf(m_url);
        edit.origin = EditSets::originOf(m_url);
        edit.element = described(selector, QString(), edit.path);
        UndoStep step;
        step.selector = selector;
        step.property = property;
        step.was = first ? QJsonObject{{"motion", applied["was"]}} : QJsonObject{{"codeOnly", true}};
        step.now = first ? QJsonObject{{"motion", applied["now"]}} : QJsonObject{{"codeOnly", true}};
        step.group = m_group;
        keep(edit, step);
        first = false;
    }
    m_group = 0;
    return motionRefresh();
}

QString LiveSession::motionEmulateReduced(bool reduced)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    const QJsonArray features = reduced ? QJsonArray{QJsonObject{{"name", "prefers-reduced-motion"}, {"value", "reduce"}}} : QJsonArray();
    call(cdp(), QStringLiteral("Emulation.setEmulatedMedia"), {{"features", features}}, m_page->sessionId, &error);
    if (!error.isEmpty())
        return QStringLiteral("Couldn't preview reduced motion: %1").arg(error);
    m_reducedEmulated = reduced;
    // The page's own rules take the motion away or not, in the next frame; the list is read then.
    QTimer::singleShot(120, this, [this] {
        if (m_page && m_motionHeld)
            motionRefresh();
    });
    return {};
}

void LiveSession::setPreviewOrigin(const QUrl &origin)
{
    m_previewOrigin = origin;
}

QString LiveSession::motionSetTrigger(const QString &name, const QStringList &selectors, const QString &from, const QString &to)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    // Load and scroll can be seen: the overlay moves the running animations to a view timeline, or lets them play from the start.
    QJsonObject applied;
    if (to == QLatin1String("scroll") || to == QLatin1String("load")) {
        applied = evaluate(QStringLiteral("window.__oma && window.__oma.motion ? window.__oma.motion.setStarts(%1, %2) : null").arg(literal(name), literal(to)), &error).toObject();
        if (!error.isEmpty())
            return error;
    }
    m_group = ++m_lastGroup;
    bool first = true;
    for (const QString &selector : selectors) {
        LiveEdit edit;
        edit.selector = selector;
        edit.property = QStringLiteral("motion-trigger");
        edit.before = from;
        edit.after = to;
        edit.path = EditSets::pathOf(m_url);
        edit.origin = EditSets::originOf(m_url);
        edit.element = described(selector, QString(), edit.path);
        edit.element["animation"] = name;
        UndoStep step;
        step.selector = selector;
        step.property = edit.property;
        const bool carries = first && !applied.isEmpty();
        step.was = carries ? QJsonObject{{"motion", applied["was"]}} : QJsonObject{{"codeOnly", true}};
        step.now = carries ? QJsonObject{{"motion", applied["now"]}} : QJsonObject{{"codeOnly", true}};
        step.group = m_group;
        keep(edit, step);
        first = false;
    }
    m_group = 0;
    return motionRefresh();
}

QString LiveSession::motionSetReducedMotion(const QString &block, const QString &removed, const QString &added)
{
    LiveEdit edit;
    edit.selector = QStringLiteral("motion:") + block;
    edit.property = QStringLiteral("reduced-motion");
    edit.before = removed;
    edit.after = added;
    edit.path = EditSets::pathOf(m_url);
    edit.origin = EditSets::originOf(m_url);
    edit.element = described(edit.selector, QString(), edit.path);
    UndoStep step;
    step.selector = edit.selector;
    step.property = edit.property;
    step.was = {{"codeOnly", true}};
    step.now = {{"codeOnly", true}};
    step.group = m_group;
    keep(edit, step);
    return {};
}
