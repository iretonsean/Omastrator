#include "Anywhere/Inspect.h"
#include "Anywhere/InspectPageScript.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
// "#rrggbb", "rgb(…)" or "rgba(…)", as a page or AT-SPI gives them.
QColor parseColor(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1Char('#')))
        return QColor::fromString(trimmed);
    static const QRegularExpression numbers(QStringLiteral("[-+]?[0-9]*\\.?[0-9]+"));
    if (!trimmed.startsWith(QLatin1String("rgb")))
        return {};
    std::vector<double> values;
    for (auto match = numbers.globalMatch(trimmed); match.hasNext();)
        values.push_back(match.next().captured(0).toDouble());
    if (values.size() < 3)
        return {};
    QColor color{int(values[0]), int(values[1]), int(values[2])};
    if (values.size() > 3)
        color.setAlphaF(float(std::clamp(values[3], 0.0, 1.0)));
    return color;
}

QRect rectFrom(const QJsonArray &array, QPoint offset)
{
    return QRectF(array.at(0).toDouble() + offset.x(), array.at(1).toDouble() + offset.y(), array.at(2).toDouble(), array.at(3).toDouble())
        .toAlignedRect();
}

QString colorName(const QColor &color)
{
    if (!color.isValid())
        return {};
    return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
}

const char *pythonHelper = R"py(import json, sys
try:
    import gi
    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi
except Exception as failure:
    print(json.dumps({"error": "AT-SPI isn't available: %s" % failure}))
    sys.exit(0)

pid, x, y = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
WINDOW = Atspi.CoordType.WINDOW


def app_for(pid):
    desktop = Atspi.get_desktop(0)
    for index in range(desktop.get_child_count()):
        app = desktop.get_child_at_index(index)
        try:
            if app is not None and app.get_process_id() == pid:
                return app
        except Exception:
            pass
    return None


def frames(app):
    ranked = []
    for index in range(app.get_child_count()):
        frame = app.get_child_at_index(index)
        if frame is None:
            continue
        try:
            states = frame.get_state_set()
            ranked.append((not states.contains(Atspi.StateType.ACTIVE), not states.contains(Atspi.StateType.SHOWING), index, frame))
        except Exception:
            ranked.append((True, True, index, frame))
    ranked.sort(key=lambda each: each[:3])
    return [each[3] for each in ranked]


def extents(node):
    box = Atspi.Component.get_extents(node, WINDOW)
    return [box.x, box.y, box.width, box.height]


def deepest(node):
    for _ in range(64):
        try:
            child = Atspi.Component.get_accessible_at_point(node, x, y, WINDOW)
        except Exception:
            child = None
        if child is None or child == node:
            return node
        node = child
    return node


def color(value):
    parts = [int(part) for part in str(value).split(",") if part.strip().isdigit()]
    if len(parts) < 3:
        return ""
    # GTK 3 reports 16-bit channels; others 8-bit.
    scale = 257 if max(parts[:3]) > 255 else 1
    return "#%02x%02x%02x" % tuple(part // scale for part in parts[:3])


app = app_for(pid)
if app is None:
    print(json.dumps({"error": "This app has no accessibility tree."}))
    sys.exit(0)
for frame in frames(app):
    try:
        node = deepest(frame)
        answer = {"role": node.get_role_name(), "name": node.get_name() or "", "rect": extents(node), "app": app.get_name() or ""}
    except Exception:
        continue
    try:
        attributes = Atspi.Text.get_default_attributes(node) or {}
        count = Atspi.Text.get_character_count(node)
        answer["text"] = Atspi.Text.get_text(node, 0, min(count, 80))
        answer["fontFamily"] = attributes.get("family-name", "")
        answer["fontSize"] = attributes.get("size", "")
        answer["fontWeight"] = attributes.get("weight", "")
        answer["color"] = color(attributes.get("fg-color", ""))
        answer["background"] = color(attributes.get("bg-color", ""))
    except Exception:
        pass
    print(json.dumps(answer))
    sys.exit(0)
print(json.dumps({"error": "Nothing accessible is there."}))
)py";
}

