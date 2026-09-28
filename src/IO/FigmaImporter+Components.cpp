#include "Document/Components.h"
#include "IO/FigmaMapperInternal.h"
#include <algorithm>

// Components and instances: a SYMBOL is a component master, whether it sits on
// its own (a set of one) or as a variant inside a FRAME with isStateGroup (a
// component set, named "Prop=Value, Prop2=Value2" per variant). An INSTANCE's
// children are never mapped from its own nodeChanges: Components::sync rebuilds
// them from the master and these overrides once the whole tree is in (docs/import/figma.md).
namespace FigmaMap {
namespace {
std::map<QString, QString> parseVariant(const QString &name)
{
    std::map<QString, QString> result;
    for (const QString &pair : name.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        result.emplace(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
    }
    return result;
}
}

void mapComponent(Context &ctx, const QVariantMap &node, const Guid &guid, VectorObject &object, const QTransform &transform)
{
    ComponentInfo info;
    const auto self = ctx.tree.nodes.find(guid);
    const Guid parentGuid = self == ctx.tree.nodes.end() ? Guid() : self->second.parent;
    const auto parentNode = parentGuid.isEmpty() ? ctx.tree.nodes.end() : ctx.tree.nodes.find(parentGuid);
    const bool inSet = parentNode != ctx.tree.nodes.end()
        && ((str(parentNode->second.fields, "type") == QLatin1String("FRAME") && boolean(parentNode->second.fields, "isStateGroup"))
            || str(parentNode->second.fields, "type") == QLatin1String("COMPONENT_SET"));
    if (inSet) {
        info.set = str(parentNode->second.fields, "name");
        info.variant = parseVariant(str(node, "name"));
    } else {
        info.set = str(node, "name");
    }
    info.placement = transform;
    object.component = info;
}

void deferInstance(Context &ctx, const QVariantMap &node, const QUuid &objectId, const QTransform &transform)
{
    const QVariantMap symbolData = map(node, "symbolData");
    PendingInstance pending;
    pending.object = objectId;
    pending.symbolID = guidKey(symbolData.value(QStringLiteral("symbolID")));
    pending.overrides = list(symbolData, "symbolOverrides");
    pending.transform = transform;
    ctx.pendingInstances.push_back(std::move(pending));
}

void resolveInstances(Context &ctx)
{
    for (const PendingInstance &pending : ctx.pendingInstances) {
        VectorObject *object = ctx.document.find(pending.object);
        if (!object)
            continue;
        const auto masterFound = pending.symbolID.isEmpty() ? ctx.objectFor.end() : ctx.objectFor.find(pending.symbolID);
        if (masterFound == ctx.objectFor.end()) {
            ctx.warn(QStringLiteral("Some instances’ components couldn’t be found, and were left empty."));
            continue;
        }
        InstanceInfo instance;
        instance.master = masterFound->second;
        instance.placement = pending.transform;
        const std::vector<std::pair<QString, QUuid>> keys = Components::keys(ctx.document, masterFound->second);
        for (const QVariant &entry : pending.overrides) {
            const QVariantMap override = entry.toMap();
            const QVariantList guids = list(map(override, "guidPath"), "guids");
            if (guids.isEmpty())
                continue;
            const Guid target = guidKey(guids.last());
            const auto targetFound = target.isEmpty() ? ctx.objectFor.end() : ctx.objectFor.find(target);
            if (targetFound == ctx.objectFor.end())
                continue;
            const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto &pair) { return pair.second == targetFound->second; });
            if (key == keys.end())
                continue;
            InstanceOverride change;
            if (override.contains(QStringLiteral("textData")))
                change.text = str(map(override, "textData"), "characters");
            if (const QVariantList fills = list(override, "fillPaints"); !fills.empty()) {
                const QVariantMap paint = fills.front().toMap();
                if (str(paint, "type") == QLatin1String("SOLID"))
                    change.fill = Paint::solid(color(map(paint, "color"), num(paint, "opacity", 1)));
            }
            if (const QVariantList strokes = list(override, "strokePaints"); !strokes.empty()) {
                const QVariantMap paint = strokes.front().toMap();
                if (str(paint, "type") == QLatin1String("SOLID"))
                    change.stroke = Paint::solid(color(map(paint, "color"), num(paint, "opacity", 1)));
            }
            if (override.contains(QStringLiteral("visible")))
                change.visible = boolean(override, "visible");
            if (!change.isEmpty())
                instance.overrides[key->first] = change;
        }
        object->instance = instance;
    }
}
}
