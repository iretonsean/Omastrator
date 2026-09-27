#pragma once
#include "Document/VectorDocument.h"
#include "System/SyncPlan.h"
#include <QString>
#include <vector>

// The global library (docs/DESIGN-SYSTEMS.md): personal tokens and components in
// $XDG_DATA_HOME/omastrator/libraries/<name>.json, usable in any document and on
// any surface.
namespace Library {
struct Contents {
    QString name;
    QString path;
    std::vector<DesignToken> tokens;
    QStringList modes;
    // Each component's subtree, root first, as a document stores it.
    std::vector<VectorObject> objects;
    // The component sets it holds, in order.
    QStringList sets() const;
    // One set's variants and their layers, ready for EditorSession::placeFromLibrary.
    std::vector<VectorObject> set(const QString &name) const;
};

QString directory();
QString pathFor(const QString &name);
// Library names, alphabetically.
QStringList names();
// An empty library of that name when there's no file yet.
Contents load(const QString &name);
QByteArray serialize(const Contents &contents);

// Saves the document's tokens (merged by name) and components (whole sets replaced) into the library.
SyncPlan pushPlan(const QString &name, const VectorDocument &document);
// Tokens from the library into the document; `apply` runs when confirmed.
SyncPlan pullPlan(const QString &name, const QString &documentName, std::function<QString(const Contents &)> apply);
}
