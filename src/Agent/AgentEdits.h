#pragma once
#include "Document/EditorSession.h"
#include "Document/VectorDocument.h"
#include <QRectF>
#include <QString>
#include <QUuid>
#include <optional>
#include <vector>

// EditorSession's object commands on a loose document, for building a
// proposal: the session's own versions each commit an undo step.
// Throw AgentProtocol::Error when the objects can't take the command.
namespace AgentEdits {
// Bottom to top, as the document stacks them.
std::vector<QUuid> inOrder(const VectorDocument &document, const std::vector<QUuid> &ids);
// `ids` without those inside another of them, so nothing moves twice.
std::vector<QUuid> roots(const VectorDocument &document, const std::vector<QUuid> &ids);
// Paths, texts and images under `ids`.
std::vector<QUuid> leaves(const VectorDocument &document, const std::vector<QUuid> &ids);
// `preferred` if it takes new objects, else the topmost visible, unlocked layer.
std::optional<QUuid> openLayer(const VectorDocument &document, std::optional<QUuid> preferred);
// An imported document's objects as one new group under `parent`; returns its id.
QUuid insertArt(VectorDocument &document, const VectorDocument &art, const QString &name, const QUuid &parent,
                std::optional<QUuid> above = std::nullopt);
// Scales `id` to fit `box`, keeping proportions, and centres it there.
void fit(VectorDocument &document, const QUuid &id, const QRectF &box);
QUuid group(VectorDocument &document, const std::vector<QUuid> &ids, const QString &name);
// The released children, and anything in `ids` that wasn't a group.
std::vector<QUuid> ungroup(VectorDocument &document, const std::vector<QUuid> &ids);
void arrange(VectorDocument &document, const std::vector<QUuid> &ids, ArrangeOrder order);
// `artboard` is the rectangle "artboard" aligns to: the active artboard of the page being edited.
void align(VectorDocument &document, const std::vector<QUuid> &ids, AlignEdge edge, AlignTarget target, const QRectF &artboard);
void distribute(VectorDocument &document, const std::vector<QUuid> &ids, DistributeAxis axis);
// The combined path's id, or none when the operation leaves nothing.
std::optional<QUuid> combine(VectorDocument &document, const std::vector<QUuid> &ids, BooleanOperation operation);
}
