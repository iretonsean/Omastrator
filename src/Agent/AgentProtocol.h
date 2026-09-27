#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <stdexcept>
#include <vector>

// Newline-delimited JSON-RPC 2.0 between the app, `omastrator agent` and
// `omastrator --mcp`. See docs/AI-DESIGN.md.
namespace AgentProtocol {
enum ErrorCode {
    parseError = -32700,
    invalidRequest = -32600,
    methodNotFound = -32601,
    invalidParams = -32602,
    internalError = -32603,
    // No document is open in the app.
    noDocument = -32000,
    // The user is mid-drag, or a proposal would be written over.
    busy = -32001,
    // A file could not be read or written.
    fileError = -32002,
    // The app is not running, or the socket dropped.
    notRunning = -32003,
};

// Thrown by the tools; becomes a JSON-RPC error.
struct Error : std::runtime_error {
    Error(int code, const QString &message) : std::runtime_error(message.toStdString()), code(code) {}
    QString message() const { return QString::fromStdString(what()); }
    int code;
};

struct Method {
    QString name;
    // read, edit, files or panels, as AI-DESIGN.md groups them.
    QString group;
    QString description;
    QJsonObject inputSchema;
    // False for the user's own desktop actions, which agents must not call: their edits are proposals.
    bool mcp = true;
};
// Every method the app answers, in the order help lists them.
const std::vector<Method> &methods();
const Method *method(const QString &name);

// $OMASTRATOR_SOCKET, else $XDG_RUNTIME_DIR/omastrator.sock, else /tmp/omastrator-<uid>.sock.
QString socketPath();

// One message as a compact line ending in '\n'.
QByteArray frame(const QJsonObject &message);
QJsonObject request(const QJsonValue &id, const QString &method, const QJsonObject &params);
QJsonObject result(const QJsonValue &id, const QJsonValue &result);
QJsonObject error(const QJsonValue &id, int code, const QString &message);

using Call = std::function<QJsonObject(const QString &method, const QJsonObject &params)>;
// Answers one request line through `call`; notifications answer with an empty object.
QJsonObject respond(const QByteArray &line, const Call &call);

// `omastrator agent --help`.
QString helpText();
// One method's parameters, for `omastrator agent <method> --help`.
QString methodHelp(const Method &method);
}
