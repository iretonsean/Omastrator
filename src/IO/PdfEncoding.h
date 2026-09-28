#pragma once
#include "IO/PdfObject.h"
#include <QHash>
#include <QString>
#include <QStringList>

// Glyph-to-Unicode mapping: a Type0 font's ToUnicode CMap, or a simple
// font's /Encoding (a base table plus /Differences), tried in that order as
// PDF.md asks. Also font-name cleanup (subset prefixes, PostScript style
// suffixes) mapped to an installed family through QFontDatabase.
namespace Pdf {
class Document;

class Font {
public:
    static Font load(const Document &document, const Object &fontDict, QStringList *warnings);

    // The bytes of one Tj/TJ show-text operand, decoded to Unicode text. Sets
    // `hadUnmapped` if a Type0 code had no ToUnicode entry (shown as '?').
    QString decode(const QByteArray &bytes, bool *hadUnmapped) const;

    QString family;
    QString style; // "Regular", "Bold", "Italic", "Bold Italic"
    bool isMissingFamily = false; // the family had to be kept as-is; QFontDatabase had no match
    QString rawBaseFont; // for the "missing font" warning, subset prefix already stripped

private:
    bool m_isType0 = false;
    bool m_hasToUnicode = false;
    QHash<quint32, QString> m_toUnicode;
    QHash<int, QString> m_simpleEncoding; // 0-255 -> unicode text, base encoding plus Differences
};

// Strips a subset prefix ("ABCDEF+") and returns family/style guessed from a
// PostScript font name's "-Bold"/"Italic"/"Oblique" style conventions.
void splitPostScriptFontName(const QString &postScriptName, QString *family, QString *style);

// A glyph name to Unicode: "uniXXXX"/"uXXXXX(X)" hex forms first, then a
// common-name table covering Latin letters, digits and everyday punctuation.
// Returns a null QChar (isNull()) when the name isn't recognised.
QChar glyphNameToUnicode(const QByteArray &name);

}
