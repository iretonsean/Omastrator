#include "IO/FigmaImporter.h"
#include "IO/FigmaKiwi.h"
#include "IO/FigmaMapper.h"
#include "IO/ZipReader.h"
#include "Logging.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

// Ties the pieces together: FigmaKiwi decodes the wire format, FigmaMap builds
// the tree and maps it. This file is the public surface (docs/import/figma.md):
// .fig files (raw or zipped), paste and the token store. The REST API's own
// JSON is FigmaImporter+Rest.cpp, feeding the same FigmaMap::Tree.
namespace FigmaImporter {
namespace {
// Bigger than any real file; matches SvgImporter's own cap.
constexpr qint64 maximumBytes = qint64(256) << 20;

// .fig as a zip: canvas.fig is the fig-kiwi container; images/<hex hash> sit beside it.
QByteArray canvasBytes(const QByteArray &data)
{
    if (!data.startsWith("PK"))
        return data;
    ZipReader zip(data);
    if (!zip.isValid() || !zip.entries().contains(QStringLiteral("canvas.fig")))
        throw FileError(QStringLiteral("This doesn’t look like a Figma file."));
    return zip.read(QStringLiteral("canvas.fig"));
}

QHash<QString, QByteArray> zipImages(const QByteArray &data)
{
    QHash<QString, QByteArray> images;
    if (!data.startsWith("PK"))
        return images;
    const ZipReader zip(data);
    if (!zip.isValid())
        return images;
    for (const QString &entry : zip.entries()) {
        if (!entry.startsWith(QStringLiteral("images/")))
            continue;
        const qsizetype dot = entry.lastIndexOf(QLatin1Char('.'));
        const QString hash = entry.mid(7, (dot > 0 ? dot : entry.size()) - 7);
        images.insert(hash, zip.read(entry));
    }
    return images;
}

// Every IMAGE paint's hash, resolved from the zip's images/ folder first, else
// a Kiwi blob (dataBlob); left unresolved when neither has it.
void collectImagesFrom(const QVariantMap &fields, const char *key, FigmaMap::Tree &tree, const QHash<QString, QByteArray> &zip)
{
    for (const QVariant &entry : fields.value(QLatin1String(key)).toList()) {
        const QVariantMap paint = entry.toMap();
        if (paint.value(QStringLiteral("type")).toString() != QLatin1String("IMAGE"))
            continue;
        const QVariantMap image = paint.value(QStringLiteral("image")).toMap();
        const QString hash = QString::fromLatin1(image.value(QStringLiteral("hash")).toByteArray().toHex());
        if (hash.isEmpty() || tree.imagesByHash.count(hash))
            continue;
        if (const auto found = zip.constFind(hash); found != zip.constEnd()) {
            tree.imagesByHash[hash] = found.value();
            continue;
        }
        if (image.contains(QStringLiteral("dataBlob"))) {
            const int index = image.value(QStringLiteral("dataBlob")).toInt();
            if (index >= 0 && index < tree.blobs.size())
                tree.imagesByHash[hash] = tree.blobs[index].toMap().value(QStringLiteral("bytes")).toByteArray();
        }
    }
}

VectorDocument fromNodeChanges(const QVariantList &nodeChanges, const QVariantList &blobs, const QHash<QString, QByteArray> &zip, QStringList *warnings)
{
    FigmaMap::Tree tree = FigmaMap::buildTree(nodeChanges);
    tree.blobs = blobs;
    for (auto &[guid, node] : tree.nodes) {
        Q_UNUSED(guid);
        collectImagesFrom(node.fields, "fillPaints", tree, zip);
        collectImagesFrom(node.fields, "strokePaints", tree, zip);
    }
    QStringList localWarnings;
    VectorDocument document = FigmaMap::map(tree, localWarnings);
    localWarnings.removeDuplicates();
    if (warnings)
        *warnings = localWarnings;
    qCInfo(lcIO) << "parsed Figma data into" << document.objects.size() << "objects;" << localWarnings.size() << "warnings";
    return document;
}

VectorDocument fromKiwiBytes(const QByteArray &fileBytes, QStringList *warnings)
{
    const QHash<QString, QByteArray> images = zipImages(fileBytes);
    const QByteArray canvas = canvasBytes(fileBytes);
    FigmaKiwi::Container container;
    try {
        container = FigmaKiwi::decodeContainer(canvas);
    } catch (const FigmaKiwi::KiwiError &error) {
        throw FileError(
            QStringLiteral("This Figma file’s format wasn’t recognized (%1). Try File ▸ Import from Figma Link… instead.").arg(QString::fromUtf8(error.what())));
    }
    const QVariantMap message = container.message.toMap();
    return fromNodeChanges(message.value(QStringLiteral("nodeChanges")).toList(), message.value(QStringLiteral("blobs")).toList(), images, warnings);
}

QString tokenPath()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : given;
    return QDir(config).filePath(QStringLiteral("omastrator/figma.json"));
}
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to import.").arg(QFileInfo(path).fileName()));
    try {
        return fromKiwiBytes(file.readAll(), warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

VectorDocument parse(const QByteArray &data, QStringList *warnings)
{
    return fromKiwiBytes(data, warnings);
}

bool canRead(const QByteArray &data)
{
    if (FigmaKiwi::looksLikeContainer(data))
        return true;
    if (!data.startsWith("PK"))
        return false;
    const ZipReader zip(data);
    return zip.isValid() && zip.entries().contains(QStringLiteral("canvas.fig"));
}

bool canRead(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(12);
    if (FigmaKiwi::looksLikeContainer(head))
        return true;
    if (!head.startsWith("PK") || file.size() > maximumBytes)
        return false;
    file.seek(0);
    return canRead(file.readAll());
}

bool isFigmaClipboardHtml(const QByteArray &html)
{
    return html.contains("(figma)");
}

namespace {
QByteArray htmlComment(const QByteArray &html, const char *tag)
{
    const QByteArray openTag = QByteArray("(") + tag + ")";
    const QByteArray closeTag = QByteArray("(/") + tag + ")";
    const qsizetype start = html.indexOf(openTag);
    if (start < 0)
        return {};
    const qsizetype contentStart = start + openTag.size();
    const qsizetype end = html.indexOf(closeTag, contentStart);
    if (end < 0)
        return {};
    return html.mid(contentStart, end - contentStart);
}
}

VectorDocument parseClipboardHtml(const QByteArray &html, QStringList *warnings)
{
    const QByteArray encoded = htmlComment(html, "figma");
    if (encoded.isEmpty())
        throw FileError(QStringLiteral("This isn’t Figma data."));
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    QStringList localWarnings;
    VectorDocument document = fromKiwiBytes(decoded, &localWarnings);
    // A paste never carries image bytes by hash (FIGMA.md): note it once, plainly,
    // rather than relying on soleImageFill's per-image wording alone.
    if (std::any_of(localWarnings.cbegin(), localWarnings.cend(), [](const QString &w) { return w.contains(QStringLiteral("image")); }))
        localWarnings << QStringLiteral("Pasted images aren’t included; use File ▸ Import from Figma Link… to bring them in.");
    if (warnings)
        *warnings = localWarnings;
    return document;
}

std::optional<LinkTarget> parseLink(const QString &url)
{
    static const QRegularExpression pattern(QStringLiteral("figma\\.com/(?:design|file|proto)/([a-zA-Z0-9]+)/"));
    const QRegularExpressionMatch match = pattern.match(url);
    if (!match.hasMatch())
        return std::nullopt;
    LinkTarget target;
    target.fileKey = match.captured(1);
    static const QRegularExpression nodeParam(QStringLiteral("[?&]node-id=([^&]+)"));
    const QRegularExpressionMatch nodeMatch = nodeParam.match(url);
    if (nodeMatch.hasMatch())
        target.nodeID = QUrl::fromPercentEncoding(nodeMatch.captured(1).toUtf8()).replace(QLatin1Char('-'), QLatin1Char(':'));
    return target;
}

namespace Token {
std::optional<QString> load()
{
    QFile file(tokenPath());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QString token = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("token")).toString();
    return token.isEmpty() ? std::nullopt : std::optional(token);
}

void save(const QString &token)
{
    const QString path = tokenPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    QJsonObject object;
    object.insert(QStringLiteral("token"), token);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcIO) << "couldn't save the Figma token:" << file.errorString();
        return;
    }
    // A personal access token: only this user reads it.
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
}

void forget()
{
    QFile::remove(tokenPath());
}

QString settingsPageURL()
{
    return QStringLiteral("https://www.figma.com/developers/api#access-tokens");
}
}
}