QString Surface::kindName(Kind kind)
{
    switch (kind) {
    case Kind::web:
        return QStringLiteral("web");
    case Kind::window:
        return QStringLiteral("window");
    case Kind::desktop:
        break;
    }
    return QStringLiteral("desktop");
}

QString Surface::keyFor(Kind kind, const QString &app, const QUrl &url)
{
    if (kind == Kind::web)
        return QStringLiteral("web:") + url.adjusted(QUrl::RemoveFragment | QUrl::StripTrailingSlash).toString();
    return kindName(kind) + QLatin1Char(':') + app;
}

QString Surface::label() const
{
    if (kind == Kind::web) {
        QString place = url.host() + url.path();
        if (url.isLocalFile())
            place = QFileInfo(url.toLocalFile()).fileName();
        if (place.endsWith(QLatin1Char('/')))
            place.chop(1);
        return place.isEmpty() ? url.toString() : place;
    }
    if (kind == Kind::window)
        return app.isEmpty() ? title : app;
    return QStringLiteral("Desktop (%1)").arg(app);
}

QPointF Surface::origin() const
{
    if (kind == Kind::web)
        return QPointF(viewport) - scroll;
    return rect.topLeft();
}

QJsonObject Surface::toJson() const
{
    return {{"kind", kindName(kind)},
            {"key", key},
            {"app", app},
            {"title", title},
            {"url", url.toString()},
            {"pid", pid},
            {"address", address},
            {"rect", QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()}},
            {"monitor", monitor},
            {"viewport", QJsonArray{viewport.x(), viewport.y()}},
            {"scroll", QJsonArray{scroll.x(), scroll.y()}},
            {"otherBrowser", otherBrowser},
            {"label", label()}};
}

Surface Surface::fromJson(const QJsonObject &json)
{
    Surface surface;
    const QString kind = json["kind"].toString();
    surface.kind = kind == QLatin1String("web") ? Kind::web : kind == QLatin1String("window") ? Kind::window : Kind::desktop;
    surface.key = json["key"].toString();
    surface.app = json["app"].toString();
    surface.title = json["title"].toString();
    surface.url = QUrl(json["url"].toString());
    surface.pid = json["pid"].toInteger();
    surface.address = json["address"].toString();
    const QJsonArray rect = json["rect"].toArray();
    surface.rect = QRect(rect.at(0).toInt(), rect.at(1).toInt(), rect.at(2).toInt(), rect.at(3).toInt());
    surface.monitor = json["monitor"].toString();
    const QJsonArray viewport = json["viewport"].toArray(), scroll = json["scroll"].toArray();
    surface.viewport = QPoint(viewport.at(0).toInt(), viewport.at(1).toInt());
    surface.scroll = QPointF(scroll.at(0).toDouble(), scroll.at(1).toDouble());
    surface.otherBrowser = json["otherBrowser"].toBool();
    if (surface.key.isEmpty())
        surface.key = keyFor(surface.kind, surface.app, surface.url);
    return surface;
}

QJsonObject Inspection::toJson() const
{
    QJsonObject json{{"id", id},
                     {"surface", surface.toJson()},
                     {"bounds", QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()}},
                     {"source", source},
                     {"role", role},
                     {"name", name},
                     {"text", text},
                     {"color", colorName(color)},
                     {"background", colorName(background)},
                     {"pixel", colorName(pixel)},
                     {"fontFamily", fontFamily},
                     {"fontSize", fontSize},
                     {"fontWeight", fontWeight},
                     {"summary", summary()}};
    if (!styles.isEmpty())
        json["styles"] = styles;
    return json;
}

QString Inspection::summary() const
{
    const QString size = QStringLiteral("%1 × %2").arg(bounds.width()).arg(bounds.height());
    QString what = name.isEmpty() ? role : name;
    if (source == QLatin1String("window") && !surface.app.isEmpty())
        what = surface.app;
    if (source == QLatin1String("screen"))
        what = surface.label();
    return what.isEmpty() ? size : what + QLatin1Char(' ') + size;
}

