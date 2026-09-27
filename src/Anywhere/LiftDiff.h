#pragma once
#include "Document/VectorDocument.h"
#include <QJsonObject>
#include <QStringList>
#include <vector>

// Changes to lifted page vectors, back as page edits (docs/ANYWHERE.md): each
// lifted object keeps its element's selector in `liftedFrom`, and a snapshot
// taken when it landed says what it looked like then. Text, colours, radius,
// size and spacing (a box's edges moved around its content) become edits for
// Live's write-back.
namespace LiftDiff {
// Every lifted object under `root` as it is now, by id.
QJsonObject snapshot(const VectorDocument &document, const QUuid &root);

struct Change {
    QString selector;
    // A CSS property, or "text".
    QString property;
    // The new value; for a relative change, the pixels to add to the element's own value.
    QString value;
    bool relative = false;
    double delta = 0;
    // "#title: color #111111 → #e11d48", for the message.
    QString description;
};
// What differs from `baseline` among the lifted objects under `roots`. `notes` gets what can't be applied, plainly.
std::vector<Change> changes(const VectorDocument &document, const std::vector<QUuid> &roots, const QJsonObject &baseline, QStringList *notes);

// The snapshots, kept beside the overlays as lifted.json.
QJsonObject readBaselines(const QString &path);
QString writeBaselines(const QString &path, const QJsonObject &baselines);
// A colour as CSS writes it: #rrggbb, or rgba() when it's see-through.
QString cssColor(const QColor &color);
}
