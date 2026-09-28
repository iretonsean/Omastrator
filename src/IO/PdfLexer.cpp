#include "IO/PdfLexer.h"

namespace Pdf {

namespace {
bool isWhitespaceChar(char c)
{
    return c == '\0' || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}
bool isDelimiterChar(char c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}
bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return c - 'a' + 10;
}
}

void Lexer::skipWhitespace()
{
    while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];
        if (c == '%') {
            while (m_pos < m_data.size() && m_data[m_pos] != '\n' && m_data[m_pos] != '\r')
                ++m_pos;
            continue;
        }
        if (isWhitespaceChar(c)) {
            ++m_pos;
            continue;
        }
        break;
    }
}

QByteArray Lexer::lexLiteralString()
{
    QByteArray out;
    int depth = 1;
    while (m_pos < m_data.size()) {
        const char c = m_data[m_pos++];
        if (c == '\\') {
            if (m_pos >= m_data.size())
                break;
            const char e = m_data[m_pos++];
            switch (e) {
            case 'n':
                out.append('\n');
                break;
            case 'r':
                out.append('\r');
                break;
            case 't':
                out.append('\t');
                break;
            case 'b':
                out.append('\b');
                break;
            case 'f':
                out.append('\f');
                break;
            case '(':
            case ')':
            case '\\':
                out.append(e);
                break;
            case '\r':
                if (m_pos < m_data.size() && m_data[m_pos] == '\n')
                    ++m_pos;
                break; // a backslash-newline is a line continuation, not a character
            case '\n':
                break;
            default:
                if (e >= '0' && e <= '7') {
                    int value = e - '0';
                    for (int i = 0; i < 2 && m_pos < m_data.size() && m_data[m_pos] >= '0' && m_data[m_pos] <= '7'; ++i)
                        value = value * 8 + (m_data[m_pos++] - '0');
                    out.append(char(value & 0xff));
                } else {
                    out.append(e);
                }
            }
            continue;
        }
        if (c == '(') {
            ++depth;
            out.append(c);
            continue;
        }
        if (c == ')') {
            if (--depth == 0)
                break;
            out.append(c);
            continue;
        }
        out.append(c);
    }
    return out;
}

QByteArray Lexer::lexHexString()
{
    QByteArray out;
    int high = -1;
    while (m_pos < m_data.size()) {
        const char c = m_data[m_pos++];
        if (c == '>')
            break;
        if (!isHexDigit(c))
            continue;
        if (high < 0)
            high = hexValue(c);
        else {
            out.append(char((high << 4) | hexValue(c)));
            high = -1;
        }
    }
    if (high >= 0)
        out.append(char(high << 4));
    return out;
}

QByteArray Lexer::lexName()
{
    QByteArray out;
    while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];
        if (isDelimiterChar(c) || isWhitespaceChar(c))
            break;
        ++m_pos;
        if (c == '#' && m_pos + 1 < m_data.size() && isHexDigit(m_data[m_pos]) && isHexDigit(m_data[m_pos + 1])) {
            out.append(char((hexValue(m_data[m_pos]) << 4) | hexValue(m_data[m_pos + 1])));
            m_pos += 2;
        } else {
            out.append(c);
        }
    }
    return out;
}

Token Lexer::lexNumberOrKeyword()
{
    const qsizetype start = m_pos;
    while (m_pos < m_data.size() && !isDelimiterChar(m_data[m_pos]) && !isWhitespaceChar(m_data[m_pos]))
        ++m_pos;
    const QByteArray text = m_data.mid(start, m_pos - start);

    bool looksNumeric = true, sawDigit = false, sawDot = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '+' || c == '-') {
            if (i != 0) {
                looksNumeric = false;
                break;
            }
        } else if (c == '.') {
            sawDot = true;
        } else if (c >= '0' && c <= '9') {
            sawDigit = true;
        } else {
            looksNumeric = false;
            break;
        }
    }
    if (looksNumeric && sawDigit) {
        Token token;
        token.kind = TokenKind::number;
        token.number = text.toDouble();
        token.isReal = sawDot;
        return token;
    }
    Token token;
    token.kind = TokenKind::keyword;
    token.bytes = text;
    return token;
}

