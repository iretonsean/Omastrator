#pragma once
#include "Document/VectorDocument.h"
#include "System/SyncPlan.h"
#include <QJsonObject>
#include <QString>
#include <vector>

class Browser;

// A design system pulled from any page in Omastrator's browser (docs/DESIGN-SYSTEMS.md):
// its colours, type scale, spacing, radii and shadows by how often they're used,
// and elements repeated with the same classes as proposed components.
namespace SiteExtract {
// Runs in the page; returns {colors, fonts, spacing, radii, shadows, components} as JSON.
QString script();
// Evaluates the script in the page through the DevTools Protocol.
QJsonObject scan(Browser &browser, const QString &sessionId, QString *error);

struct Proposal {
    QString source;
    std::vector<DesignToken> tokens;
    // Each proposed component's layers, root (a component) first.
    std::vector<VectorObject> objects;
    QStringList components() const;
};
Proposal propose(const QJsonObject &scan, const QString &source);
// Takes the proposal into the document; `apply` runs when confirmed.
SyncPlan pullPlan(const Proposal &proposal, const QString &documentName, std::function<QString(const Proposal &)> apply);
}
