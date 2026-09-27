#pragma once
#include <QColor>
#include <QString>
#include <QStringList>
#include <vector>

// One question the Connect sheet asks before rclone takes over.
struct CloudField {
    QString key;   // rclone's option name
    QString label;
    bool secret = false;
    bool optional = false;
    QStringList choices; // values; the first is the default
    QString placeholder;
};

// A storage service rclone can reach. Sign-in (OAuth, 2FA) happens in rclone's own questions.
struct CloudProvider {
    QString type; // rclone backend name; empty for "Other"
    QString name;
    QString badge; // one or two letters for the badge
    QColor color;
    // Signs in through the browser, so there is nothing to type first.
    bool browserSignIn = false;
    std::vector<CloudField> fields;
    QString suggestedRemote;
};

namespace CloudProviders {
// The popular ones first, then "Other (any rclone backend)".
const std::vector<CloudProvider> &all();
// A known provider for an rclone type, or a generic one named after the type.
CloudProvider forType(const QString &type);
}
