#pragma once
#include <QString>
#include <QUrl>
#include <optional>

// What a Browser View's address field accepts (docs/BROWSER-VIEW.md, section 4).
namespace BrowserAddress {
// The page for what was typed: http and https only, a bare host becoming https:// (http:// for localhost and
// 127.0.0.1). Nothing for anything else, and for an empty string.
std::optional<QUrl> parse(const QString &typed);
// The address as the bar shows it: host and path, without the scheme or a leading www.
QString shown(const QUrl &url);
}
