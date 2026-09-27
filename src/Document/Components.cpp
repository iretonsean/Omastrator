#include "Document/Components.h"
#include "Document/DocumentCodec.h"
#include "Document/VectorDocument.h"
#include <QJsonArray>
#include <algorithm>
#include <map>
#include <set>

namespace {
QJsonArray matrix(const QTransform &t)
{
    return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}

QTransform readMatrix(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 6)
        return {};
    return QTransform(a[0].toDouble(1), a[1].toDouble(), a[2].toDouble(), a[3].toDouble(1), a[4].toDouble(), a[5].toDouble());
}

InstanceMade madeOf(const VectorObject &object)
{
    return {object.fill, object.stroke.paint, object.kind == ObjectKind::text ? object.text.text : QString(), object.isVisible};
}

// A paint changed by hand, not just recoloured by its token.
bool paintChanged(const Paint &now, const Paint &made)
{
    if (!now.token.isEmpty() && now.token == made.token)
        return false;
    return now != made;
}

// The instance's children, rebuilt from its component.
void rebuild(VectorDocument &document, VectorObject &instance, const VectorObject &master)
{
    const QUuid instanceId = instance.id;
    InstanceInfo &info = *instance.instance;
    // Edits since the last sync are overrides.
    const auto currentKeys = Components::keys(document, instanceId);
    for (const auto &[key, id] : currentKeys) {
        const auto made = info.made.find(key);
        const VectorObject *child = document.find(id);
        if (made == info.made.end() || !child)
            continue;
        InstanceOverride &change = info.overrides[key];
        if (child->hasPaint() && paintChanged(child->fill, made->second.fill))
            change.fill = child->fill;
        if (child->hasPaint() && paintChanged(child->stroke.paint, made->second.stroke))
            change.stroke = child->stroke.paint;
        if (child->kind == ObjectKind::text && child->text.text != made->second.text)
            change.text = child->text.text;
        if (child->isVisible != made->second.visible)
            change.visible = child->isVisible;
    }
    std::erase_if(info.overrides, [](const auto &entry) { return entry.second.isEmpty(); });

    const QTransform delta = master.component->placement.inverted() * info.placement;
    const auto masterKeys = Components::keys(document, master.id);
    std::map<QUuid, QUuid> renamed{{master.id, instanceId}};
    std::vector<VectorObject> copies;
    std::map<QString, InstanceMade> made;
    for (const auto &[key, id] : masterKeys) {
        const VectorObject *source = document.find(id);
        VectorObject copy = *source;
        copy.id = QUuid::createUuidV5(instanceId, key);
        renamed[source->id] = copy.id;
        copy.parentID = source->parentID ? std::optional(renamed.at(*source->parentID)) : std::nullopt;
        copy.component.reset();
        copy.instance.reset();
        copy.isLocked = false;
        if (const auto change = info.overrides.find(key); change != info.overrides.end()) {
            if (change->second.fill && copy.hasPaint())
                copy.fill = *change->second.fill;
            if (change->second.stroke && copy.hasPaint())
                copy.stroke.paint = *change->second.stroke;
            if (change->second.text && copy.kind == ObjectKind::text)
                copy.text.replace(0, int(copy.text.text.size()), *change->second.text);
            if (change->second.visible)
                copy.isVisible = *change->second.visible;
        }
        copies.push_back(std::move(copy));
    }
    // Out with the old children, in with the copies where they stood.
    const int at = document.indexOf(instanceId);
    document.remove(document.children(instanceId));
    document.objects.insert(document.objects.begin() + at + 1, copies.begin(), copies.end());
    for (const QUuid &child : document.children(instanceId))
        document.transform(child, delta);
    VectorObject *fresh = document.find(instanceId);
    for (const auto &[key, id] : masterKeys) {
        if (const VectorObject *child = document.find(renamed.at(id)))
            made[key] = madeOf(*child);
    }
    fresh->instance->made = std::move(made);
}
}

