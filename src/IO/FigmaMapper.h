#pragma once
#include "Document/VectorDocument.h"
#include <QColor>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVariant>
#include <map>
#include <vector>

// The tree both Kiwi (paste, .fig) and the REST API normalise into, and the one
// mapper (FigmaImporter+Mapper.cpp and its +Part.cpp files) that turns it into a
// VectorDocument. Field names follow Figma's own Kiwi vocabulary (see
// docs/import/figma.md): a node is a QVariantMap keyed by name ("type", "name",
// "fillPaints", "stackMode", …), REST JSON translated into the same names.
namespace FigmaMap {

// "sessionID:localID" (Kiwi) or the REST node id, used as-is; empty is "no node".
using Guid = QString;

struct Node {
    QVariantMap fields;
    Guid guid;
    Guid parent;
    // parentIndex.position (Kiwi) or a zero-padded index (REST): children sort by this, plainly.
    QString position;
    std::vector<Guid> children;
};

struct Tree {
    std::map<Guid, Node> nodes;
    // CANVAS (page) nodes, in document order; each becomes an artboard.
    std::vector<Guid> pages;
    // Kiwi only: Message.blobs, indexed by the integers vectorNetworkBlob/dataBlob give.
    QVariantList blobs;
    // Kiwi/.fig only: image bytes already resolved by hash (zip images/, or a blob).
    std::map<QString, QByteArray> imagesByHash;
};

// Assembles the flat node list (Kiwi's nodeChanges, or REST nodes flattened by
// FigmaImporter+Rest.cpp) into a Tree: resolves parent/child links and sorts
// each parent's children by `position`.
Tree buildTree(const QVariantList &nodeChanges);

// The whole mapper: builds a VectorDocument with one artboard per page (or, for
// a single imported node, one artboard sized to it). Never throws; anything it
// can't carry over becomes a warning.
VectorDocument map(const Tree &tree, QStringList &warnings);
// A single node's subtree (File ▸ Import…'s ?node-id=), without page artboards.
VectorDocument mapNode(const Tree &tree, const Guid &nodeID, QStringList &warnings);

// GUID struct {sessionID, localID} -> "session:local"; invalid (either UINT_MAX) -> empty.
Guid guidKey(const QVariant &guidField);

// QVariantMap field access, forgiving of an absent key (Figma's Kiwi fields are
// mostly optional, each defaulting the way Figma's own editor does).
QString str(const QVariantMap &node, const char *key, const QString &fallback = {});
double num(const QVariantMap &node, const char *key, double fallback = 0);
bool boolean(const QVariantMap &node, const char *key, bool fallback = false);
QVariantList list(const QVariantMap &node, const char *key);
QVariantMap map(const QVariantMap &node, const char *key);
// {m00,m01,m02,m10,m11,m12} (Kiwi) or a [[m00,m01,m02],[m10,m11,m12]] array (REST).
QTransform matrix(const QVariant &field);
// {r,g,b,a} 0..1.
QColor color(const QVariantMap &node, double opacity = 1);
QPointF point(const QVariantMap &node);
}