namespace Inspect {
QJsonObject Measure::toJson() const
{
    return {{"x1", line.x1()}, {"y1", line.y1()}, {"x2", line.x2()}, {"y2", line.y2()}, {"value", value}};
}

std::vector<Measure> distances(const QRect &from, const QRect &to)
{
    std::vector<Measure> lines;
    if (from.isEmpty() || to.isEmpty())
        return lines;
    // Edges as a designer counts them: right = x + width.
    const int al = from.x(), ar = from.x() + from.width(), at = from.y(), ab = from.y() + from.height();
    const int bl = to.x(), br = to.x() + to.width(), bt = to.y(), bb = to.y() + to.height();
    auto add = [&](QLine line) {
        const int value = std::abs(line.dx()) + std::abs(line.dy());
        if (value > 0)
            lines.push_back({line, value});
    };
    const bool fromInside = bl <= al && ar <= br && bt <= at && ab <= bb;
    const bool toInside = al <= bl && br <= ar && at <= bt && bb <= ab;
    if (fromInside || toInside) {
        // Insets of the inner box, measured through its centre.
        const QRect inner = fromInside ? from : to, outer = fromInside ? to : from;
        const int il = inner.x(), ir = inner.x() + inner.width(), it = inner.y(), ib = inner.y() + inner.height();
        const int ol = outer.x(), orr = outer.x() + outer.width(), ot = outer.y(), ob = outer.y() + outer.height();
        const int cx = il + inner.width() / 2, cy = it + inner.height() / 2;
        add(QLine(cx, ot, cx, it));
        add(QLine(cx, ib, cx, ob));
        add(QLine(ol, cy, il, cy));
        add(QLine(ir, cy, orr, cy));
        return lines;
    }
    const int overlapTop = std::max(at, bt), overlapBottom = std::min(ab, bb);
    const int overlapLeft = std::max(al, bl), overlapRight = std::min(ar, br);
    const int midY = overlapTop < overlapBottom ? (overlapTop + overlapBottom) / 2 : at + from.height() / 2;
    const int midX = overlapLeft < overlapRight ? (overlapLeft + overlapRight) / 2 : al + from.width() / 2;
    bool gap = false;
    if (bl >= ar) {
        add(QLine(ar, midY, bl, midY));
        gap = true;
    } else if (br <= al) {
        add(QLine(br, midY, al, midY));
        gap = true;
    }
    if (bt >= ab) {
        add(QLine(midX, ab, midX, bt));
        gap = true;
    } else if (bb <= at) {
        add(QLine(midX, bb, midX, at));
        gap = true;
    }
    if (!gap) {
        // Overlapping: how far the near edges are apart.
        add(QLine(std::min(al, bl), midY, std::max(al, bl), midY));
        add(QLine(midX, std::min(at, bt), midX, std::max(at, bt)));
    }
    return lines;
}

QPoint viewportOrigin(const QRect &window, const QSizeF &inner)
{
    if (inner.isEmpty())
        return window.topLeft();
    // The same arithmetic as webScript's, so the page and the overlay agree.
    const int side = std::max(0, int(std::floor((window.width() - inner.width()) / 2)));
    const int top = std::max(0, int(std::floor(window.height() - inner.height())) - side);
    return window.topLeft() + QPoint(side, top);
}

QString webScript(QPoint windowPoint, QSize windowSize)
{
    return QStringLiteral("(function () {\n%1\nreturn omastratorInspect(%2, %3, %4, %5);\n})()")
        .arg(QString::fromUtf8(OmastratorAnywhere::inspectPage))
        .arg(windowPoint.x())
        .arg(windowPoint.y())
        .arg(windowSize.width())
        .arg(windowSize.height());
}

std::optional<Inspection> fromWeb(const QJsonObject &answer, Surface surface)
{
    const QJsonArray inner = answer["inner"].toArray(), scroll = answer["scroll"].toArray();
    surface.kind = Surface::Kind::web;
    if (!answer["url"].toString().isEmpty())
        surface.url = QUrl(answer["url"].toString());
    surface.title = answer["title"].toString(surface.title);
    surface.viewport = viewportOrigin(surface.rect, QSizeF(inner.at(0).toDouble(), inner.at(1).toDouble()));
    surface.scroll = QPointF(scroll.at(0).toDouble(), scroll.at(1).toDouble());
    surface.key = Surface::keyFor(surface.kind, surface.app, surface.url);
    const QJsonObject element = answer["element"].toObject();
    if (element.isEmpty())
        return std::nullopt;
    Inspection inspection;
    inspection.surface = surface;
    inspection.source = QStringLiteral("dom");
    inspection.bounds = rectFrom(element["rect"].toArray(), surface.viewport);
    inspection.role = element["tag"].toString();
    inspection.name = element["selector"].toString();
    inspection.text = element["text"].toString();
    inspection.color = parseColor(element["color"].toString());
    inspection.background = parseColor(element["background"].toString());
    inspection.fontFamily = element["fontFamily"].toString();
    inspection.fontSize = element["fontSize"].toDouble();
    inspection.fontWeight = element["fontWeight"].toVariant().toString();
    inspection.styles = element["styles"].toObject();
    return inspection;
}

QString accessibleScript()
{
    return QString::fromUtf8(pythonHelper);
}

std::optional<QJsonObject> accessibleAt(qint64 pid, QPoint windowPoint, QString *error)
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_ATSPI");
    QString program = overridden;
    QStringList args;
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (program.isEmpty()) {
            if (error)
                *error = QStringLiteral("python3 isn't installed, so the accessibility tree can't be read.");
            return std::nullopt;
        }
        args << QStringLiteral("-c") << accessibleScript();
    }
    args << QString::number(pid) << QString::number(windowPoint.x()) << QString::number(windowPoint.y());
    QProcess process;
    process.start(program, args);
    // A slow tree is skipped: the window's bounds stand in.
    if (!process.waitForStarted(2000) || !process.waitForFinished(1500)) {
        process.kill();
        process.waitForFinished(500);
        if (error)
            *error = QStringLiteral("The accessibility tree took too long.");
        return std::nullopt;
    }
    const QJsonObject answer = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (answer.isEmpty() || answer.contains("error")) {
        if (error)
            *error = answer["error"].toString(QStringLiteral("The accessibility tree couldn't be read."));
        return std::nullopt;
    }
    return answer;
}