namespace Components {
std::vector<std::pair<QString, QUuid>> keys(const VectorDocument &document, const QUuid &root)
{
    std::vector<std::pair<QString, QUuid>> result;
    std::function<void(const QUuid &, const QString &)> walk = [&](const QUuid &parent, const QString &prefix) {
        std::map<QString, int> seen;
        for (const QUuid &child : document.children(parent)) {
            const VectorObject *object = document.find(child);
            QString name = object->name.isEmpty() ? rawValue(object->kind) : object->name;
            name.replace(QLatin1Char('/'), QLatin1Char('-'));
            const int count = ++seen[name];
            const QString key = prefix + name + (count > 1 ? QStringLiteral("#%1").arg(count) : QString());
            result.emplace_back(key, child);
            walk(child, key + QLatin1Char('/'));
        }
    };
    walk(root, QString());
    return result;
}

void sync(VectorDocument &document)
{
    std::vector<QUuid> instances;
    for (const VectorObject &object : document.objects) {
        if (object.instance)
            instances.push_back(object.id);
    }
    if (instances.empty())
        return;
    // Instances inside components go first, so instances of those components copy them fresh.
    std::stable_sort(instances.begin(), instances.end(), [&](const QUuid &a, const QUuid &b) {
        const auto insideMaster = [&](const QUuid &id) {
            for (auto parent = document.find(id)->parentID; parent; parent = document.find(*parent)->parentID) {
                if (document.find(*parent)->component)
                    return true;
            }
            return false;
        };
        return insideMaster(a) && !insideMaster(b);
    });
    for (const QUuid &id : instances) {
        VectorObject *instance = document.find(id);
        if (!instance || !instance->instance)
            continue;
        const VectorObject *master = document.find(instance->instance->master);
        // Its component is gone, or it would contain itself: it stays as it is, detached.
        if (!master || !master->component || master->id == id || document.isAncestor(id, master->id) || document.isAncestor(master->id, id)) {
            instance->instance.reset();
            instance->kind = ObjectKind::group;
            continue;
        }
        rebuild(document, *instance, *master);
    }
}

std::vector<QUuid> masters(const VectorDocument &document)
{
    std::vector<QUuid> result;
    for (const VectorObject &object : document.objects) {
        if (object.component)
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> variantsOf(const VectorDocument &document, const QString &set)
{
    std::vector<QUuid> result;
    for (const VectorObject &object : document.objects) {
        if (object.component && object.component->set == set)
            result.push_back(object.id);
    }
    return result;
}

std::vector<std::pair<QString, QStringList>> properties(const VectorDocument &document, const QString &set)
{
    std::vector<std::pair<QString, QStringList>> result;
    for (const QUuid &id : variantsOf(document, set)) {
        for (const auto &[property, value] : document.find(id)->component->variant) {
            auto found = std::find_if(result.begin(), result.end(), [&](const auto &entry) { return entry.first == property; });
            if (found == result.end()) {
                result.emplace_back(property, QStringList());
                found = result.end() - 1;
            }
            if (!found->second.contains(value))
                found->second.append(value);
        }
    }
    return result;
}

std::optional<QUuid> bestVariant(const VectorDocument &document, const QString &set, const std::map<QString, QString> &wanted)
{
    std::optional<QUuid> best;
    int bestScore = -1;
    for (const QUuid &id : variantsOf(document, set)) {
        const auto &variant = document.find(id)->component->variant;
        int score = 0;
        for (const auto &[property, value] : wanted) {
            const auto found = variant.find(property);
            if (found != variant.end() && found->second == value)
                score += 2;
        }
        if (variant == wanted)
            score += 1;
        if (score > bestScore) {
            bestScore = score;
            best = id;
        }
    }
    return best;
}

QString variantLabel(const std::map<QString, QString> &variant)
{
    QStringList parts;
    for (const auto &[property, value] : variant)
        parts.append(property + QLatin1Char('=') + value);
    return parts.join(QStringLiteral(", "));
}

std::vector<QUuid> instancesOf(const VectorDocument &document, const QUuid &master)
{
    std::vector<QUuid> result;
    for (const VectorObject &object : document.objects) {
        if (object.instance && object.instance->master == master)
            result.push_back(object.id);
    }
    return result;
}

QJsonObject encode(const ComponentInfo &info)
{
    QJsonObject variant;
    for (const auto &[property, value] : info.variant)
        variant[property] = value;
    return {{"set", info.set}, {"variant", variant}, {"placement", matrix(info.placement)}};
}

ComponentInfo decodeComponent(const QJsonObject &json)
{
    ComponentInfo info;
    info.set = json["set"].toString();
    const QJsonObject variant = json["variant"].toObject();
    for (auto each = variant.begin(); each != variant.end(); ++each)
        info.variant[each.key()] = each.value().toString();
    info.placement = readMatrix(json["placement"]);
    return info;
}

QJsonObject encode(const InstanceInfo &info)
{
    QJsonObject overrides;
    for (const auto &[key, change] : info.overrides) {
        QJsonObject json;
        if (change.fill)
            json["fill"] = DocumentCodec::encode(*change.fill);
        if (change.stroke)
            json["stroke"] = DocumentCodec::encode(*change.stroke);
        if (change.text)
            json["text"] = *change.text;
        if (change.visible)
            json["visible"] = *change.visible;
        overrides[key] = json;
    }
    return {{"master", info.master.toString(QUuid::WithoutBraces)}, {"placement", matrix(info.placement)}, {"overrides", overrides}};
}

InstanceInfo decodeInstance(const QJsonObject &json)
{
    InstanceInfo info;
    info.master = QUuid::fromString(json["master"].toString());
    info.placement = readMatrix(json["placement"]);
    const QJsonObject overrides = json["overrides"].toObject();
    for (auto each = overrides.begin(); each != overrides.end(); ++each) {
        const QJsonObject change = each.value().toObject();
        InstanceOverride made;
        if (change.contains("fill"))
            made.fill = DocumentCodec::decodePaint(change["fill"].toObject());
        if (change.contains("stroke"))
            made.stroke = DocumentCodec::decodePaint(change["stroke"].toObject());
        if (change.contains("text"))
            made.text = change["text"].toString();
        if (change.contains("visible"))
            made.visible = change["visible"].toBool();
        if (!made.isEmpty())
            info.overrides[each.key()] = made;
    }
    return info;
}
}
