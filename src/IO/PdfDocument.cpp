#include "IO/PdfDocument.h"
#include "IO/PdfLexer.h"
#include <QSet>

namespace Pdf {

namespace {
bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}
bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0';
}
}

Object Document::resolve(const Object &value) const
{
    if (!value.isReference())
        return value;
    return object(value.toReference().number);
}

Object Document::object(int number) const
{
    const auto cached = m_cache.constFind(number);
    if (cached != m_cache.constEnd())
        return cached.value();
    m_cache.insert(number, Object::null()); // guards a self-referential file against infinite recursion
    Object result;
    const auto offsetIt = m_offsets.constFind(number);
    if (offsetIt != m_offsets.constEnd()) {
        result = loadObjectAt(offsetIt.value());
    } else {
        const auto compressedIt = m_compressed.constFind(number);
        if (compressedIt != m_compressed.constEnd())
            result = loadCompressedObject(compressedIt.value().first, compressedIt.value().second);
    }
    m_cache.insert(number, result);
    return result;
}

Object Document::catalog() const
{
    return resolve(m_trailer.value(QStringLiteral("Root")));
}

QByteArray Document::readRawStreamBytes(Lexer &lexer, const Dict &dict) const
{
    qsizetype pos = lexer.position();
    if (pos < m_data.size() && m_data[pos] == '\r')
        ++pos;
    if (pos < m_data.size() && m_data[pos] == '\n')
        ++pos;

    qint64 length = -1;
    const Object lengthObject = dict.value(QStringLiteral("Length"));
    if (lengthObject.isNumber())
        length = lengthObject.toInt(-1);
    else if (lengthObject.isReference())
        length = resolve(lengthObject).toInt(-1);

    bool lengthLandsOnEndstream = false;
    if (length >= 0 && pos + length <= m_data.size()) {
        Lexer probe(m_data, pos + length);
        const Token token = probe.next();
        lengthLandsOnEndstream = token.kind == TokenKind::keyword && token.bytes == "endstream";
    }
    if (lengthLandsOnEndstream)
        return m_data.mid(pos, length);

    // A wrong or missing /Length: fall back to scanning for "endstream".
    qsizetype endPos = m_data.indexOf("endstream", pos);
    if (endPos < 0)
        endPos = m_data.size();
    qsizetype trimEnd = endPos;
    if (trimEnd > pos && m_data[trimEnd - 1] == '\n')
        --trimEnd;
    if (trimEnd > pos && m_data[trimEnd - 1] == '\r')
        --trimEnd;
    return m_data.mid(pos, trimEnd - pos);
}

Object Document::loadObjectAt(qint64 offset) const
{
    if (offset < 0 || offset >= m_data.size())
        return Object::null();
    Lexer lexer(m_data, offset);
    const Token numberToken = lexer.next();
    const Token generationToken = lexer.next();
    const Token objToken = lexer.next();
    if (numberToken.kind != TokenKind::number || generationToken.kind != TokenKind::number || objToken.kind != TokenKind::keyword
        || objToken.bytes != "obj")
        return Object::null();

    const Object value = parseObject(lexer);
    const Token streamToken = lexer.peek();
    if (streamToken.kind == TokenKind::keyword && streamToken.bytes == "stream" && value.isDictionary()) {
        lexer.next();
        const Dict dict = value.toDict();
        return Object::stream(dict, readRawStreamBytes(lexer, dict));
    }
    return value;
}

PdfFilters::Decoded Document::streamData(const Object &streamObject) const
{
    if (!streamObject.isStream())
        return {};
    const auto resolver = [this](const Object &o) { return resolve(o); };
    return PdfFilters::decodeStream(streamObject.toDict(), streamObject.rawStreamBytes(), resolver);
}

