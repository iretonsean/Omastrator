#include "IO/SvgSource.h"
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
// A number at the start of `text` and where it stops, as SVG writes them: 1, -.5, 2e3.
std::optional<std::pair<double, qsizetype>> leadingNumber(QStringView text, qsizetype from = 0)
{
    qsizetype i = from;
    const qsizetype n = text.size();
    if (i < n && (text[i] == QLatin1Char('+') || text[i] == QLatin1Char('-')))
        ++i;
    bool digits = false, dot = false;
    while (i < n && (text[i].isDigit() || (text[i] == QLatin1Char('.') && !dot))) {
        if (text[i] == QLatin1Char('.'))
            dot = true;
        else
            digits = true;
        ++i;
    }
    if (!digits)
        return std::nullopt;
    if (i < n && (text[i] == QLatin1Char('e') || text[i] == QLatin1Char('E'))) {
        qsizetype e = i + 1;
        if (e < n && (text[e] == QLatin1Char('+') || text[e] == QLatin1Char('-')))
            ++e;
        if (e < n && text[e].isDigit()) {
            i = e;
            while (i < n && text[i].isDigit())
                ++i;
        }
    }
    bool ok = false;
    const double value = text.mid(from, i - from).toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    return std::pair{value, i};
}
}

SvgSource::SvgSource(const QString &svg) : m_source(svg)
{
    QXmlStreamReader xml(svg);
    // nanosvg matches names as written, prefixes and all; so do we.
    xml.setNamespaceProcessing(false);
    std::vector<int> open;
    int textDepth = 0;
    bool sawRoot = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            SvgElement element;
            element.tag = xml.qualifiedName().toString();
            element.attributes = xml.attributes();
            element.style = SvgSyntax::style(element.attributes.value(QStringLiteral("style")));
            element.end = xml.characterOffset();
            element.begin = element.end > 0 ? svg.lastIndexOf(QLatin1Char('<'), element.end - 1) : -1;
            // Offsets are how ids get written in; if they are off, nothing is.
            if (element.begin < 0 || element.end < 2 || element.end > svg.size() || svg.at(element.end - 1) != QLatin1Char('>')) {
                m_root = -1;
                return;
            }
            element.selfClosing = svg.at(element.end - 2) == QLatin1Char('/');
            const int index = int(m_elements.size());
            if (!open.empty()) {
                element.parent = open.back();
                m_elements[size_t(open.back())].children.push_back(index);
            } else if (!sawRoot) {
                sawRoot = true;
                if (element.tag == QLatin1String("svg"))
                    m_root = index;
            }
            const QString id = element.attributes.value(QStringLiteral("id")).toString();
            if (!id.isEmpty() && !m_ids.contains(id))
                m_ids.insert(id, index);
            if (element.tag == QLatin1String("text"))
                ++textDepth;
            m_elements.push_back(std::move(element));
            open.push_back(index);
        } else if (token == QXmlStreamReader::EndElement) {
            if (!open.empty()) {
                if (m_elements[size_t(open.back())].tag == QLatin1String("text"))
                    --textDepth;
                open.pop_back();
            }
        } else if (token == QXmlStreamReader::Characters && textDepth > 0 && !open.empty()) {
            SvgElement characters;
            characters.tag = QStringLiteral("#text");
            characters.text = xml.text().toString();
            characters.parent = open.back();
            m_elements[size_t(open.back())].children.push_back(int(m_elements.size()));
            m_elements.push_back(std::move(characters));
        }
    }
    if (xml.hasError())
        m_root = -1;
}

QString SvgSource::attribute(int index, const QString &name) const
{
    return at(index).attributes.value(name).toString().trimmed();
}

QString SvgSource::property(int index, const QString &name) const
{
    const SvgElement &element = at(index);
    const auto styled = element.style.constFind(name);
    if (styled != element.style.cend())
        return *styled;
    return attribute(index, name);
}

QString SvgSource::inherited(int index, const QString &name) const
{
    for (int at = index; at >= 0; at = this->at(at).parent) {
        const QString value = property(at, name);
        if (!value.isEmpty() && value != QLatin1String("inherit"))
            return value;
    }
    return {};
}

QString SvgSource::label(int index) const
{
    for (const char *name : {"inkscape:label", "data-name", "id"}) {
        const QString value = attribute(index, QLatin1String(name));
        if (!value.isEmpty())
            return value;
    }
    return {};
}

int SvgSource::reference(const QString &url) const
{
    QString id = url.trimmed();
    if (id.startsWith(QLatin1String("url("))) {
        const qsizetype close = id.indexOf(QLatin1Char(')'));
        id = id.mid(4, close < 0 ? -1 : close - 4).trimmed();
    }
    if (id.size() > 1 && (id.front() == QLatin1Char('"') || id.front() == QLatin1Char('\'')))
        id = id.mid(1, id.size() - 2);
    if (!id.startsWith(QLatin1Char('#')))
        return -1;
    return m_ids.value(id.mid(1), -1);
}

QTransform SvgSource::transform(int index) const
{
    return SvgSyntax::transform(attribute(index, QStringLiteral("transform")));
}

bool SvgSource::isHidden(int index) const
{
    const QString visibility = property(index, QStringLiteral("visibility"));
    return property(index, QStringLiteral("display")) == QLatin1String("none") || visibility == QLatin1String("hidden")
           || visibility == QLatin1String("collapse");
}

QString SvgSource::uniqueID()
{
    QString id;
    do
        id = QStringLiteral("oma-import-%1").arg(m_nextID++);
    while (m_ids.contains(id));
    m_ids.insert(id, -1);
    return id;
}

