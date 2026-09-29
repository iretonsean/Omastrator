#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>
#include <vector>

// Which local folder a site's code lives in (docs/OS-SUITE.md), kept in
// $XDG_CONFIG_HOME/omastrator/projects.json as {origin: folder}. Write-back is
// offered only for pages whose origin is registered.
namespace ProjectRegistry {
QString path();
// "https://example.com", "http://localhost:5173".
QString originOf(const QUrl &url);
std::optional<QString> folderFor(const QUrl &url);
// Whether the page is one of the user's own sites (a registered origin): the rest are somebody else's.
bool owns(const QUrl &url);
// Returns why it couldn't save, or empty.
QString remember(const QUrl &url, const QString &folder);
QString forget(const QUrl &url);

struct Suggestion {
    QString folder;
    // Why it matches, for the user to judge: "its git remote names example.com".
    QString reason;
    int score = 0;
};
// Likely folders for `url`, best first, from git remotes, .vercel/project.json,
// netlify.toml, wrangler.toml, package.json, folder names, and for localhost the
// process serving that port.
std::vector<Suggestion> suggest(const QUrl &url, const QStringList &roots = {});
// Folders people keep code in, under $HOME, that exist.
QStringList defaultRoots();
// The project folder of the process listening on 127.0.0.1:`port`, from /proc.
std::optional<QString> folderServing(int port);
}