Object Document::loadCompressedObject(int streamNumber, int index) const
{
    const Object streamObject = object(streamNumber);
    if (!streamObject.isStream())
        return Object::null();
    const PdfFilters::Decoded decoded = streamData(streamObject);
    const Dict &dict = streamObject.toDict();
    const int count = resolve(dict.value(QStringLiteral("N"))).toInt(0);
    const int first = resolve(dict.value(QStringLiteral("First"))).toInt(0);

    Lexer headerLexer(decoded.bytes, 0);
    qint64 offset = -1;
    for (int i = 0; i < count; ++i) {
        const Token numberToken = headerLexer.next();
        const Token offsetToken = headerLexer.next();
        if (i == index) {
            offset = qint64(offsetToken.number);
            Q_UNUSED(numberToken);
            break;
        }
    }
    if (offset < 0)
        return Object::null();
    Lexer objectLexer(decoded.bytes, first + offset);
    return parseObject(objectLexer);
}

void Document::mergeTrailer(const Dict &dict)
{
    for (auto it = dict.constBegin(); it != dict.constEnd(); ++it) {
        if (!m_trailer.contains(it.key()))
            m_trailer.insert(it.key(), it.value());
    }
}

void Document::parseClassicXrefTable(Lexer &lexer, QStringList *warnings)
{
    while (true) {
        const Token peeked = lexer.peek();
        if (peeked.kind != TokenKind::number)
            break;
        const Token startToken = lexer.next();
        const Token countToken = lexer.next();
        if (countToken.kind != TokenKind::number) {
            if (warnings)
                *warnings << QStringLiteral("The cross-reference table was malformed.");
            break;
        }
        const int start = int(startToken.number);
        const int count = int(countToken.number);
        for (int i = 0; i < count; ++i) {
            const Token offsetToken = lexer.next();
            const Token generationToken = lexer.next();
            const Token typeToken = lexer.next();
            Q_UNUSED(generationToken);
            if (offsetToken.kind != TokenKind::number)
                return;
            const bool isFree = typeToken.kind == TokenKind::keyword && typeToken.bytes == "f";
            if (!isFree) {
                const int number = start + i;
                if (!m_offsets.contains(number) && !m_compressed.contains(number))
                    m_offsets.insert(number, qint64(offsetToken.number));
            }
        }
    }
}

void Document::parseCrossReferenceStream(const Dict &dict, const QByteArray &rawData, QStringList *warnings)
{
    const auto resolver = [this](const Object &o) { return resolve(o); };
    const PdfFilters::Decoded decoded = PdfFilters::decodeStream(dict, rawData, resolver);
    if (warnings)
        *warnings << decoded.warnings;
    const QByteArray &data = decoded.bytes;

    const Array widths = dict.value(QStringLiteral("W")).toArray();
    if (widths.size() < 3)
        return;
    const int w0 = widths[0].toInt(1), w1 = widths[1].toInt(1), w2 = widths[2].toInt(1);
    const int recordSize = w0 + w1 + w2;
    if (recordSize <= 0)
        return;

    QList<std::pair<int, int>> ranges;
    const Object indexObject = dict.value(QStringLiteral("Index"));
    if (indexObject.isArray()) {
        const Array index = indexObject.toArray();
        for (qsizetype i = 0; i + 1 < index.size(); i += 2)
            ranges.append({index[i].toInt(0), index[i + 1].toInt(0)});
    } else {
        ranges.append({0, dict.value(QStringLiteral("Size")).toInt(0)});
    }

    qint64 pos = 0;
    const auto readField = [&](int width) -> qint64 {
        qint64 value = 0;
        for (int i = 0; i < width; ++i) {
            if (pos >= data.size())
                return value;
            value = (value << 8) | uchar(data[pos++]);
        }
        return value;
    };

    for (const auto &range : ranges) {
        for (int i = 0; i < range.second; ++i) {
            if (pos + recordSize > data.size())
                break;
            const qint64 type = w0 == 0 ? 1 : readField(w0);
            const qint64 field2 = readField(w1);
            const qint64 field3 = readField(w2);
            const int number = range.first + i;
            if (type == 1) {
                if (!m_offsets.contains(number) && !m_compressed.contains(number))
                    m_offsets.insert(number, field2);
            } else if (type == 2) {
                if (!m_offsets.contains(number) && !m_compressed.contains(number))
                    m_compressed.insert(number, {int(field2), int(field3)});
            }
        }
    }
}

