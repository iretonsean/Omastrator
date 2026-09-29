#include "IO/PdfFilters.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

using Pdf::Dict;
using Pdf::Object;

namespace PdfFilters {

namespace {
QString tooLargeWarning()
{
    return QStringLiteral("A stream was too large to decode in full; the rest was left out.");
}
QString damagedWarning()
{
    return QStringLiteral("A compressed stream was damaged or cut short; what could be read was kept.");
}
}

QByteArray inflate(const QByteArray &data, QStringList *warnings)
{
    if (data.isEmpty())
        return {};
#ifdef OMASTRATOR_HAVE_ZLIB
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK)
        return {};
    QByteArray out;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(std::min<qsizetype>(data.size(), std::numeric_limits<uInt>::max()));
    constexpr qsizetype chunk = 256 * 1024;
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        if (out.size() >= maximumDecodedSize) {
            if (warnings)
                *warnings << tooLargeWarning();
            break;
        }
        const qsizetype used = out.size();
        out.resize(used + chunk);
        stream.next_out = reinterpret_cast<Bytef *>(out.data() + used);
        stream.avail_out = uInt(chunk);
        result = inflate(&stream, Z_NO_FLUSH);
        out.resize(used + chunk - qsizetype(stream.avail_out));
        if (result == Z_STREAM_END)
            break;
        // Z_BUF_ERROR with no input left is a truncated stream; a data error
        // (bad checksum, corrupt block) keeps the good prefix too.
        if (result != Z_OK && !(result == Z_BUF_ERROR && stream.avail_in > 0)) {
            if (warnings)
                *warnings << damagedWarning();
            break;
        }
        if (stream.avail_in == 0 && stream.avail_out != 0) {
            if (warnings)
                *warnings << damagedWarning();
            break;
        }
    }
    inflateEnd(&stream);
    if (out.size() > maximumDecodedSize)
        out.truncate(maximumDecodedSize);
    return out;
#else
    if (warnings)
        *warnings << QStringLiteral("A compressed stream was left out because this build has no zlib.");
    return {};
#endif
}

QByteArray decodeLZW(const QByteArray &data, int earlyChange, QStringList *warnings)
{
    QByteArray out;
    std::vector<QByteArray> dictionary(4096);
    for (int i = 0; i < 256; ++i)
        dictionary[i] = QByteArray(1, char(i));
    int codeLength = 9;
    int nextCode = 258;
    int previousCode = -1;
    QByteArray previousSequence;

    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
    const qsizetype size = data.size();
    qsizetype bytePos = 0;
    quint32 bitBuffer = 0;
    int bitCount = 0;
    const auto readCode = [&]() -> int {
        while (bitCount < codeLength) {
            if (bytePos >= size)
                return -1;
            bitBuffer = (bitBuffer << 8) | bytes[bytePos++];
            bitCount += 8;
        }
        const int code = int((bitBuffer >> (bitCount - codeLength)) & ((1u << codeLength) - 1));
        bitCount -= codeLength;
        return code;
    };

    while (true) {
        const int code = readCode();
        if (code < 0 || code == 257)
            break;
        if (code == 256) {
            for (int i = 256; i < 4096; ++i)
                dictionary[i].clear();
            codeLength = 9;
            nextCode = 258;
            previousCode = -1;
            continue;
        }
        QByteArray sequence;
        if (code < nextCode && !(code >= 256 && dictionary[code].isEmpty()))
            sequence = dictionary[code];
        else if (code == nextCode && previousCode >= 0)
            sequence = previousSequence + previousSequence.left(1);
        else
            break; // a corrupt or truncated stream
        out += sequence;
        if (out.size() >= maximumDecodedSize) {
            if (warnings)
                *warnings << tooLargeWarning();
            break;
        }
        if (previousCode >= 0 && nextCode < 4096) {
            dictionary[nextCode] = previousSequence + sequence.left(1);
            ++nextCode;
            if (nextCode + earlyChange - 1 >= (1 << codeLength) && codeLength < 12)
                ++codeLength;
        }
        previousCode = code;
        previousSequence = sequence;
    }
    return out;
}

