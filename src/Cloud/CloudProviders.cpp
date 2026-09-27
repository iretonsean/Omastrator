#include "Cloud/CloudProviders.h"

namespace {
CloudField text(const char *key, const char *label, const char *placeholder = "", bool optional = false)
{
    return {QString::fromUtf8(key), QString::fromUtf8(label), false, optional, {}, QString::fromUtf8(placeholder)};
}

CloudField secret(const char *key, const char *label, bool optional = false)
{
    return {QString::fromUtf8(key), QString::fromUtf8(label), true, optional, {}, {}};
}

CloudField choice(const char *key, const char *label, QStringList values)
{
    return {QString::fromUtf8(key), QString::fromUtf8(label), false, false, std::move(values), {}};
}

CloudProvider browser(const char *type, const char *name, const char *badge, QColor color)
{
    return {QString::fromUtf8(type), QString::fromUtf8(name), QString::fromUtf8(badge), color, true, {}, QString::fromUtf8(type)};
}

CloudProvider form(const char *type, const char *name, const char *badge, QColor color, std::vector<CloudField> fields, const char *remote)
{
    return {QString::fromUtf8(type), QString::fromUtf8(name), QString::fromUtf8(badge), color, false, std::move(fields), QString::fromUtf8(remote)};
}
}

namespace CloudProviders {
const std::vector<CloudProvider> &all()
{
    static const std::vector<CloudProvider> list{
        browser("drive", "Google Drive", "G", QColor(0x1f, 0xa4, 0x63)),
        browser("dropbox", "Dropbox", "D", QColor(0x00, 0x61, 0xfe)),
        browser("onedrive", "OneDrive", "O", QColor(0x08, 0x78, 0xd4)),
        form("iclouddrive", "iCloud Drive", "i", QColor(0x3d, 0x9b, 0xf5),
             {text("apple_id", "Apple ID", "name@icloud.com"), secret("password", "Password")}, "icloud"),
        browser("box", "Box", "B", QColor(0x00, 0x61, 0xd5)),
        form("protondrive", "Proton Drive", "P", QColor(0x6d, 0x4a, 0xff),
             {text("username", "Username"), secret("password", "Password"), text("2fa", "Two-factor code", "If it's on", true)}, "proton"),
        browser("pcloud", "pCloud", "pC", QColor(0x17, 0xbe, 0xd0)),
        form("mega", "Mega", "M", QColor(0xd9, 0x27, 0x2e), {text("user", "Email"), secret("pass", "Password")}, "mega"),
        form("s3", "Amazon S3 and compatible", "S3", QColor(0xe2, 0x8a, 0x1f),
             {choice("provider", "Provider", {"AWS", "Cloudflare", "Wasabi", "DigitalOcean", "Minio", "Other"}),
              text("access_key_id", "Access key ID"), secret("secret_access_key", "Secret access key"),
              text("region", "Region", "us-east-1", true), text("endpoint", "Endpoint", "Needed for R2 and most others", true)},
             "s3"),
        form("b2", "Backblaze B2", "B2", QColor(0xe2, 0x1e, 0x29), {text("account", "Key ID"), secret("key", "Application key")}, "b2"),
        form("webdav", "Nextcloud or WebDAV", "W", QColor(0x00, 0x82, 0xc9),
             {text("url", "Server URL", "https://cloud.example.com/remote.php/dav/files/you"),
              choice("vendor", "Server", {"nextcloud", "owncloud", "sharepoint", "other"}), text("user", "User"), secret("pass", "Password")},
             "nextcloud"),
        form("sftp", "SFTP", "SF", QColor(0x5a, 0x6b, 0x7c),
             {text("host", "Host", "example.com"), text("user", "User"), text("port", "Port", "22", true), secret("pass", "Password", true),
              text("key_file", "Key file", "~/.ssh/id_ed25519", true)},
             "sftp"),
        form("", "Other (any rclone backend)", "…", QColor(0x80, 0x80, 0x80), {text("type", "rclone type", "ftp, koofr, seafile…")}, "remote"),
    };
    return list;
}

CloudProvider forType(const QString &type)
{
    for (const CloudProvider &provider : all()) {
        if (!provider.type.isEmpty() && provider.type == type)
            return provider;
    }
    // Anything else rclone knows: named after its backend.
    CloudProvider other{type, type, type.left(1).toUpper(), QColor(0x80, 0x80, 0x80), false, {}, type};
    if (type == QLatin1String("local"))
        other.name = QStringLiteral("Local folder");
    return other;
}
}
