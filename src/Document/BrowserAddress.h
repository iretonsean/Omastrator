#pragma once
#include <QString>
#include <QUrl>
#include <optional>

// What a Browser View's address field accepts (docs/BROWSER-VIEW.md, section 4).
namespace BrowserAddress {
// The page for what was typed: http and https only, a bare host becoming https:// (http:// for localhost and
// 127.0.0.1). Nothing for anything else, and for an empty string.
std::optional<QUrl> parse(const QString &typed);
// Whether a page may be opened at this address: a valid http or https URL with a host. Everything that reaches a tab
// from a file, the clipboard or an agent goes through this, not just what the bar parsed.
bool allowed(const QUrl &url);
// The address as the bar shows it: host and path, without the scheme or a leading www.
QString shown(const QUrl &url);
}