QByteArray decodeAsciiHex(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() / 2);
    int high = -1;
    for (const char c : data) {
        if (c == '>')
            break;
        int value;
        if (c >= '0' && c <= '9')
            value = c - '0';
        else if (c >= 'A' && c <= 'F')
            value = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            value = c - 'a' + 10;
        else
            continue; // whitespace
        if (high < 0)
            high = value;
        else {
            out.append(char((high << 4) | value));
            high = -1;
        }
    }
    if (high >= 0)
        out.append(char(high << 4));
    return out;
}

QByteArray decodeAscii85(const QByteArray &data)
{
    QByteArray out;
    quint32 tuple = 0;
    int count = 0;
    qsizetype i = data.startsWith("<~") ? 2 : 0;
    for (; i < data.size(); ++i) {
        const char c = data[i];
        if (c == '~')
            break;
        if (c == 'z' && count == 0) {
            out.append(4, char(0));
            continue;
        }
        if (c < '!' || c > 'u')
            continue; // whitespace
        tuple = tuple * 85 + quint32(c - '!');
        if (++count == 5) {
            out.append(char((tuple >> 24) & 0xff));
            out.append(char((tuple >> 16) & 0xff));
            out.append(char((tuple >> 8) & 0xff));
            out.append(char(tuple & 0xff));
            tuple = 0;
            count = 0;
        }
    }
    if (count > 0) {
        for (int j = count; j < 5; ++j)
            tuple = tuple * 85 + 84;
        for (int j = 0; j < count - 1; ++j)
            out.append(char((tuple >> (24 - j * 8)) & 0xff));
    }
    return out;
}

QByteArray decodeRunLength(const QByteArray &data, QStringList *warnings)
{
    QByteArray out;
    qsizetype i = 0;
    while (i < data.size()) {
        if (out.size() >= maximumDecodedSize) {
            if (warnings)
                *warnings << tooLargeWarning();
            break;
        }
        const uchar length = uchar(data[i++]);
        if (length == 128)
            break; // EOD
        if (length < 128) {
            const int count = length + 1;
            out.append(data.mid(i, count));
            i += count;
        } else {
            if (i >= data.size())
                break;
            out.append(257 - length, data[i]);
            i += 1;
        }
    }
    return out;
}

QByteArray applyPredictor(const QByteArray &data, int predictor, int colors, int bitsPerComponent, int columns)
{
    if (predictor <= 1)
        return data;
    // 64-bit so hostile /Colors, /BitsPerComponent and /Columns can't overflow into a small row.
    colors = std::clamp(colors, 1, 256);
    bitsPerComponent = std::clamp(bitsPerComponent, 1, 32);
    columns = std::clamp(columns, 1, 1 << 28);
    const int bytesPerPixel = std::max(1, (colors * bitsPerComponent + 7) / 8);
    const qint64 rowBytes64 = (qint64(colors) * bitsPerComponent * columns + 7) / 8;
    // A row that can't fit in the data yields nothing; don't allocate a previous-row buffer for it.
    if (rowBytes64 + 1 > data.size())
        return predictor == 2 ? data : QByteArray();
    const int rowBytes = int(rowBytes64);

    if (predictor == 2) {
        // TIFF predictor 2 (horizontal differencing); only meaningful byte-aligned.
        QByteArray out = data;
        if (bitsPerComponent == 8) {
            for (qsizetype row = 0; row + rowBytes <= out.size(); row += rowBytes) {
                for (int i = colors; i < rowBytes; ++i)
                    out[row + i] = char(uchar(out[row + i]) + uchar(out[row + i - colors]));
            }
        }
        return out;
    }

    // PNG predictors (10-15): each row is prefixed with a filter-type byte.
    QByteArray out;
    QByteArray previous(rowBytes, char(0));
    qsizetype pos = 0;
    while (pos + 1 + rowBytes <= data.size()) {
        const uchar filter = uchar(data[pos]);
        QByteArray row = data.mid(pos + 1, rowBytes);
        pos += 1 + rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            const int a = i >= bytesPerPixel ? uchar(row[i - bytesPerPixel]) : 0;
            const int b = uchar(previous[i]);
            const int c = i >= bytesPerPixel ? uchar(previous[i - bytesPerPixel]) : 0;
            int value = uchar(row[i]);
            switch (filter) {
            case 1:
                value += a;
                break;
            case 2:
                value += b;
                break;
            case 3:
                value += (a + b) / 2;
                break;
            case 4: {
                const int p = a + b - c;
                const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                value += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default:
                break;
            }
            row[i] = char(uchar(value));
        }
        out += row;
        previous = row;
    }
    return out;
}

