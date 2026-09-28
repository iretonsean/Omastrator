#include "IO/PdfEncoding.h"
#include "IO/PdfDocument.h"
#include "IO/PdfLexer.h"
#include <QFontDatabase>
#include <QRegularExpression>

namespace Pdf {

namespace {

QChar winAnsiChar(int code)
{
    switch (code) {
    case 0x80:
        return QChar(0x20AC);
    case 0x82:
        return QChar(0x201A);
    case 0x83:
        return QChar(0x0192);
    case 0x84:
        return QChar(0x201E);
    case 0x85:
        return QChar(0x2026);
    case 0x86:
        return QChar(0x2020);
    case 0x87:
        return QChar(0x2021);
    case 0x88:
        return QChar(0x02C6);
    case 0x89:
        return QChar(0x2030);
    case 0x8A:
        return QChar(0x0160);
    case 0x8B:
        return QChar(0x2039);
    case 0x8C:
        return QChar(0x0152);
    case 0x8E:
        return QChar(0x017D);
    case 0x91:
        return QChar(0x2018);
    case 0x92:
        return QChar(0x2019);
    case 0x93:
        return QChar(0x201C);
    case 0x94:
        return QChar(0x201D);
    case 0x95:
        return QChar(0x2022);
    case 0x96:
        return QChar(0x2013);
    case 0x97:
        return QChar(0x2014);
    case 0x98:
        return QChar(0x02DC);
    case 0x99:
        return QChar(0x2122);
    case 0x9A:
        return QChar(0x0161);
    case 0x9B:
        return QChar(0x203A);
    case 0x9C:
        return QChar(0x0153);
    case 0x9E:
        return QChar(0x017E);
    case 0x9F:
        return QChar(0x0178);
    case 0x81:
    case 0x8D:
    case 0x8F:
    case 0x90:
    case 0x9D:
        return QChar(0x0000); // undefined in CP1252
    default:
        return QChar(char16_t(code)); // ASCII and the Latin-1 supplement map 1:1
    }
}

QChar macRomanChar(int code)
{
    if (code < 0x80)
        return QChar(char16_t(code));
    static const char16_t table[128] = {
        0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7,
        0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5,
        0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9,
        0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
        0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248,
        0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
        0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7,
        0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
        0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD,
        0x02DB, 0x02C7,
    };
    return QChar(table[code - 0x80]);
}

QChar standardChar(int code)
{
    switch (code) {
    case 0x27:
        return QChar(0x2019); // quoteright
    case 0x60:
        return QChar(0x2018); // quoteleft
    case 0xA1:
        return QChar(0x00A1);
    case 0xA2:
        return QChar(0x00A2);
    case 0xA3:
        return QChar(0x00A3);
    case 0xA4:
        return QChar(0x2044);
    case 0xA5:
        return QChar(0x00A5);
    case 0xA6:
        return QChar(0x0192);
    case 0xA7:
        return QChar(0x00A7);
    case 0xA8:
        return QChar(0x00A4);
    case 0xA9:
        return QChar(0x0027);
    case 0xAA:
        return QChar(0x201C);
    case 0xAB:
        return QChar(0x00AB);
    case 0xAC:
        return QChar(0x2039);
    case 0xAD:
        return QChar(0x203A);
    case 0xAE:
        return QChar(0xFB01);
    case 0xAF:
        return QChar(0xFB02);
    case 0xB1:
        return QChar(0x2013);
    case 0xB2:
        return QChar(0x2020);
    case 0xB3:
        return QChar(0x2021);
    case 0xB4:
        return QChar(0x00B7);
    case 0xB6:
        return QChar(0x00B6);
    case 0xB7:
        return QChar(0x2022);
    case 0xB8:
        return QChar(0x201A);
    case 0xB9:
        return QChar(0x201E);
    case 0xBA:
        return QChar(0x201D);
    case 0xBB:
        return QChar(0x00BB);
    case 0xBC:
        return QChar(0x2026);
    case 0xBD:
        return QChar(0x2030);
    case 0xBF:
        return QChar(0x00BF);
    case 0xC1:
        return QChar(0x0060);
    case 0xC2:
        return QChar(0x00B4);
    case 0xC3:
        return QChar(0x02C6);
    case 0xC4:
        return QChar(0x02DC);
    case 0xC5:
        return QChar(0x00AF);
    case 0xC6:
        return QChar(0x02D8);
    case 0xC7:
        return QChar(0x02D9);
    case 0xC8:
        return QChar(0x00A8);
    case 0xCA:
        return QChar(0x02DA);
    case 0xCB:
        return QChar(0x00B8);
    case 0xCD:
        return QChar(0x02DD);
    case 0xCE:
        return QChar(0x02DB);
    case 0xCF:
        return QChar(0x02C7);
    case 0xD0:
        return QChar(0x2014);
    case 0xE1:
        return QChar(0x00C6);
    case 0xE3:
        return QChar(0x00AA);
    case 0xE8:
        return QChar(0x0141);
    case 0xE9:
        return QChar(0x00D8);
    case 0xEA:
        return QChar(0x0152);
    case 0xEB:
        return QChar(0x00BA);
    case 0xF1:
        return QChar(0x00E6);
    case 0xF5:
        return QChar(0x0131);
    case 0xF8:
        return QChar(0x0142);
    case 0xF9:
        return QChar(0x00F8);
    case 0xFA:
        return QChar(0x0153);
    case 0xFB:
        return QChar(0x00DF);
    default:
        return code >= 0x20 && code <= 0x7E ? QChar(char16_t(code)) : QChar(0x0000);
    }
}

const QHash<QByteArray, char16_t> &glyphNameTable()
{
    static const QHash<QByteArray, char16_t> table{
        {"space", 0x0020},          {"exclam", 0x0021},          {"quotedbl", 0x0022},        {"numbersign", 0x0023},
        {"dollar", 0x0024},         {"percent", 0x0025},         {"ampersand", 0x0026},       {"quotesingle", 0x0027},
        {"parenleft", 0x0028},      {"parenright", 0x0029},      {"asterisk", 0x002A},        {"plus", 0x002B},
        {"comma", 0x002C},          {"hyphen", 0x002D},          {"minus", 0x2212},           {"period", 0x002E},
        {"slash", 0x002F},          {"zero", 0x0030},            {"one", 0x0031},             {"two", 0x0032},
        {"three", 0x0033},          {"four", 0x0034},            {"five", 0x0035},            {"six", 0x0036},
        {"seven", 0x0037},          {"eight", 0x0038},           {"nine", 0x0039},            {"colon", 0x003A},
        {"semicolon", 0x003B},      {"less", 0x003C},            {"equal", 0x003D},           {"greater", 0x003E},
        {"question", 0x003F},       {"at", 0x0040},              {"bracketleft", 0x005B},     {"backslash", 0x005C},
        {"bracketright", 0x005D},   {"asciicircum", 0x005E},     {"underscore", 0x005F},      {"grave", 0x0060},
        {"braceleft", 0x007B},      {"bar", 0x007C},             {"braceright", 0x007D},      {"asciitilde", 0x007E},
        {"Agrave", 0x00C0},         {"Aacute", 0x00C1},          {"Acircumflex", 0x00C2},     {"Atilde", 0x00C3},
        {"Adieresis", 0x00C4},      {"Aring", 0x00C5},           {"AE", 0x00C6},              {"Ccedilla", 0x00C7},
        {"Egrave", 0x00C8},         {"Eacute", 0x00C9},          {"Ecircumflex", 0x00CA},     {"Edieresis", 0x00CB},
        {"Igrave", 0x00CC},         {"Iacute", 0x00CD},          {"Icircumflex", 0x00CE},     {"Idieresis", 0x00CF},
        {"Eth", 0x00D0},            {"Ntilde", 0x00D1},          {"Ograve", 0x00D2},          {"Oacute", 0x00D3},
        {"Ocircumflex", 0x00D4},    {"Otilde", 0x00D5},          {"Odieresis", 0x00D6},       {"multiply", 0x00D7},
        {"Oslash", 0x00D8},         {"Ugrave", 0x00D9},          {"Uacute", 0x00DA},          {"Ucircumflex", 0x00DB},
        {"Udieresis", 0x00DC},      {"Yacute", 0x00DD},          {"Thorn", 0x00DE},           {"germandbls", 0x00DF},
        {"agrave", 0x00E0},         {"aacute", 0x00E1},          {"acircumflex", 0x00E2},     {"atilde", 0x00E3},
        {"adieresis", 0x00E4},      {"aring", 0x00E5},           {"ae", 0x00E6},              {"ccedilla", 0x00E7},
        {"egrave", 0x00E8},         {"eacute", 0x00E9},          {"ecircumflex", 0x00EA},     {"edieresis", 0x00EB},
        {"igrave", 0x00EC},         {"iacute", 0x00ED},          {"icircumflex", 0x00EE},     {"idieresis", 0x00EF},
        {"eth", 0x00F0},            {"ntilde", 0x00F1},          {"ograve", 0x00F2},          {"oacute", 0x00F3},
        {"ocircumflex", 0x00F4},    {"otilde", 0x00F5},          {"odieresis", 0x00F6},       {"divide", 0x00F7},
        {"oslash", 0x00F8},         {"ugrave", 0x00F9},          {"uacute", 0x00FA},          {"ucircumflex", 0x00FB},
        {"udieresis", 0x00FC},      {"yacute", 0x00FD},          {"thorn", 0x00FE},           {"ydieresis", 0x00FF},
        {"emdash", 0x2014},         {"endash", 0x2013},          {"quoteleft", 0x2018},       {"quoteright", 0x2019},
        {"quotedblleft", 0x201C},   {"quotedblright", 0x201D},   {"quotesinglbase", 0x201A},  {"quotedblbase", 0x201E},
        {"bullet", 0x2022},         {"ellipsis", 0x2026},        {"dagger", 0x2020},          {"daggerdbl", 0x2021},
        {"perthousand", 0x2030},    {"trademark", 0x2122},       {"copyright", 0x00A9},       {"registered", 0x00AE},
        {"degree", 0x00B0},         {"plusminus", 0x00B1},       {"fi", 0xFB01},              {"fl", 0xFB02},
        {"florin", 0x0192},         {"section", 0x00A7},         {"paragraph", 0x00B6},       {"periodcentered", 0x00B7},
        {"guillemotleft", 0x00AB},  {"guillemotright", 0x00BB},  {"guilsinglleft", 0x2039},   {"guilsinglright", 0x203A},
        {"nonbreakingspace", 0x00A0}, {"Euro", 0x20AC},          {"currency", 0x00A4},        {"cent", 0x00A2},
        {"sterling", 0x00A3},       {"yen", 0x00A5},             {"circumflex", 0x02C6},      {"tilde", 0x02DC},
        {"dotlessi", 0x0131},       {"Lslash", 0x0141},          {"lslash", 0x0142},          {"OE", 0x0152},
        {"oe", 0x0153},             {"Scaron", 0x0160},          {"scaron", 0x0161},          {"Zcaron", 0x017D},
        {"zcaron", 0x017E},         {"Ydieresis", 0x0178},       {"onehalf", 0x00BD},         {"onequarter", 0x00BC},
        {"threequarters", 0x00BE},  {"onesuperior", 0x00B9},     {"twosuperior", 0x00B2},     {"threesuperior", 0x00B3},
        {"exclamdown", 0x00A1},     {"questiondown", 0x00BF},    {"logicalnot", 0x00AC},      {"brokenbar", 0x00A6},
        {"macron", 0x00AF},         {"acute", 0x00B4},           {"cedilla", 0x00B8},         {"dieresis", 0x00A8},
        {"ring", 0x02DA},           {"ordfeminine", 0x00AA},     {"ordmasculine", 0x00BA},    {"breve", 0x02D8},
        {"caron", 0x02C7},          {"dotaccent", 0x02D9},       {"hungarumlaut", 0x02DD},    {"ogonek", 0x02DB},
    };
    return table;
}

QHash<quint32, QString> parseToUnicodeCMap(const QByteArray &data)
{
    QHash<quint32, QString> map;
    Lexer lexer(data, 0);
    const auto bytesToCode = [](const QByteArray &bytes) -> quint32 {
        quint32 value = 0;
        for (const char byte : bytes)
            value = (value << 8) | uchar(byte);
        return value;
    };
    const auto bytesToUnicode = [](const QByteArray &bytes) -> QString {
        QString text;
        for (qsizetype i = 0; i + 1 < bytes.size(); i += 2)
            text.append(QChar(char16_t((uchar(bytes[i]) << 8) | uchar(bytes[i + 1]))));
        return text;
    };

    while (true) {
        const Token token = lexer.next();
        if (token.kind == TokenKind::end)
            break;
        if (token.kind != TokenKind::keyword)
            continue;
        if (token.bytes == "beginbfchar") {
            while (true) {
                const Token src = lexer.next();
                if (src.kind != TokenKind::string)
                    break;
                const Token dst = lexer.next();
                if (dst.kind != TokenKind::string)
                    break;
                map.insert(bytesToCode(src.bytes), bytesToUnicode(dst.bytes));
            }
        } else if (token.bytes == "beginbfrange") {
            while (true) {
                const Token lowToken = lexer.next();
                if (lowToken.kind != TokenKind::string)
                    break;
                const Token highToken = lexer.next();
                if (highToken.kind != TokenKind::string)
                    break;
                const quint32 low = bytesToCode(lowToken.bytes), high = bytesToCode(highToken.bytes);
                const Token destination = lexer.next();
                if (destination.kind == TokenKind::string) {
                    const QString base = bytesToUnicode(destination.bytes);
                    for (quint32 code = low; code <= high && code - low < 65536; ++code) {
                        QString text = base;
                        if (!text.isEmpty())
                            text[text.size() - 1] = QChar(char16_t(text.back().unicode() + (code - low)));
                        map.insert(code, text);
                    }
                } else if (destination.kind == TokenKind::arrayStart) {
                    quint32 code = low;
                    while (true) {
                        const Token item = lexer.next();
                        if (item.kind == TokenKind::arrayEnd || item.kind == TokenKind::end)
                            break;
                        if (item.kind == TokenKind::string)
                            map.insert(code, bytesToUnicode(item.bytes));
                        ++code;
                    }
                }
            }
        }
    }
    return map;
}
}

QChar glyphNameToUnicode(const QByteArray &name)
{
    if (name.size() == 1 && name[0] >= 0x20 && name[0] <= 0x7E)
        return QChar(char16_t(uchar(name[0])));
    if ((name.startsWith("uni") && name.size() >= 7) || (name.startsWith('u') && name.size() >= 5 && name.size() <= 7)) {
        const QByteArray hex = name.startsWith("uni") ? name.mid(3, 4) : name.mid(1);
        bool ok = false;
        const uint code = hex.toUInt(&ok, 16);
        if (ok && code <= 0x10FFFF)
            return QChar(char16_t(code));
    }
    const auto it = glyphNameTable().constFind(name);
    if (it != glyphNameTable().constEnd())
        return QChar(*it);
    // "A.sc", "e.alt01" and similar suffixed variants: fall back to the base glyph.
    const qsizetype dot = name.indexOf('.');
    if (dot > 0)
        return glyphNameToUnicode(name.left(dot));
    return QChar();
}

void splitPostScriptFontName(const QString &postScriptName, QString *family, QString *style)
{
    QString name = postScriptName;
    static const QRegularExpression subsetPrefix(QStringLiteral("^[A-Z]{6}\\+"));
    name.remove(subsetPrefix);

    bool bold = false, italic = false;
    const QStringList parts = name.split(QRegularExpression(QStringLiteral("[-,]")), Qt::SkipEmptyParts);
    QString baseName = parts.isEmpty() ? name : parts.first();
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString part = parts[i];
        if (part.contains(QStringLiteral("Bold"), Qt::CaseInsensitive))
            bold = true;
        if (part.contains(QStringLiteral("Italic"), Qt::CaseInsensitive) || part.contains(QStringLiteral("Oblique"), Qt::CaseInsensitive))
            italic = true;
    }
    // Some names run the style into the family with no separator ("ArialBoldMT").
    if (baseName.contains(QStringLiteral("Bold"), Qt::CaseInsensitive)) {
        bold = true;
        baseName.remove(QStringLiteral("Bold"), Qt::CaseInsensitive);
    }
    if (baseName.contains(QStringLiteral("Italic"), Qt::CaseInsensitive)) {
        italic = true;
        baseName.remove(QStringLiteral("Italic"), Qt::CaseInsensitive);
    } else if (baseName.contains(QStringLiteral("Oblique"), Qt::CaseInsensitive)) {
        italic = true;
        baseName.remove(QStringLiteral("Oblique"), Qt::CaseInsensitive);
    }
    baseName = baseName.trimmed();
    if (baseName.isEmpty())
        baseName = name.isEmpty() ? QStringLiteral("Sans Serif") : name;

