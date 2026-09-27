#pragma once
#include "Agent/AgentHost.h"
#include "Document/VectorDocument.h"
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <optional>
#include <vector>

class EditorSession;

// Every method in docs/AI-DESIGN.md, run against the host's front session.
// Edits land in one open interaction, the proposal, which only the user
// commits (Enter) or cancels (Esc).
class AgentTools : public QObject {
    Q_OBJECT
public:
    explicit AgentTools(AgentHost &host, QObject *parent = nullptr);

    // Runs one method; throws AgentProtocol::Error.
    QJsonObject call(const QString &method, const QJsonObject &params);

    // True while this bridge's proposal is the session's open interaction.
    bool hasProposal() const;
    // The session the proposal is open in, or null.
    EditorSession *proposalSession() const;
    // "AI: <title>", as the undo step will be named.
    QString proposalTitle() const;
    // status_get's answer: never throws, and needs no document.
    QJsonObject status();
    // The `design` method's actions (docs/ANYWHERE.md).
    static const QStringList &designActions();

    // The last screenshot open_capture traced, which Vectorize with AI can take next.
    struct Capture {
        QPointer<EditorSession> session;
        QUuid group;
        QString imagePath;
    };
    // Only while its traced group is still in the front document.
    std::optional<Capture> pendingCapture();

signals:
    // A proposal opened, grew or was renamed.
    void proposalChanged();

private:
    EditorSession &session();
    const VectorDocument &document();
    bool ownsProposal(const EditorSession &session) const;
    // What an edit starts from: the proposal as it stands, or the document.
    VectorDocument draft();
    // Shows `document` as the proposal, opening one named for `title` if needed.
    void propose(const QString &title, const VectorDocument &document, const std::vector<QUuid> &selection);
    // `ids`, else the selection; every one must exist and be no layer.
    std::vector<QUuid> targets(const QJsonObject &params, bool required = false);
    std::vector<QUuid> unlocked(const VectorDocument &document, const std::vector<QUuid> &ids) const;

    // Read.
    QJsonObject documentGet(const QJsonObject &params);
    QJsonObject selectionGet();
    QJsonObject render(const QJsonObject &params);
    // Edit.
    QJsonObject insertSvg(const QJsonObject &params);
    QJsonObject setStyle(const QJsonObject &params);
    QJsonObject transform(const QJsonObject &params);
    QJsonObject arrange(const QJsonObject &params);
    QJsonObject align(const QJsonObject &params);
    QJsonObject distribute(const QJsonObject &params);
    QJsonObject group(const QJsonObject &params);
    QJsonObject ungroup(const QJsonObject &params);
    QJsonObject pathfinder(const QJsonObject &params);
    QJsonObject remove(const QJsonObject &params);
    QJsonObject select(const QJsonObject &params);
    QJsonObject updateObject(const QJsonObject &params);
    QJsonObject replaceObjects(const QJsonObject &params);
    QJsonObject proposalFinish(const QJsonObject &params);
    QJsonObject traceImage(const QJsonObject &params);
    // Files.
    QJsonObject open(const QJsonObject &params);
    QJsonObject save(const QJsonObject &params);
    QJsonObject exportFile(const QJsonObject &params);
    QJsonObject place(const QJsonObject &params);
    // Session.
    QJsonObject selectTool(const QJsonObject &params);
    // Desktop: the user's own actions from the island, each a normal undo step.
    QJsonObject applyColor(const QJsonObject &params);
    QJsonObject swatchesGet(const QJsonObject &params);
    QJsonObject swatchesAdd(const QJsonObject &params);
    QJsonObject openCapture(const QJsonObject &params);
    QJsonObject pasteSvg(const QJsonObject &params);
    QJsonObject newDocument(const QJsonObject &params);
    QJsonObject showPanel(const QJsonObject &params);
    QJsonObject aiStart(const QJsonObject &params);
    QJsonObject liveDeployed(const QJsonObject &params);
    QJsonObject live(const QJsonObject &params);
    QJsonObject command(const QJsonObject &params);
    QJsonObject design(const QJsonObject &params);
    QJsonObject showWindow(const QJsonObject &params);
    QJsonObject quitApp(const QJsonObject &params);
    // The session to act on for the user; refused while a drag or proposal is open.
    EditorSession &idleSession();
    // Commits `document` as one undo step named `name`.
    void commit(EditorSession &session, const QString &name, const VectorDocument &document, const std::vector<QUuid> &selection);
    // Panels.
    QJsonObject showVariations(const QJsonObject &params);
    QJsonObject showRoast(const QJsonObject &params);

    AgentHost &m_host;
    QPointer<EditorSession> m_session;
    // The document as last proposed: a user's drag never matches it.
    std::optional<VectorDocument> m_preview;
    QString m_title;
    std::optional<Capture> m_capture;
};
