#pragma once
#include "IO/FigmaMapper.h"
#include <QTransform>
#include <QUuid>
#include <map>
#include <optional>

// Shared between FigmaImporter+Mapper.cpp and its +Part.cpp files: not part of
// the public API (FigmaMapper.h), just the pieces the mapper's own translation
// units pass to each other.
namespace FigmaMap {

// A component instance whose children we deferred (Components::sync rebuilds
// them from the master once every node has been mapped, so instances never
// need their own subtree walked; see docs/import/figma.md's Decisions).
struct PendingInstance {
    QUuid object;
    Guid symbolID;
    QVariantList overrides;
    QTransform transform;
};

struct Context {
    const Tree &tree;
    VectorDocument &document;
    QStringList &warnings;
    // Every mapped node, by Figma guid: instances resolve their master through this,
    // and a master's own descendants resolve an override's guidPath through it too.
    std::map<Guid, QUuid> objectFor;
    std::vector<PendingInstance> pendingInstances;
    std::map<QString, QByteArray> imagesByHash;
    // How deep the recursive walk is now; hostile files nest (or loop) without end.
    int depth = 0;

    void warn(const QString &message);
};

// FigmaImporter+Mapper.cpp: the recursive walk and shape geometry.
std::optional<QUuid> mapNode(Context &ctx, const Guid &guid, const QUuid &parent, const QTransform &parentTransform);
void applyCommon(Context &ctx, const QVariantMap &node, VectorObject &object);

// FigmaImporter+Paint.cpp
void applyFillsAndStrokes(Context &ctx, const QVariantMap &node, VectorObject &object);
void applyEffects(Context &ctx, const QVariantMap &node, VectorObject &object);
// A fill-only image paint, or std::nullopt when the node's fills don't reduce to one.
std::optional<QImage> soleImageFill(Context &ctx, const QVariantMap &node);

// FigmaImporter+Layout.cpp
void applyAutoLayout(Context &ctx, const QVariantMap &node, VectorObject &frame);
void applyLayoutItem(Context &ctx, const QVariantMap &node, const QVariantMap *parentNode, VectorObject &object);

// FigmaImporter+Text.cpp
void mapText(Context &ctx, const QVariantMap &node, VectorObject &object);

// FigmaImporter+Components.cpp
void mapComponent(Context &ctx, const QVariantMap &node, const Guid &guid, VectorObject &object, const QTransform &transform);
void deferInstance(Context &ctx, const QVariantMap &node, const QUuid &objectId, const QTransform &transform);
void resolveInstances(Context &ctx);

// FigmaImporter+VectorNetwork.cpp: node.vectorData.vectorNetworkBlob (Kiwi), or
// node.fillGeometry (REST, when ?geometry=paths was asked for) -> a local-space path.
VectorPath decodeVectorNetwork(Context &ctx, const QVariantMap &node);
}
