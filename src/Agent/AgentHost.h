#pragma once
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QUuid>
#include <vector>

class EditorSession;
class Swatches;

// One option for the Variations panel; `svg` has already parsed once.
struct AgentVariation {
    QString name;
    QString svg;
    QString note;
};

// One point of the Roast panel's sincere feedback.
struct AgentFeedback {
    QString title;
    QString detail;
    std::vector<QUuid> objectIds;
};

struct AgentRoast {
    QString requestId;
    QString roast;
    std::vector<AgentFeedback> feedback;
    QString suggestedPrompt;
};

// What the app gives the agent bridge, so oma_agent needs no widgets.
// Everything is called on the thread the AgentServer lives on.
class AgentHost {
public:
    virtual ~AgentHost() = default;
    // The front document's session; null, or one without a document, when none is open.
    virtual EditorSession *session() = 0;
    // Each returns why it failed, or an empty string. They must not show alerts.
    virtual QString openFile(const QString &path) = 0;
    // An empty path saves to the document's own file.
    virtual QString saveFile(const QString &path) = 0;
    // The panels' results: not document edits.
    virtual void showVariations(const QString &requestId, const std::vector<AgentVariation> &variations) = 0;
    virtual void showRoast(const AgentRoast &roast) = 0;
    // The agent is done; the accept bar shows `summary` until Enter or Esc.
    virtual void proposalFinished(const QString &title, const QString &summary) = 0;
    // What only the app knows for status_get: the agent task it waits on, variations ready, the live session.
    virtual QJsonObject statusExtras() { return {}; }
    // The Swatches panel's library, or null in a host without one.
    virtual Swatches *swatches() { return nullptr; }
    // A new blank document in front, for a capture or a paste with none open. Returns why it failed, or empty.
    virtual QString newDocument(QSizeF size) = 0;
};