std::optional<Inspection> fromAccessible(const QJsonObject &answer, const Surface &surface)
{
    const QJsonArray rect = answer["rect"].toArray();
    if (rect.size() < 4 || rect.at(2).toInt() <= 0 || rect.at(3).toInt() <= 0)
        return std::nullopt;
    Inspection inspection;
    inspection.surface = surface;
    inspection.source = QStringLiteral("accessibility");
    inspection.bounds = rectFrom(rect, surface.rect.topLeft()).intersected(surface.rect);
    if (inspection.bounds.isEmpty())
        return std::nullopt;
    inspection.role = answer["role"].toString();
    inspection.name = answer["name"].toString();
    inspection.text = answer["text"].toString();
    inspection.fontFamily = answer["fontFamily"].toString();
    inspection.fontSize = answer["fontSize"].toVariant().toString().toDouble();
    inspection.fontWeight = answer["fontWeight"].toVariant().toString();
    inspection.color = parseColor(answer["color"].toString());
    inspection.background = parseColor(answer["background"].toString());
    return inspection;
}

std::optional<QColor> parsePpm(const QByteArray &ppm)
{
    // "P6\n1 1\n255\n" then three bytes.
    if (!ppm.startsWith("P6"))
        return std::nullopt;
    int fields = 0;
    qsizetype at = 2;
    while (fields < 3 && at < ppm.size()) {
        while (at < ppm.size() && QChar::isSpace(uchar(ppm[at])))
            ++at;
        if (at < ppm.size() && ppm[at] == '#') {
            while (at < ppm.size() && ppm[at] != '\n')
                ++at;
            continue;
        }
        while (at < ppm.size() && !QChar::isSpace(uchar(ppm[at])))
            ++at;
        ++fields;
    }
    ++at;
    if (fields < 3 || at + 3 > ppm.size())
        return std::nullopt;
    return QColor(uchar(ppm[at]), uchar(ppm[at + 1]), uchar(ppm[at + 2]));
}
}
