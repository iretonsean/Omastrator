#pragma once
#include "IO/PdfObject.h"
#include <QByteArray>

// The token grammar PDF's file structure and content streams share: numbers,
// literal and hex strings, names, array/dictionary delimiters, and bare
// keywords (true/false/null, obj/R/stream, or a content operator like `re`).
// Streams and "N G obj" wrapping are handled by PdfDocument, one level up.
namespace Pdf {

enum class TokenKind { end, number, string, name, arrayStart, arrayEnd, dictStart, dictEnd, keyword };

struct Token {
    TokenKind kind = TokenKind::end;
    QByteArray bytes; // string contents, name (unescaped), or keyword text
    double number = 0;
    bool isReal = false; // the source text had a '.' (Object::real vs Object::integer)
};

class Lexer {
public:
    explicit Lexer(const QByteArray &data, qsizetype pos = 0) : m_data(data), m_pos(pos) {}

    Token next();
    // Returns the next token without consuming it.
    Token peek();

    qsizetype position() const { return m_pos; }
    void seek(qsizetype pos)
    {
        m_pos = pos;
        m_hasPeeked = false;
    }
    const QByteArray &data() const { return m_data; }
    bool atEnd() const { return m_pos >= m_data.size(); }

    void skipWhitespace();
    // For "BI ... ID <raw bytes> EI": consumes exactly one byte of whitespace
    // after ID, then everything up to (not including) a whitespace-delimited
    // "EI", advancing past it. `expectedLength`, if positive, is tried first
    // as the exact byte count (uncompressed inline image data knows its own
    // size), falling back to the EI scan if that guess doesn't land on one.
    QByteArray captureInlineImageData(qint64 expectedLength = -1);

private:
    QByteArray m_data;
    qsizetype m_pos = 0;
    bool m_hasPeeked = false;
    Token m_peeked;

    Token lex();
    QByteArray lexLiteralString();
    QByteArray lexHexString();
    QByteArray lexName();
    Token lexNumberOrKeyword();
};

// Parses one PDF object (number, string, name, boolean, null, array,
// dictionary or an "N G R" indirect reference) starting at the lexer's
// current position. Never consumes a `stream` body.
Object parseObject(Lexer &lexer);

}
