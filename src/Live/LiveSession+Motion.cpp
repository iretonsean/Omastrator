#include "Live/LiveSession.h"
#include <QJsonDocument>

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
    m_motionHeld = false;
    if (!m_page)
        return {};
    motionLetGo(15'000);
    m_motion = {};
    emit motionChanged();
    return {};
}

// The page plays on, and every forced state ends. Best effort: the page may be gone.
void LiveSession::motionLetGo(int timeoutMs)
{
    if (!m_page)
        return;
    if (!m_forced.isEmpty()) {
        // The overlay learns first, so it drops the transitions back that letting go starts.
        evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.setForced([])"));
        const QStringList selectors = m_forced.keys();
        m_forced.clear();
        for (const QString &selector : selectors)
            forceState(selector, {}, timeoutMs);
    }
    if (m_agentsOn) {
        call(cdp(), QStringLiteral("CSS.disable"), {}, m_page->sessionId, nullptr, timeoutMs);
        call(cdp(), QStringLiteral("DOM.disable"), {}, m_page->sessionId, nullptr, timeoutMs);
        m_agentsOn = false;
        m_forcedNodes.clear();
    }
    evaluate(QStringLiteral("window.__oma && window.__oma.motion && window.__oma.motion.release()"));
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
