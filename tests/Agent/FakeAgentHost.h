#pragma once
#include "Agent/AgentHost.h"
#include "Document/EditorSession.h"
#include "Document/Swatches.h"
#include <QStringList>
#include <optional>

// Records what the tools ask of the app, around one real session.
class FakeAgentHost : public AgentHost {
public:
    EditorSession editor;
    bool hasSession = true;
    QString failure;
    QStringList opened;
    QStringList saved;
    QString variationsRequest;
    std::vector<AgentVariation> variations;
    std::optional<AgentRoast> roast;
    QString finishedTitle;
    QString finishedSummary;
    int finishedCount = 0;
    QJsonObject extras;

    EditorSession *session() override { return hasSession ? &editor : nullptr; }
    QString openFile(const QString &path) override
    {
        opened << path;
        return failure;
    }
    QString saveFile(const QString &path) override
    {
        saved << path;
        return failure;
    }
    void showVariations(const QString &requestId, const std::vector<AgentVariation> &shown) override
    {
        variationsRequest = requestId;
        variations = shown;
    }
    void showRoast(const AgentRoast &shown) override { roast = shown; }
    void proposalFinished(const QString &title, const QString &summary) override
    {
        finishedTitle = title;
        finishedSummary = summary;
        ++finishedCount;
    }
    QJsonObject statusExtras() override { return extras; }
    Swatches library{QString()};
    Swatches *swatches() override { return &library; }
    AiRequest lastAi;
    QString startAi(const AiRequest &request) override
    {
        lastAi = request;
        return failure;
    }
    // The last `live` call, and what it answers.
    QString liveAction;
    QJsonObject liveParams;
    QJsonObject liveResult;
    QString live(const QString &action, const QJsonObject &params, QJsonObject &result) override
    {
        liveAction = action;
        liveParams = params;
        result = liveResult;
        return failure;
    }
    QString newDocument(QSizeF size) override
    {
        editor.createDocument(size);
        return failure;
    }
    // Every `design` call, in order, and what the last one answers.
    std::vector<std::pair<QString, QJsonObject>> designCalls;
    QJsonObject designResult;
    QString design(const QString &action, const QJsonObject &params, QJsonObject &result) override
    {
        designCalls.emplace_back(action, params);
        result = designResult;
        return failure;
    }
    QStringList shownFiles;
    int windowShown = 0;
    QString shownFocus;
    bool shownRaise = true;
    QString showWindow(const QStringList &files, bool raise, const QString &focus) override
    {
        ++windowShown;
        shownFiles = files;
        shownRaise = raise;
        shownFocus = focus;
        return failure;
    }
};
