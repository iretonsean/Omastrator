#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <vector>

struct LiveEdit;

// Edits to a site that isn't yours (docs/ANYWHERE.md): real DOM and CSS
// changes in Omastrator's browser, kept on this machine as named sets per
// origin in $XDG_DATA_HOME/omastrator/edit-sets.json. An enabled set is put
// back on every visit; none is ever deployed.
namespace EditSets {
struct Edit {
    // The page's path ("/pricing"): an edit is put back only there.
    QString path;
    QString selector;
    // A CSS property, or "text".
    QString property;
    QString value;
    QString before;
    // The page's own token it snapped to ("--brand"), if any.
    QString token;
    QString addClass;
    QString removeClass;

    QJsonObject toJson() const;
    static Edit fromJson(const QJsonObject &json);
    static Edit fromLive(const LiveEdit &edit);
    friend bool operator==(const Edit &, const Edit &) = default;
};

struct Set {
    QString name;
    bool enabled = true;
    QDateTime updated;
    std::vector<Edit> edits;
    QJsonObject summary() const;
};

// $XDG_DATA_HOME/omastrator/edit-sets.json.
QString path();
// "https://example.com", "http://127.0.0.1:5173"; a file page's folder as "file:///home/…/site".
QString originOf(const QUrl &url);
// A page's path as edits record it.
QString pathOf(const QUrl &url);

std::vector<Set> read(const QString &origin);
// Keeps `edits` in the set called `name`, made if new; a later edit to the same thing replaces the earlier one.
// Returns why it couldn't save, or empty.
QString keep(const QString &origin, const QString &name, const std::vector<Edit> &edits);
QString setEnabled(const QString &origin, const QString &name, bool enabled);
QString remove(const QString &origin, const QString &name);
// "Edits 1", "Edits 2": the first name not yet used on this origin.
QString suggestedName(const QString &origin);
// What goes back on `path`: every enabled set's edits for it, oldest set first.
std::vector<Edit> active(const QString &origin, const QString &path);

// The edits as a style sheet: plain CSS, or a userstyle (Stylus, the ==UserStyle== header and @-moz-document per
// page). Text changes can't be made in CSS, so they're listed in a comment.
QString css(const QString &origin, const QString &name, const std::vector<Edit> &edits, bool userstyle);
// The same as a diff against the page's own values, for an agent: "selector { property: before → after }".
QString diff(const std::vector<Edit> &edits);
QJsonArray toJson(const std::vector<Edit> &edits);
}
