#include "Agent/Capture.h"
#include "Anywhere/AnywhereSettings.h"
#include "Anywhere/LiftDiff.h"
#include "UI/AgentBridge.h"
#include "UI/AgentSheets.h"
#include "UI/DesignController.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

// Change the real thing, widened (docs/ANYWHERE.md): Hand to Agent from any
// surface, and Apply to Source for lifted art on the user's own page.

namespace {
QString quoted(const QString &text)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

QString capturePath(const QString &name)
{
    const QString folder = Capture::capturesDirectory();
    QDir().mkpath(folder);
    return QDir(folder).filePath(QStringLiteral("%1-%2.png").arg(name, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
}
}

QString DesignController::handOffPage(const QJsonObject &params, QJsonObject &result)
{
    LiveSession &live = m_bridge.liveSession();
    if (live.state() != LiveSession::State::running)
        return QStringLiteral("Open the page in Omastrator's browser first.");
    return handOff(Target{std::nullopt, Surface::keyFor(Surface::Kind::web, QString(), live.url())}, params, result);
}

QString DesignController::handOff(const Target &target, const QJsonObject &params, QJsonObject &result)
{
    const QString key = surfaceKeyOf(target);
    const Surface surface = surfaceOf(target);
    AgentBridge::HandOff handOff;
    handOff.instruction = params["prompt"].toString().trimmed();
    handOff.source = surface.label();
    if (surface.kind == Surface::Kind::web)
        handOff.url = surface.url.toString();
    // The surface's art, all of it: what was drawn and what was lifted, which says what the drawing is over.
    if (!m_overlays.art(key).empty()) {
        VectorDocument art = m_overlays.extract(key);
        if (!art.layers().empty() && !art.children(art.layers().front()).empty()) {
            const QRectF bounds = art.bounds(art.children(art.layers().front()), true);
            art.size = QSizeF(std::max(1.0, bounds.right()), std::max(1.0, bounds.bottom()));
            art.background = Qt::transparent;
            handOff.art = art;
        }
    }
    // The surface as it is now; a page that isn't yours also as the site made it, and its edits.
    LiveSession &live = m_bridge.liveSession();
    const bool page = surface.kind == Surface::Kind::web && live.state() == LiveSession::State::running
                      && Surface::keyFor(Surface::Kind::web, QString(), live.url()) == key;
    if (page) {
        const QString now = capturePath(QStringLiteral("handoff"));
        if (live.screenshot(now).isEmpty())
            handOff.screenshot = now;
        if (live.isMockup()) {
            handOff.edits = live.editsShown();
            handOff.origin = live.origin();
            if (!handOff.edits.empty() && live.showOriginal(true).isEmpty()) {
                const QString original = capturePath(QStringLiteral("handoff-original"));
                if (live.screenshot(original).isEmpty())
                    handOff.original = original;
                live.showOriginal(false);
            }
        }
    } else if (!surface.rect.isEmpty()) {
        handOff.screenshot = keepScreenshot(surface.rect);
    }
    auto start = [this, key](AgentBridge::HandOff ready, const QString &folder, const QString &notes, QJsonObject &answer) {
        ready.folder = folder;
        if (!notes.isEmpty())
            ready.instruction = notes;
        QString requestId;
        if (const QString failure = m_bridge.handOff(ready, &requestId); !failure.isEmpty())
            return failure;
        AnywhereSettings::setHandoffFolder(key, QFileInfo(folder).canonicalFilePath());
        answer["requestId"] = requestId;
        say(QStringLiteral("Handed to the agent. Review changes shows what it did when it's done."));
        return QString();
    };
    const QString folder = params["folder"].toString();
    if (!folder.isEmpty())
        return start(handOff, folder, QString(), result);
    // The sheet asks which folder, offering the one used last for this surface.
    result["sheet"] = true;
    m_bridge.bringForward();
    const QString offered = AnywhereSettings::handoffFolder(key);
    QMetaObject::invokeMethod(
        this,
        [this, handOff, offered, start, what = surface.label()] {
            AgentSheets::handoffFrom(&m_window, QStringLiteral("what you made on %1").arg(what), offered,
                                     [handOff, start](const QString &chosen, const QString &notes) {
                                         QJsonObject ignored;
                                         return start(handOff, chosen, notes, ignored);
                                     });
        },
        Qt::QueuedConnection);
    return {};
}

QString DesignController::applyLifted(const QString &key, const std::vector<QUuid> &roots, bool *applied, QJsonObject &result)
{
    Q_UNUSED(key)
    *applied = false;
    EditorSession &overlay = m_overlays.session();
    if (!overlay.hasDocument() || roots.empty())
        return {};
    QStringList notes;
    const std::vector<LiftDiff::Change> changes = LiftDiff::changes(*overlay.document(), roots, LiftDiff::readBaselines(liftedPath()), &notes);
    if (changes.empty())
        return {};
    *applied = true;
    LiveSession &live = m_bridge.liveSession();
    QStringList done, failed;
    for (const LiftDiff::Change &change : changes) {
        QString value = change.value;
        if (change.relative) {
            // Sizes and spacing move by as much as the vectors did, from the element's own value.
            const QJsonObject element = live.evaluate(QStringLiteral("window.__oma.info(%1)").arg(quoted(change.selector))).toObject();
            const auto current = TokenSet::pixels(element["styles"].toObject()[change.property].toString());
            if (!current) {
                failed << QStringLiteral("%1 (its %2 couldn't be read)").arg(change.description, change.property);
                continue;
            }
            value = QStringLiteral("%1px").arg(std::max(0.0, std::round((*current + change.delta) * 100) / 100));
        }
        if (const QString failure = live.edit(change.selector, change.property, value); !failure.isEmpty())
            failed << QStringLiteral("%1 (%2)").arg(change.description, failure);
        else
            done << change.description;
    }
    if (done.isEmpty())
        return QStringLiteral("None of the changes could be made on the page: %1").arg(failed.join(QStringLiteral("; ")));
    // The vectors match the page now: a later Apply to Source starts from here.
    QJsonObject baselines = LiftDiff::readBaselines(liftedPath());
    for (const QUuid &root : roots) {
        const QJsonObject shot = LiftDiff::snapshot(*overlay.document(), root);
        for (auto it = shot.begin(); it != shot.end(); ++it)
            baselines[it.key()] = it.value();
    }
    LiftDiff::writeBaselines(liftedPath(), baselines);
    QString requestId;
    if (const QString failure = m_bridge.liveWriteBack(&requestId); !failure.isEmpty())
        return failure;
    result["applied"] = QJsonArray::fromStringList(done);
    if (!failed.isEmpty() || !notes.isEmpty())
        result["notes"] = QJsonArray::fromStringList(failed + notes);
    if (!requestId.isEmpty())
        result["requestId"] = requestId;
    QString line = requestId.isEmpty() ? QStringLiteral("Applied %1 changes to the page and its code. Review changes shows the diff.").arg(done.size())
                                       : QStringLiteral("Applied %1 changes to the page; the agent is writing the ones that weren't certain.").arg(done.size());
    if (!failed.isEmpty() || !notes.isEmpty())
        line += QLatin1Char(' ') + (failed + notes).join(QLatin1Char(' '));
    say(line);
    return {};
}
