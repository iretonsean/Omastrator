#include "Document/BrowserAddress.h"
#include <QRegularExpression>

namespace {
bool local(const QString &host)
{
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("[::1]");
}
}

bool BrowserAddress::allowed(const QUrl &url)
{
    const QString kind = url.scheme().toLower();
    return url.isValid() && !url.host().isEmpty() && (kind == QLatin1String("http") || kind == QLatin1String("https"));
}

std::optional<QUrl> BrowserAddress::parse(const QString &typed)
{
    const QString text = typed.trimmed();
    if (text.isEmpty() || text.contains(QRegularExpression(QStringLiteral("\\s"))))
        return std::nullopt;
    // "host:port" reads as a scheme to QUrl, so a scheme counts only when "//" follows it or it is a known one.
    static const QRegularExpression scheme(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):"));
    const auto match = scheme.match(text);
    QUrl url;
    if (match.hasMatch() && !QRegularExpression(QStringLiteral("^[^/]*:\\d+(/|$)")).match(text).hasMatch()) {
        url = QUrl(text, QUrl::StrictMode);
    } else {
        const QString host = text.section(QRegularExpression(QStringLiteral("[/:?#]")), 0, 0).toLower();
        url = QUrl(QStringLiteral("%1://%2").arg(local(host) ? "http" : "https", text), QUrl::StrictMode);
    }
    if (!allowed(url))
        return std::nullopt;
    return url;
}

QString BrowserAddress::shown(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return {};
    QString host = url.host();
    if (host.startsWith(QLatin1String("www.")))
        host.remove(0, 4);
    if (url.port() > 0)
        host += QLatin1Char(':') + QString::number(url.port());
    QString path = url.path(QUrl::FullyDecoded);
    if (path == QLatin1String("/"))
        path.clear();
    if (url.hasQuery())
        path += QLatin1Char('?') + url.query();
    return host + path;
}
