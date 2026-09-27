#pragma once
#include "Document/Paint.h"
#include <QJsonObject>
#include <QString>
#include <QTransform>
#include <QUuid>
#include <map>
#include <optional>
#include <vector>

struct VectorDocument;
struct VectorObject;

// A main component: a group on the canvas whose copies (instances) follow it.
// Components sharing a `set` are its variants, told apart by `variant`.
struct ComponentInfo {
    // "Button"; a component on its own is a set of one.
    QString set;
    // Property → value: {"size": "md", "state": "hover"}.
    std::map<QString, QString> variant;
    // The component's frame in the document: moving it moves the frame, so instances don't jump.
    QTransform placement;
    friend bool operator==(const ComponentInfo &, const ComponentInfo &) = default;
};

// What an instance changes on one layer of its component, keyed by that layer's name path.
struct InstanceOverride {
    std::optional<Paint> fill;
    std::optional<Paint> stroke;
    std::optional<QString> text;
    std::optional<bool> visible;
    bool isEmpty() const { return !fill && !stroke && !text && !visible; }
    friend bool operator==(const InstanceOverride &, const InstanceOverride &) = default;
};

// How a layer looked as the instance last made it: a change since is an override.
struct InstanceMade {
    Paint fill;
    Paint stroke;
    QString text;
    bool visible = true;
    friend bool operator==(const InstanceMade &, const InstanceMade &) = default;
};

// An instance: a group whose children are copies of `master`'s, placed by `placement`.
struct InstanceInfo {
    QUuid master;
    QTransform placement;
    std::map<QString, InstanceOverride> overrides;
    // Not saved: rebuilt by the first sync.
    std::map<QString, InstanceMade> made;
    friend bool operator==(const InstanceInfo &, const InstanceInfo &) = default;
};

namespace Components {
// Rebuilds every instance from its component: edits made inside an instance since the last
// sync become overrides (fill, stroke colour, text, visibility), then the children are
// copied again with the overrides on top. Instances of a component that's gone are detached.
void sync(VectorDocument &document);
// Layers of `root`'s subtree by name path ("Label", "Icon/Path#2"), in document order.
std::vector<std::pair<QString, QUuid>> keys(const VectorDocument &document, const QUuid &root);
// Components by set, in document order.
std::vector<QUuid> masters(const VectorDocument &document);
std::vector<QUuid> variantsOf(const VectorDocument &document, const QString &set);
// Each property of a set and the values its variants use, in first-use order.
std::vector<std::pair<QString, QStringList>> properties(const VectorDocument &document, const QString &set);
// The variant of `set` closest to `wanted`: every property that matches counts.
std::optional<QUuid> bestVariant(const VectorDocument &document, const QString &set, const std::map<QString, QString> &wanted);
// "size=md, state=hover".
QString variantLabel(const std::map<QString, QString> &variant);
// Instances of `master` in the document.
std::vector<QUuid> instancesOf(const VectorDocument &document, const QUuid &master);

QJsonObject encode(const ComponentInfo &info);
ComponentInfo decodeComponent(const QJsonObject &json);
QJsonObject encode(const InstanceInfo &info);
InstanceInfo decodeInstance(const QJsonObject &json);
}
