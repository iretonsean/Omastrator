#include "IO/ProjectStore.h"
#include "Document/DocumentCodec.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace {
// Placed images make big files, but a document past this is not ours.
constexpr qint64 maximumBytes = qint64(2) << 30;

QString displayName(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}
}

namespace ProjectStore {
VectorDocument read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot open" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(displayName(path), file.errorString()));
    }
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to be an Omastrator document.").arg(displayName(path)));
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        qCWarning(lcIO).noquote() << "bad JSON in" << path + ":" << parseError.errorString();
        throw FileError(QStringLiteral("“%1” is not an Omastrator document, or it is damaged.").arg(displayName(path)));
    }
    try {
        VectorDocument document = DocumentCodec::decode(json.object());
        qCInfo(lcIO).noquote() << "read" << path << document.objects.size() << "objects";
        return document;
    } catch (const CodecError &error) {
        qCWarning(lcIO).noquote() << "cannot decode" << path + ":" << error.what();
        throw FileError(QStringLiteral("“%1” could not be read: %2.").arg(displayName(path), QString::fromUtf8(error.what())));
    }
}

void write(const VectorDocument &document, const QString &path)
{
    const QByteArray bytes = QJsonDocument(DocumentCodec::encode(document)).toJson(QJsonDocument::Compact);
    // QSaveFile writes beside the target and renames, so a failed save leaves the old file whole.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcIO).noquote() << "cannot write" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be saved: %2").arg(displayName(path), file.errorString()));
    }
    qCInfo(lcIO).noquote() << "wrote" << bytes.size() << "bytes to" << path;
}
}