Token Lexer::lex()
{
    skipWhitespace();
    if (m_pos >= m_data.size())
        return {};
    const char c = m_data[m_pos];
    if (c == '(') {
        ++m_pos;
        return {TokenKind::string, lexLiteralString()};
    }
    if (c == '<') {
        if (m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '<') {
            m_pos += 2;
            return {TokenKind::dictStart, {}};
        }
        ++m_pos;
        return {TokenKind::string, lexHexString()};
    }
    if (c == '>') {
        if (m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '>') {
            m_pos += 2;
            return {TokenKind::dictEnd, {}};
        }
        ++m_pos; // a stray '>' outside a hex string; skip and keep going
        return lex();
    }
    if (c == '[') {
        ++m_pos;
        return {TokenKind::arrayStart, {}};
    }
    if (c == ']') {
        ++m_pos;
        return {TokenKind::arrayEnd, {}};
    }
    if (c == '/') {
        ++m_pos;
        return {TokenKind::name, lexName()};
    }
    if (c == '{' || c == '}') {
        ++m_pos;
        return {TokenKind::keyword, QByteArray(1, c)};
    }
    return lexNumberOrKeyword();
}

Token Lexer::next()
{
    if (m_hasPeeked) {
        m_hasPeeked = false;
        return m_peeked;
    }
    return lex();
}

Token Lexer::peek()
{
    if (!m_hasPeeked) {
        m_peeked = lex();
        m_hasPeeked = true;
    }
    return m_peeked;
}

QByteArray Lexer::captureInlineImageData(qint64 expectedLength)
{
    m_hasPeeked = false;
    if (m_pos < m_data.size() && isWhitespaceChar(m_data[m_pos]))
        ++m_pos; // the one required separator byte after ID
    const qsizetype start = m_pos;

    if (expectedLength > 0 && start + expectedLength <= m_data.size()) {
        qsizetype probe = start + expectedLength;
        while (probe < m_data.size() && isWhitespaceChar(m_data[probe]))
            ++probe;
        if (probe + 1 < m_data.size() && m_data[probe] == 'E' && m_data[probe + 1] == 'I') {
            const QByteArray result = m_data.mid(start, expectedLength);
            m_pos = probe + 2;
            return result;
        }
    }

    qsizetype pos = start;
    while (pos + 1 < m_data.size()) {
        const bool boundaryBefore = pos == start || isWhitespaceChar(m_data[pos - 1]);
        const bool boundaryAfter = pos + 2 >= m_data.size() || isWhitespaceChar(m_data[pos + 2]) || isDelimiterChar(m_data[pos + 2]);
        if (m_data[pos] == 'E' && m_data[pos + 1] == 'I' && boundaryBefore && boundaryAfter) {
            const qsizetype dataEnd = pos > start ? pos - 1 : pos; // drop the separating whitespace byte
            const QByteArray result = m_data.mid(start, dataEnd - start);
            m_pos = pos + 2;
            return result;
        }
        ++pos;
    }
    m_pos = m_data.size();
    return m_data.mid(start);
}

namespace {
Object parseArrayBody(Lexer &lexer)
{
    Array items;
    while (true) {
        const Token peeked = lexer.peek();
        if (peeked.kind == TokenKind::arrayEnd) {
            lexer.next();
            break;
        }
        if (peeked.kind == TokenKind::end) {
            lexer.next();
            break;
        }
        items.append(parseObject(lexer));
    }
    return Object::array(std::move(items));
}

Object parseDictBody(Lexer &lexer)
{
    Dict dict;
    while (true) {
        const Token peeked = lexer.peek();
        if (peeked.kind == TokenKind::dictEnd) {
            lexer.next();
            break;
        }
        if (peeked.kind == TokenKind::end) {
            lexer.next();
            break;
        }
        if (peeked.kind != TokenKind::name) {
            parseObject(lexer); // a malformed entry with no key; discard and continue
            continue;
        }
        lexer.next();
        const QString key = QString::fromLatin1(peeked.bytes);
        dict.insert(key, parseObject(lexer));
    }
    return Object::dictionary(std::move(dict));
}
}

Object parseObject(Lexer &lexer)
{
    const Token token = lexer.next();
    switch (token.kind) {
    case TokenKind::number: {
        // "N G R" folds into one indirect-reference object; anything else
        // rewinds to just past this number.
        if (!token.isReal) {
            const qsizetype mark = lexer.position();
            const Token second = lexer.next();
            if (second.kind == TokenKind::number && !second.isReal) {
                const Token third = lexer.next();
                if (third.kind == TokenKind::keyword && third.bytes == "R")
                    return Object::reference(int(token.number), int(second.number));
            }
            lexer.seek(mark);
        }
        return token.isReal ? Object::real(token.number) : Object::integer(qint64(token.number));
    }
    case TokenKind::string:
        return Object::string(token.bytes);
    case TokenKind::name:
        return Object::name(token.bytes);
    case TokenKind::arrayStart:
        return parseArrayBody(lexer);
    case TokenKind::dictStart:
        return parseDictBody(lexer);
    case TokenKind::keyword:
        if (token.bytes == "true")
            return Object::boolean(true);
        if (token.bytes == "false")
            return Object::boolean(false);
        return Object::null(); // "null" or an unexpected keyword
    default:
        return Object::null();
    }
}

}