namespace {
QByteArray predictorPass(const QByteArray &bytes, const Dict &parms, const std::function<Object(const Object &)> &resolve)
{
    const int predictor = resolve(parms.value(QStringLiteral("Predictor"))).toInt(1);
    if (predictor <= 1)
        return bytes;
    const int colors = resolve(parms.value(QStringLiteral("Colors"))).toInt(1);
    const int bpc = resolve(parms.value(QStringLiteral("BitsPerComponent"))).toInt(8);
    const int columns = resolve(parms.value(QStringLiteral("Columns"))).toInt(1);
    return applyPredictor(bytes, predictor, colors, bpc, columns);
}
}

Decoded decodeStream(const Dict &dict, const QByteArray &rawBytes, const std::function<Object(const Object &)> &resolve)
{
    Decoded result;
    result.bytes = rawBytes;

    const Object filterObject = resolve(dict.value(QStringLiteral("Filter")));
    const Object parmsObject = resolve(dict.value(QStringLiteral("DecodeParms")));
    QList<Object> filters;
    QList<Object> parms;
    if (filterObject.isName()) {
        filters << filterObject;
        parms << parmsObject;
    } else if (filterObject.isArray()) {
        filters = filterObject.toArray();
        if (parmsObject.isArray())
            parms = parmsObject.toArray();
    }

    for (int i = 0; i < filters.size(); ++i) {
        const QByteArray name = resolve(filters[i]).toNameValue();
        const Object parmObject = i < parms.size() ? resolve(parms[i]) : Object();
        const Dict parmDict = parmObject.isDictionary() ? parmObject.toDict() : Dict();

        if (name == "FlateDecode" || name == "Fl") {
            result.bytes = inflate(result.bytes, &result.warnings);
            result.bytes = predictorPass(result.bytes, parmDict, resolve);
        } else if (name == "LZWDecode" || name == "LZW") {
            const int early = resolve(parmDict.value(QStringLiteral("EarlyChange"))).toInt(1);
            result.bytes = decodeLZW(result.bytes, early, &result.warnings);
            result.bytes = predictorPass(result.bytes, parmDict, resolve);
        } else if (name == "ASCIIHexDecode" || name == "AHx") {
            result.bytes = decodeAsciiHex(result.bytes);
        } else if (name == "ASCII85Decode" || name == "A85") {
            result.bytes = decodeAscii85(result.bytes);
        } else if (name == "RunLengthDecode" || name == "RL") {
            result.bytes = decodeRunLength(result.bytes, &result.warnings);
        } else if (name == "DCTDecode" || name == "DCT" || name == "JPXDecode" || name == "CCITTFaxDecode" || name == "CCF") {
            result.imageFilter = name;
            return result; // left encoded for the image decoder
        } else if (name == "Crypt") {
            // Encrypted files are refused before streams are ever decoded.
        } else if (!name.isEmpty()) {
            result.warnings << QStringLiteral("An unsupported filter (%1) was left out.").arg(QString::fromLatin1(name));
        }
    }
    return result;
}

}
