#pragma once
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

// Hands work to Omarchy's default agent, the way omadesign does: runs
// `omarchy agent prompt <prompt>` in a folder that tells the agent how to
// drive Omastrator. $OMASTRATOR_OMARCHY replaces `omarchy`, for tests.
namespace AgentLauncher {
// The agent Omarchy launches, or empty with `error` set.
QString defaultAgent(QString *error = nullptr);
// ~/.local/share/omastrator/agent, or under $XDG_DATA_HOME.
QString folder();
// AGENTS.md, CLAUDE.md and .mcp.json in `directory`. Returns why it failed, or empty.
QString writeInstructions(const QString &directory, const QString &binary, const QString &socket);
// The instructions AGENTS.md and CLAUDE.md hold.
QString instructions(const QString &binary);
// Writes the folder, then launches the agent there. Returns why it failed, or empty.
QString launch(const QString &taskPrompt, const QString &socket = QString());

// One round of Generate: what was asked, and the SVG the user picked from it.
struct Round {
    QString instruction;
    QString chosenSvg;
};

// The four flows in docs/AI-DESIGN.md; each names its request id and the methods that deliver it.
QString generatePrompt(const QString &requestId, const QString &brief, int count, std::optional<QRectF> fitTo,
                       const std::vector<Round> &history = {});
QString editPrompt(const QString &requestId, const QString &instruction, bool hasSelection);
enum class TraceMode { logo, sketch };
QString smartTracePrompt(const QString &requestId, const QString &traceGroupId, const QString &imagePath, TraceMode mode);
QString roastPrompt(const QString &requestId, const QString &renderPath, bool selectionOnly);
}
