#pragma once
#include "Document/VectorDocument.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

// Share with client (docs/SHARE.md): what gets shared, where it can go, and what
// was shared, kept per document in $XDG_CONFIG_HOME/omastrator/shares.json and
// never inside the .omai.
namespace Share {
enum class Format { png, pdf, svg };
QString suffix(Format format);
// "PNG at 2×", "PDF", "SVG": what the popover and toast say.
QString label(Format format);
std::optional<Format> parseFormat(const QString &suffix);
// PNGs are shared at twice the artboard's size, so they stay sharp on a client's laptop.
constexpr double pngScale = 2;

// Destination ids: "cloud:<remote>", "github", "live".
QString cloudDestination(const QString &remote);
// The remote named by a "cloud:" id, else empty.
QString remoteOf(const QString &destination);
inline const QString github = QStringLiteral("github");
inline const QString live = QStringLiteral("live");

// One thing shared: the link, and what Unshare needs to take it down again.
struct Record {
    QString id;
    QString link;
    QDateTime time;
    // "cloud:<remote>", "gist", "release" or "live".
    QString kind;
    // What the list says: "Google Drive", "GitHub gist", "GitHub release", "Preview deploy".
    QString where;
    // "png", "pdf", "svg", or "site" for a deploy.
    QString format;
    // "artboard" or "selection", and the selection's objects, for Paste client feedback.
    QString scope;
    QStringList objects;
    // Unshare: the cloud file ("remote:path"), the gist, or the release's repository and tag.
    QString remotePath;
    QString gist;
    QString repository;
    QString tag;

    bool canUnshare() const { return kind != live; }
    QJsonObject toJson() const;
    static Record fromJson(const QJsonObject &json);
};

// What one document remembers: the last format and destination chosen, and its shares, newest last.
struct DocumentShares {
    std::optional<Format> format;
    QString destination;
    std::vector<Record> shared;
};
QString storePath();
DocumentShares load(const QString &key);
QString save(const QString &key, const DocumentShares &shares);
// A document saved for the first time keeps what it had shared while untitled.
QString rename(const QString &from, const QString &to);
// The first upload to GitHub asks once; this is the answer.
bool githubConfirmed();
QString setGithubConfirmed(bool confirmed);

// The selection alone, moved so its bounds (strokes included) are the artboard, on clear paper.
VectorDocument selectionDocument(const VectorDocument &document, const std::vector<QUuid> &ids);
// Writes `document` as `format`; throws FileError. PNG is at pngScale; paper is left out when it's clear.
void write(const VectorDocument &document, Format format, const QString &path);
// A name safe in every service and in GitHub asset names: letters, digits, dashes, dots and underscores.
QString safeName(const QString &name);
// "2026-09-27-141200"
QString stamp(const QDateTime &time);
}
