#pragma once
#include "Agent/AgentHost.h"
#include "Document/EditorSession.h"
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
};