    *family = baseName;
    *style = bold && italic ? QStringLiteral("Bold Italic") : bold ? QStringLiteral("Bold") : italic ? QStringLiteral("Italic") : QStringLiteral("Regular");
}

QString Font::decode(const QByteArray &bytes, bool *hadUnmapped) const
{
    QString out;
    if (m_isType0) {
        for (qsizetype i = 0; i + 1 < bytes.size(); i += 2) {
            const quint32 code = (uchar(bytes[i]) << 8) | uchar(bytes[i + 1]);
            const auto it = m_toUnicode.constFind(code);
            if (m_hasToUnicode && it != m_toUnicode.constEnd())
                out += it.value();
            else {
                out += QChar('?');
                if (hadUnmapped)
                    *hadUnmapped = true;
            }
        }
        return out;
    }
    for (const char byte : bytes) {
        const int code = uchar(byte);
        const auto unicodeIt = m_toUnicode.constFind(quint32(code));
        if (m_hasToUnicode && unicodeIt != m_toUnicode.constEnd()) {
            out += unicodeIt.value();
            continue;
        }
        out += m_simpleEncoding.value(code, QStringLiteral("?"));
    }
    return out;
}

Font Font::load(const Document &document, const Object &fontObject, QStringList *warnings)
{
    Font font;
    const Object resolved = document.resolve(fontObject);
    const Dict dict = resolved.toDict();

    const QByteArray subtype = document.resolve(dict.value(QStringLiteral("Subtype"))).toNameValue();
    font.m_isType0 = subtype == "Type0";

    QString postScriptName = QString::fromLatin1(document.resolve(dict.value(QStringLiteral("BaseFont"))).toNameValue());
    if (font.m_isType0) {
        const Array descendants = document.resolve(dict.value(QStringLiteral("DescendantFonts"))).toArray();
        if (!descendants.isEmpty()) {
            const Dict descendant = document.resolve(descendants[0]).toDict();
            if (postScriptName.isEmpty())
                postScriptName = QString::fromLatin1(document.resolve(descendant.value(QStringLiteral("BaseFont"))).toNameValue());
        }
        const QByteArray cmapName = document.resolve(dict.value(QStringLiteral("Encoding"))).toNameValue();
        if (!cmapName.isEmpty() && cmapName != "Identity-H" && cmapName != "Identity-V" && warnings)
            *warnings << QStringLiteral("A composite font with a non-Identity encoding was read as if it were Identity; some text may be wrong.");
    }
    font.rawBaseFont = postScriptName;
    splitPostScriptFontName(postScriptName.isEmpty() ? QStringLiteral("Sans Serif") : postScriptName, &font.family, &font.style);

    const QStringList installed = QFontDatabase::families();
    bool found = false;
    for (const QString &candidate : installed) {
        if (candidate.compare(font.family, Qt::CaseInsensitive) == 0) {
            font.family = candidate;
            found = true;
            break;
        }
    }
    font.isMissingFamily = !found;

    const Object toUnicodeObject = document.resolve(dict.value(QStringLiteral("ToUnicode")));
    if (toUnicodeObject.isStream()) {
        font.m_toUnicode = parseToUnicodeCMap(document.streamData(toUnicodeObject).bytes);
        font.m_hasToUnicode = true;
    }

    if (!font.m_isType0) {
        for (int code = 0; code < 256; ++code)
            font.m_simpleEncoding.insert(code, QString(standardChar(code)));

        const Object encodingObject = document.resolve(dict.value(QStringLiteral("Encoding")));
        QByteArray baseEncodingName;
        Array differences;
        if (encodingObject.isName()) {
            baseEncodingName = encodingObject.toNameValue();
        } else if (encodingObject.isDictionary()) {
            baseEncodingName = document.resolve(encodingObject.at(QStringLiteral("BaseEncoding"))).toNameValue();
            differences = document.resolve(encodingObject.at(QStringLiteral("Differences"))).toArray();
        }
        if (baseEncodingName == "WinAnsiEncoding") {
            for (int code = 0; code < 256; ++code)
                font.m_simpleEncoding.insert(code, QString(winAnsiChar(code)));
        } else if (baseEncodingName == "MacRomanEncoding") {
            for (int code = 0; code < 256; ++code)
                font.m_simpleEncoding.insert(code, QString(macRomanChar(code)));
        }
        int currentCode = 0;
        for (const Object &entry : differences) {
            const Object resolvedEntry = document.resolve(entry);
            if (resolvedEntry.isNumber()) {
                currentCode = int(resolvedEntry.toInt());
            } else if (resolvedEntry.isName() && currentCode >= 0 && currentCode < 256) {
                const QChar mapped = glyphNameToUnicode(resolvedEntry.toNameValue());
                if (!mapped.isNull())
                    font.m_simpleEncoding.insert(currentCode, QString(mapped));
                ++currentCode;
            }
        }
    }

    return font;
}

}
