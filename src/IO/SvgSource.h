#pragma once
#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QXmlStreamAttributes>
#include <utility>
#include <vector>

// One element of the SVG as QXmlStreamReader reads it; "#text" holds
// character data inside <text>.
struct SvgElement {
    QString tag;
    QXmlStreamAttributes attributes;
    QHash<QString, QString> style;
    QString text;
    int parent = -1;
    std::vector<int> children;
    // The start tag's span in the source.
    qsizetype begin = 0;
    qsizetype end = 0;
    bool selfClosing = false;
};

// The SVG's element tree, read beside nanosvg for what nanosvg drops:
// structure, names, text, images and clipping. Names are as written, prefix
// and all, since nanosvg matches them that way too.
class SvgSource {
public:
    explicit SvgSource(const QString &svg);
    // False when the XML is malformed or has no <svg> root.
    bool isValid() const { return m_root >= 0; }
    int root() const { return m_root; }
    const std::vector<SvgElement> &elements() const { return m_elements; }
    const SvgElement &at(int index) const { return m_elements[size_t(index)]; }

    QString attribute(int index, const QString &name) const;
    // A presentation property: style="" wins over the attribute.
    QString property(int index, const QString &name) const;
    // Inherited properties: the nearest element up the tree that sets it.
    QString inherited(int index, const QString &name) const;
    // The Layers panel name: inkscape:label, data-name, then id.
    QString label(int index) const;
    // The element a url(#id) or #id reference names, or -1.
    int reference(const QString &url) const;
    QTransform transform(int index) const;
    bool isHidden(int index) const;

    // An id no element uses.
    QString uniqueID();
    // Text for nanosvg to read at `at` in the source; rewritten() applies them.
    void insert(qsizetype at, const QString &text);
    QString rewritten() const;

private:
    QString m_source;
    std::vector<SvgElement> m_elements;
    QHash<QString, int> m_ids;
    std::vector<std::pair<qsizetype, QString>> m_insertions;
    int m_root = -1;
    int m_nextID = 1;
};

namespace SvgSyntax {
// transform="": matrix, translate, scale, rotate, skewX, skewY.
QTransform transform(QStringView text);
QHash<QString, QString> style(QStringView text);
QList<double> numbers(QStringView text);
// A CSS length in px; em against `fontSize`, % against `percentOf`.
double length(QStringView text, double fallback = 0, double fontSize = 16, double percentOf = 0);
// preserveAspectRatio: where the content sits (0, 0.5, 1 per axis) and
// whether it fills (slice) or fits (meet); `none` stretches.
struct AspectRatio {
    bool none = false;
    bool slice = false;
    QPointF align{0.5, 0.5};
};
AspectRatio aspectRatio(QStringView text);
// An attribute for text nanosvg reads, value escaped.
QString attribute(const QString &name, const QString &value);
}