void SvgSource::insert(qsizetype at, const QString &text)
{
    m_insertions.emplace_back(at, text);
}

QString SvgSource::rewritten() const
{
    std::vector<std::pair<qsizetype, QString>> insertions = m_insertions;
    std::stable_sort(insertions.begin(), insertions.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QString result;
    qsizetype done = 0;
    for (const auto &[at, text] : insertions) {
        result += QStringView(m_source).mid(done, at - done);
        result += text;
        done = at;
    }
    result += QStringView(m_source).mid(done);
    return result;
}

namespace SvgSyntax {
QTransform transform(QStringView text)
{
    QTransform result;
    qsizetype i = 0;
    const qsizetype n = text.size();
    while (i < n) {
        while (i < n && (text[i].isSpace() || text[i] == QLatin1Char(',')))
            ++i;
        const qsizetype nameStart = i;
        while (i < n && text[i].isLetter())
            ++i;
        const QStringView name = text.mid(nameStart, i - nameStart);
        const qsizetype open = text.indexOf(QLatin1Char('('), i);
        const qsizetype close = open < 0 ? -1 : text.indexOf(QLatin1Char(')'), open);
        if (name.isEmpty() || close < 0)
            break;
        const QList<double> a = numbers(text.mid(open + 1, close - open - 1));
        i = close + 1;
        const auto arg = [&](int index, double fallback) { return index < a.size() ? a[index] : fallback; };
        QTransform step;
        if (name == QLatin1String("matrix") && a.size() >= 6) {
            step = QTransform(a[0], a[1], a[2], a[3], a[4], a[5]);
        } else if (name == QLatin1String("translate") && !a.isEmpty()) {
            step = QTransform::fromTranslate(a[0], arg(1, 0));
        } else if (name == QLatin1String("scale") && !a.isEmpty()) {
            step = QTransform::fromScale(a[0], arg(1, a[0]));
        } else if (name == QLatin1String("rotate") && !a.isEmpty()) {
            const double radians = a[0] * M_PI / 180, c = std::cos(radians), s = std::sin(radians);
            const QPointF center(arg(1, 0), arg(2, 0));
            step = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform(c, s, -s, c, 0, 0)
                   * QTransform::fromTranslate(center.x(), center.y());
        } else if (name == QLatin1String("skewX") && !a.isEmpty()) {
            step = QTransform(1, 0, std::tan(a[0] * M_PI / 180), 1, 0, 0);
        } else if (name == QLatin1String("skewY") && !a.isEmpty()) {
            step = QTransform(1, std::tan(a[0] * M_PI / 180), 0, 1, 0, 0);
        } else {
            break;
        }
        // The rightmost step applies first.
        result = step * result;
    }
    return result;
}

QHash<QString, QString> style(QStringView text)
{
    QHash<QString, QString> result;
    for (QStringView declaration : text.split(QLatin1Char(';'))) {
        const qsizetype colon = declaration.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString name = declaration.left(colon).trimmed().toString().toLower();
        QString value = declaration.mid(colon + 1).trimmed().toString();
        if (value.endsWith(QLatin1String("!important")))
            value = value.chopped(10).trimmed();
        if (!name.isEmpty())
            result.insert(name, value);
    }
    return result;
}

QList<double> numbers(QStringView text)
{
    QList<double> result;
    qsizetype i = 0;
    while (i < text.size()) {
        if (text[i].isSpace() || text[i] == QLatin1Char(',')) {
            ++i;
            continue;
        }
        const auto number = leadingNumber(text, i);
        if (!number) {
            ++i;
            continue;
        }
        result << number->first;
        i = number->second;
    }
    return result;
}

double length(QStringView text, double fallback, double fontSize, double percentOf)
{
    text = text.trimmed();
    const auto number = leadingNumber(text);
    if (!number)
        return fallback;
    const QString unit = text.mid(number->second).trimmed().toString().toLower();
    double scale = 1;
    if (unit == QLatin1String("pt"))
        scale = 96.0 / 72;
    else if (unit == QLatin1String("pc"))
        scale = 16;
    else if (unit == QLatin1String("mm"))
        scale = 96 / 25.4;
    else if (unit == QLatin1String("cm"))
        scale = 96 / 2.54;
    else if (unit == QLatin1String("in"))
        scale = 96;
    else if (unit == QLatin1String("em"))
        scale = fontSize;
    else if (unit == QLatin1String("ex"))
        scale = fontSize / 2;
    else if (unit == QLatin1String("rem"))
        scale = 16;
    else if (unit == QLatin1String("%"))
        scale = percentOf / 100;
    return number->first * scale;
}

AspectRatio aspectRatio(QStringView text)
{
    AspectRatio result;
    QList<QStringView> words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!words.isEmpty() && words.front() == QLatin1String("defer"))
        words.removeFirst();
    if (words.isEmpty())
        return result;
    const QStringView align = words.front();
    if (align == QLatin1String("none")) {
        result.none = true;
        return result;
    }
    const auto place = [](QStringView word) { return word == QLatin1String("Min") ? 0.0 : word == QLatin1String("Max") ? 1.0 : 0.5; };
    if (align.size() == 8)
        result.align = {place(align.mid(1, 3)), place(align.mid(5, 3))};
    result.slice = words.size() > 1 && words[1] == QLatin1String("slice");
    return result;
}

QString attribute(const QString &name, const QString &value)
{
    return QStringLiteral(" %1=\"%2\"").arg(name, value.toHtmlEscaped());
}
}