bool Document::parseXrefSection(qint64 offset, QStringList *warnings, qint64 &nextOffset)
{
    nextOffset = -1;
    Lexer lexer(m_data, offset);
    const Token first = lexer.peek();
    if (first.kind == TokenKind::keyword && first.bytes == "xref") {
        lexer.next();
        parseClassicXrefTable(lexer, warnings);
        const Token trailerKeyword = lexer.next();
        Dict trailerDict;
        if (trailerKeyword.kind == TokenKind::keyword && trailerKeyword.bytes == "trailer") {
            const Object trailerObject = parseObject(lexer);
            if (trailerObject.isDictionary())
                trailerDict = trailerObject.toDict();
        }
        mergeTrailer(trailerDict);
        if (trailerDict.contains(QStringLiteral("XRefStm"))) {
            const qint64 hybridOffset = trailerDict.value(QStringLiteral("XRefStm")).toInt(-1);
            if (hybridOffset >= 0 && hybridOffset < m_data.size()) {
                qint64 ignored = -1;
                parseXrefSection(hybridOffset, warnings, ignored);
            }
        }
        if (trailerDict.contains(QStringLiteral("Prev")))
            nextOffset = trailerDict.value(QStringLiteral("Prev")).toInt(-1);
        return true;
    }

    const Token numberToken = lexer.next();
    const Token generationToken = lexer.next();
    const Token objToken = lexer.next();
    if (numberToken.kind != TokenKind::number || generationToken.kind != TokenKind::number || objToken.kind != TokenKind::keyword
        || objToken.bytes != "obj")
        return false;
    const Object dictObject = parseObject(lexer);
    if (!dictObject.isDictionary())
        return false;
    const Dict dict = dictObject.toDict();
    const Token streamToken = lexer.peek();
    QByteArray raw;
    if (streamToken.kind == TokenKind::keyword && streamToken.bytes == "stream") {
        lexer.next();
        raw = readRawStreamBytes(lexer, dict);
    }
    parseCrossReferenceStream(dict, raw, warnings);
    mergeTrailer(dict);
    if (dict.contains(QStringLiteral("Prev")))
        nextOffset = dict.value(QStringLiteral("Prev")).toInt(-1);
    return true;
}

bool Document::parseXrefChain(qint64 startOffset, QStringList *warnings)
{
    QSet<qint64> visited;
    qint64 offset = startOffset;
    bool any = false;
    while (offset >= 0 && offset < m_data.size() && !visited.contains(offset)) {
        visited.insert(offset);
        qint64 nextOffset = -1;
        if (!parseXrefSection(offset, warnings, nextOffset))
            break;
        any = true;
        offset = nextOffset;
    }
    return any;
}

void Document::repairByScanning(QStringList *warnings)
{
    if (warnings)
        *warnings << QStringLiteral("The file's cross-reference table was damaged, so it was rebuilt by scanning the file.");
    m_offsets.clear();
    m_compressed.clear();
    m_cache.clear();

    qsizetype pos = 0;
    while (true) {
        const qsizetype objPos = m_data.indexOf("obj", pos);
        if (objPos < 0)
            break;
        pos = objPos + 3;
        qsizetype p = objPos;
        while (p > 0 && isSpace(m_data[p - 1]))
            --p;
        const qsizetype genEnd = p;
        while (p > 0 && isDigit(m_data[p - 1]))
            --p;
        const qsizetype genStart = p;
        while (p > 0 && isSpace(m_data[p - 1]))
            --p;
        const qsizetype numEnd = p;
        while (p > 0 && isDigit(m_data[p - 1]))
            --p;
        const qsizetype numStart = p;
        if (numStart < numEnd && genStart < genEnd) {
            const int number = m_data.mid(numStart, numEnd - numStart).toInt();
            m_offsets.insert(number, numStart); // a later occurrence (a newer update) overwrites an earlier one
        }
    }

    const qsizetype trailerPos = m_data.lastIndexOf("trailer");
    Dict trailerDict;
    if (trailerPos >= 0) {
        Lexer lexer(m_data, trailerPos + 7);
        const Object trailerObject = parseObject(lexer);
        if (trailerObject.isDictionary())
            trailerDict = trailerObject.toDict();
    }

    // Object streams the flat scan can't see into directly.
    for (auto it = m_offsets.constBegin(); it != m_offsets.constEnd(); ++it) {
        const Object candidate = loadObjectAt(it.value());
        if (candidate.isStream() && resolve(candidate.at(QStringLiteral("Type"))).isName(QLatin1StringView("ObjStm"))) {
            const PdfFilters::Decoded decoded = streamData(candidate);
            const int count = resolve(candidate.at(QStringLiteral("N"))).toInt(0);
            Lexer headerLexer(decoded.bytes, 0);
            for (int i = 0; i < count; ++i) {
                const Token numberToken = headerLexer.next();
                const Token offsetToken = headerLexer.next();
                Q_UNUSED(offsetToken);
                const int number = int(numberToken.number);
                if (!m_offsets.contains(number))
                    m_compressed.insert(number, {it.key(), i});
            }
        }
    }

    if (!trailerDict.contains(QStringLiteral("Root"))) {
        for (auto it = m_offsets.constBegin(); it != m_offsets.constEnd(); ++it) {
            const Object candidate = loadObjectAt(it.value());
            if (candidate.isDictionary() && resolve(candidate.at(QStringLiteral("Type"))).isName(QLatin1StringView("Catalog"))) {
                trailerDict.insert(QStringLiteral("Root"), Object::reference(it.key(), 0));
                break;
            }
        }
    }
    mergeTrailer(trailerDict);
}

