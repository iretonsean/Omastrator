#pragma once
#include "IO/PdfObject.h"
#include <QByteArray>
#include <QStringList>
#include <functional>

// The stream filters PDF.md's parsing list calls for. FlateDecode and
// LZWDecode decode to plain samples (predictors applied); ASCIIHex, ASCII85
// and RunLength likewise. DCTDecode, JPXDecode and CCITTFaxDecode are left
// undecoded for the image decoder, named in `imageFilter`.
namespace PdfFilters {

// No single stream decodes past this; a Flate or LZW bomb is cut off here
// with a warning instead of exhausting memory.
constexpr qint64 maximumDecodedSize = 128LL * 1024 * 1024;

struct Decoded {
    QByteArray bytes;
    // Empty once every filter named has produced plain samples. Otherwise the
    // filter left for the image decoder (DCTDecode, JPXDecode or
    // CCITTFaxDecode); `bytes` is that filter's still-encoded payload.
    QByteArray imageFilter;
    QStringList warnings;
};

// Applies every filter named in a stream dictionary's /Filter, honouring
// /DecodeParms, to that stream's raw bytes. `resolve` follows indirect
// references (PdfDocument::resolve); pass-through for direct objects.
Decoded decodeStream(const Pdf::Dict &dict, const QByteArray &rawBytes,
                      const std::function<Pdf::Object(const Pdf::Object &)> &resolve);

// Each decoder keeps whatever it decoded before a corrupt, truncated or
// oversized stream gave out, and says so once in `warnings` (when given).
QByteArray inflate(const QByteArray &data, QStringList *warnings = nullptr);
QByteArray decodeLZW(const QByteArray &data, int earlyChange = 1, QStringList *warnings = nullptr);
QByteArray decodeAsciiHex(const QByteArray &data);
QByteArray decodeAscii85(const QByteArray &data);
QByteArray decodeRunLength(const QByteArray &data, QStringList *warnings = nullptr);
// PNG predictors 10-15 and TIFF predictor 2; predictor <= 1 returns `data` unchanged.
QByteArray applyPredictor(const QByteArray &data, int predictor, int colors, int bitsPerComponent, int columns);

}
