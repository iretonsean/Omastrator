#include "IO/FileError.h"
#include "IO/ImageImporterParts.h"
#include <QSet>
#include <memory>
#ifdef OMASTRATOR_HAVE_LIBHEIF
#include <libheif/heif.h>
#endif

namespace ImageImport {

bool isHeicOrAvif(const QByteArray &head)
{
    // An ISO base media file: a 4-byte box size, "ftyp", then a 4-byte major brand.
    if (head.size() < 12 || head.mid(4, 4) != "ftyp")
        return false;
    static const QSet<QByteArray> brands{"heic", "heix", "heim", "heis", "hevc", "hevx", "hevm",
                                          "hevs", "mif1", "msf1", "avif", "avis"};
    return brands.contains(head.mid(8, 4));
}

#ifdef OMASTRATOR_HAVE_LIBHEIF
QImage readHeicOrAvif(const QString &path, const QByteArray &data)
{
    Q_UNUSED(path)
    struct ContextDeleter {
        void operator()(heif_context *context) const { heif_context_free(context); }
    };
    const std::unique_ptr<heif_context, ContextDeleter> context(heif_context_alloc());
    heif_error error = heif_context_read_from_memory_without_copy(context.get(), data.constData(), size_t(data.size()), nullptr);
    if (error.code != heif_error_Ok)
        throw FileError(QStringLiteral("This HEIC/AVIF file could not be read: %1").arg(QString::fromUtf8(error.message)));

    heif_image_handle *handleRaw = nullptr;
    error = heif_context_get_primary_image_handle(context.get(), &handleRaw);
    if (error.code != heif_error_Ok || !handleRaw)
        throw FileError(QStringLiteral("This HEIC/AVIF file could not be read: %1").arg(QString::fromUtf8(error.message)));
    struct HandleDeleter {
        void operator()(heif_image_handle *handle) const { heif_image_handle_release(handle); }
    };
    const std::unique_ptr<heif_image_handle, HandleDeleter> handle(handleRaw);

    heif_image *imageRaw = nullptr;
    error = heif_decode_image(handle.get(), &imageRaw, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    if (error.code != heif_error_Ok || !imageRaw)
        throw FileError(QStringLiteral("This HEIC/AVIF file could not be decoded: %1").arg(QString::fromUtf8(error.message)));
    struct ImageDeleter {
        void operator()(heif_image *image) const { heif_image_release(image); }
    };
    const std::unique_ptr<heif_image, ImageDeleter> decoded(imageRaw);

    int stride = 0;
    const uint8_t *plane = heif_image_get_plane_readonly(decoded.get(), heif_channel_interleaved, &stride);
    const int width = heif_image_get_width(decoded.get(), heif_channel_interleaved);
    const int height = heif_image_get_height(decoded.get(), heif_channel_interleaved);
    if (!plane || width <= 0 || height <= 0)
        throw FileError(QStringLiteral("This HEIC/AVIF file has no image to place."));
    // Copied out: `plane` is only valid while `decoded` lives.
    return QImage(plane, width, height, stride, QImage::Format_RGBA8888).copy();
}
#else
QImage readHeicOrAvif(const QString &, const QByteArray &)
{
    throw FileError(QStringLiteral("HEIC and AVIF need libheif: install it with “omarchy pkg add libheif”."));
}
#endif

}