std::unique_ptr<Document> Document::load(const QByteArray &data, QStringList *warnings)
{
    QStringList localWarnings;
    auto document = std::make_unique<Document>();
    document->m_data = data;

    qint64 startOffset = -1;
    const qsizetype startxrefPos = data.lastIndexOf("startxref");
    if (startxrefPos >= 0) {
        Lexer lexer(data, startxrefPos + 9);
        const Token token = lexer.next();
        if (token.kind == TokenKind::number)
            startOffset = qint64(token.number);
    }

    bool ok = false;
    if (startOffset >= 0 && startOffset < data.size())
        ok = document->parseXrefChain(startOffset, &localWarnings);
    if (!ok || document->m_trailer.isEmpty() || !document->m_trailer.contains(QStringLiteral("Root")))
        document->repairByScanning(&localWarnings);

    if (document->m_trailer.contains(QStringLiteral("Encrypt")))
        throw FileError(QStringLiteral("This PDF is password-protected. Remove the password (or export it again without one) and try again."));

    if (!document->catalog().isDictionary())
        throw FileError(QStringLiteral("This PDF has no readable pages."));

    if (warnings)
        *warnings = localWarnings;
    return document;
}

namespace {
void collectPages(const Document &document, const Object &nodeReferenceOrValue, Dict inherited, QList<Dict> &out, QSet<int> &visitedKids)
{
    const Object node = document.resolve(nodeReferenceOrValue);
    if (!node.isDictionary())
        return;
    const Dict &dict = node.toDict();
    for (const QString &key :
         {QStringLiteral("Resources"), QStringLiteral("MediaBox"), QStringLiteral("CropBox"), QStringLiteral("Rotate")}) {
        if (dict.contains(key))
            inherited.insert(key, dict.value(key));
    }
    const QByteArray type = document.resolve(dict.value(QStringLiteral("Type"))).toNameValue();
    const bool isIntermediate = type == "Pages" || (dict.contains(QStringLiteral("Kids")) && type != "Page");
    if (isIntermediate) {
        const Array kids = document.resolve(dict.value(QStringLiteral("Kids"))).toArray();
        for (const Object &kid : kids) {
            if (kid.isReference()) {
                const int number = kid.toReference().number;
                if (visitedKids.contains(number))
                    continue;
                visitedKids.insert(number);
            }
            collectPages(document, kid, inherited, out, visitedKids);
        }
    } else {
        Dict page = dict;
        for (auto it = inherited.constBegin(); it != inherited.constEnd(); ++it) {
            if (!page.contains(it.key()))
                page.insert(it.key(), it.value());
        }
        out.append(page);
    }
}
}

QList<Dict> Document::pages() const
{
    QList<Dict> result;
    const Object root = catalog();
    if (!root.isDictionary())
        return result;
    QSet<int> visited;
    collectPages(*this, root.at(QStringLiteral("Pages")), Dict(), result, visited);
    return result;
}

}
