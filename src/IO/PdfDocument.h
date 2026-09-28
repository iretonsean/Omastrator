#pragma once
#include "IO/FileError.h"
#include "IO/PdfFilters.h"
#include "IO/PdfObject.h"
#include <QByteArray>
#include <QStringList>
#include <memory>

// A PDF file's object table: the classic xref table and cross-reference
// streams, object streams, incremental updates (following /Prev, newest
// wins), and a scan-for-"obj" repair when the xref itself can't be read.
// Throws FileError for an encrypted file.
namespace Pdf {

class Document {
public:
    static std::unique_ptr<Document> load(const QByteArray &data, QStringList *warnings);

    // Follows an indirect reference; anything else passes straight through.
    Object resolve(const Object &object) const;
    Object object(int number) const;
    const Dict &trailer() const { return m_trailer; }
    Object catalog() const;
    // Every page's dictionary, in document order, with inherited
    // /Resources, /MediaBox, /CropBox and /Rotate already folded in.
    QList<Dict> pages() const;
    // A stream object's bytes, filtered except for an image codec filter
    // (DCTDecode, JPXDecode or CCITTFaxDecode) left for the image decoder.
    PdfFilters::Decoded streamData(const Object &streamObject) const;

private:
    QByteArray m_data;
    Dict m_trailer;
    QHash<int, qint64> m_offsets;
    QHash<int, std::pair<int, int>> m_compressed; // object number -> (object-stream number, index)
    mutable QHash<int, Object> m_cache;

    bool parseXrefChain(qint64 startOffset, QStringList *warnings);
    bool parseXrefSection(qint64 offset, QStringList *warnings, qint64 &nextOffset);
    void parseClassicXrefTable(class Lexer &lexer, QStringList *warnings);
    void parseCrossReferenceStream(const Dict &dict, const QByteArray &rawData, QStringList *warnings);
    void repairByScanning(QStringList *warnings);
    void mergeTrailer(const Dict &dict);
    Object loadObjectAt(qint64 offset) const;
    Object loadCompressedObject(int streamNumber, int index) const;
    QByteArray readRawStreamBytes(class Lexer &lexer, const Dict &dict) const;
};

}
