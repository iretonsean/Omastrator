#include "Anywhere/Lift.h"
#include "Document/ImageTrace.h"
#include "Document/PathOperations.h"
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <map>

// Lift for other apps: the accessibility tree over a screenshot, or, with no
// tree, the screenshot traced.

namespace {
const char *treeHelper = R"py(import json, sys, time
try:
    import gi
    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi
except Exception as failure:
    print(json.dumps({"error": "AT-SPI isn't available: %s" % failure}))
    sys.exit(0)

pid, limit = int(sys.argv[1]), int(sys.argv[2])
WINDOW = Atspi.CoordType.WINDOW
deadline = time.time() + 4
count = 0


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


def color(value):
    parts = [int(part) for part in str(value).split(",") if part.strip().isdigit()]
    if len(parts) < 3:
        return ""
    scale = 257 if max(parts[:3]) > 255 else 1
    return "#%02x%02x%02x" % tuple(part // scale for part in parts[:3])


def read(node, depth):
    global count
    if count >= limit or time.time() > deadline or depth > 40:
        return None
    try:
        states = node.get_state_set()
        if not states.contains(Atspi.StateType.SHOWING):
            return None
        box = Atspi.Component.get_extents(node, WINDOW)
        if box.width <= 0 or box.height <= 0:
            return None
        count += 1
        out = {"role": node.get_role_name(), "name": node.get_name() or "", "rect": [box.x, box.y, box.width, box.height], "index": node.get_index_in_parent()}
    except Exception:
        return None
    try:
        length = Atspi.Text.get_character_count(node)
        if length > 0:
            out["text"] = Atspi.Text.get_text(node, 0, min(length, 2000))
            extents = Atspi.Text.get_range_extents(node, 0, min(length, 2000), WINDOW)
            out["textRect"] = [extents.x, extents.y, extents.width, extents.height]
            attributes = Atspi.Text.get_default_attributes(node) or {}
            out["fontFamily"] = attributes.get("family-name", "")
            out["fontSize"] = attributes.get("size", "")
            out["fontWeight"] = attributes.get("weight", "")
            out["color"] = color(attributes.get("fg-color", ""))
    except Exception:
        pass
    children = []
    try:
        for index in range(min(node.get_child_count(), 400)):
            child = node.get_child_at_index(index)
            if child is None:
                continue
            made = read(child, depth + 1)
            if made:
                children.append(made)
    except Exception:
        pass
    if children:
        out["children"] = children
    return out


app = app_for(pid)
if app is None:
    print(json.dumps({"error": "This app has no accessibility tree."}))
    sys.exit(0)
best = None
for index in range(app.get_child_count()):
    frame = app.get_child_at_index(index)
    try:
        if frame is not None and frame.get_state_set().contains(Atspi.StateType.ACTIVE):
            best = frame
            break
        if best is None and frame is not None and frame.get_state_set().contains(Atspi.StateType.SHOWING):
            best = frame
    except Exception:
        pass
root = read(best, 0) if best is not None else None
if root is None:
    print(json.dumps({"error": "This app's window has nothing accessible."}))
    sys.exit(0)
print(json.dumps({"app": app.get_name() or "", "root": root, "truncated": count >= limit or time.time() > deadline}))
)py";

QRectF rectOf(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return QRectF(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble());
}

// The colour most of a crop is: a widget's background.
QColor commonColor(const QImage &image, bool *flat = nullptr)
{
    std::map<QRgb, int> counts;
    const int step = std::max(1, int(std::sqrt(double(image.width()) * image.height() / 4000)));
    int total = 0;
    for (int y = 0; y < image.height(); y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += step) {
            // Near colours count together, so antialiasing doesn't split a flat fill.
            ++counts[line[x] & 0xfff0f0f0];
            ++total;
        }
    }
    QRgb best = 0;
    int most = 0;
    for (const auto &[rgb, count] : counts) {
        if (count > most) {
            most = count;
            best = rgb;
        }
    }
    if (flat)
        *flat = total > 0 && most >= total * 0.97;
    QColor color = QColor::fromRgba(best | 0xff000000);
    return color;
}

struct Builder {
    const QImage &shot;
    double scale;
    QRect region;
    Lift::Result &result;
    int done = 0;
    int total = 0;
    const std::function<bool(int, int)> &progress;
    bool stopped = false;

    QImage crop(const QRectF &rect) const
    {
        return shot.copy(QRectF(rect.topLeft() * scale, rect.size() * scale).toAlignedRect().intersected(shot.rect()));
    }

    static int count(const QJsonObject &node)
    {
        int n = 1;
        for (const QJsonValue &child : node["children"].toArray())
            n += count(child.toObject());
        return n;
    }

    // One accessible object: a group when it holds others, with a rectangle in its colour where that differs from its parent's.
    void add(const QJsonObject &node, const QUuid &parent, const QColor &behind, const QString &path)
    {
        if (stopped)
            return;
        if (progress && ++done % 40 == 0 && !progress(done, total)) {
            stopped = true;
            return;
        }
        QRectF rect = rectOf(node["rect"]);
        if (!region.isEmpty())
            rect = rect.intersected(region);
        if (rect.width() < 1 || rect.height() < 1)
            return;
        const QString role = node["role"].toString();
        const QString name = node["name"].toString().left(40);
        const QString label = name.isEmpty() ? role : QStringLiteral("%1 “%2”").arg(role, name);
        const QString here = path + QLatin1Char('/') + role + QLatin1Char('[') + QString::number(node["index"].toInt()) + QLatin1Char(']');
        const QJsonArray children = node["children"].toArray();
        const QImage pixels = crop(rect);
        bool flat = false;
        const QColor color = pixels.isNull() ? behind : commonColor(pixels, &flat);

        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = label;
        group.liftedFrom = here;
        group.isExpanded = parent == result.art.layers().front();
        const QUuid groupId = group.id;
        result.art.insert(std::move(group), parent);
        if (result.root.isNull())
            result.root = groupId;

        if (color != behind || parent == result.art.layers().front()) {
            VectorObject box;
            box.name = QStringLiteral("Background");
            box.liftedFrom = here;
            LiveRectangle shape;
            shape.rect = rect;
            box.shape = shape;
            box.path = shape.path();
            box.fill = Paint::solid(color);
            box.stroke.paint = Paint::none();
            box.stroke.width = 0;
            result.art.insert(std::move(box), groupId);
        }
        const QString text = node["text"].toString().trimmed();
        if (children.isEmpty() && text.isEmpty() && !flat && !pixels.isNull()) {
            // An icon or a picture the tree can't describe: its pixels.
            VectorObject image;
            image.kind = ObjectKind::image;
            image.name = QStringLiteral("Picture");
            image.liftedFrom = here;
            image.image = pixels.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            image.transform = QTransform::fromScale(rect.width() / pixels.width(), rect.height() / pixels.height())
                              * QTransform::fromTranslate(rect.left(), rect.top());
            result.art.insert(std::move(image), groupId);
        }
        if (!text.isEmpty() && !text.contains(QLatin1Char('\n'))) {
            QRectF box = node["textRect"].isArray() ? rectOf(node["textRect"]) : QRectF();
            if (box.width() < 1 || box.height() < 1)
                box = rect;
            VectorObject words;
            words.kind = ObjectKind::text;
            words.name = QStringLiteral("Text");
            words.liftedFrom = here;
            TextContent &content = words.text;
            content.text = text;
            const QString family = node["fontFamily"].toString();
            if (!family.isEmpty())
                content.family = family;
            // AT-SPI sizes are points; the screen is 96 per inch.
            const double points = node["fontSize"].toVariant().toString().toDouble();
            content.size = points > 0 ? points * 96 / 72 : std::clamp(box.height() * 0.72, 8.0, 48.0);
            content.style = TextContent::styleFor(content.family, node["fontWeight"].toVariant().toString().toInt() > 0
                                                                      ? node["fontWeight"].toVariant().toString().toInt()
                                                                      : 400,
                                                  false);
            const QColor ink = QColor::fromString(node["color"].toString());
            words.fill = Paint::solid(ink.isValid() ? ink : (qGray(color.rgb()) > 128 ? QColor(Qt::black) : QColor(Qt::white)));
            words.stroke.paint = Paint::none();
            words.stroke.width = 0;
            const QFontMetricsF metrics(content.character().font(1));
            const double full = metrics.ascent() + metrics.descent();
            const double baseline = box.top() + (box.height() - full) / 2 + metrics.ascent();
            words.transform = QTransform::fromTranslate(box.left(), baseline);
            result.art.insert(std::move(words), groupId);
        }
        for (const QJsonValue &child : children)
            add(child.toObject(), groupId, color, here);
    }
};
}

namespace Lift {
QString accessibleTreeScript()
{
    return QString::fromUtf8(treeHelper);
}

std::optional<QJsonObject> accessibleTree(qint64 pid, int maxNodes, QString *error)
{
    QString program = qEnvironmentVariable("OMASTRATOR_ATSPI_TREE");
    QStringList args;
    if (program.isEmpty()) {
        program = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (program.isEmpty()) {
            if (error)
                *error = QStringLiteral("python3 isn't installed, so the accessibility tree can't be read.");
            return std::nullopt;
        }
        args << QStringLiteral("-c") << accessibleTreeScript();
    }
    args << QString::number(pid) << QString::number(maxNodes);
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(3000) || !process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(500);
        if (error)
            *error = QStringLiteral("The accessibility tree took too long.");
        return std::nullopt;
    }
    const QJsonObject answer = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (answer.isEmpty() || answer.contains("error") || !answer["root"].isObject()) {
        if (error)
            *error = answer["error"].toString(QStringLiteral("The accessibility tree couldn't be read."));
        return std::nullopt;
    }
    return answer;
}

std::optional<Result> fromAccessible(const QJsonObject &tree, const QImage &screenshot, const QSize &windowSize, const QRect &region,
                                     const QString &label, QString *error, const std::function<bool(int, int)> &progress)
{
    const QJsonObject root = tree["root"].toObject();
    // A tree of one bare frame says nothing a screenshot doesn't: trace instead.
    if (root.isEmpty() || root["children"].toArray().isEmpty()) {
        if (error)
            *error = QStringLiteral("This app's window has nothing accessible.");
        return std::nullopt;
    }
    Result result;
    result.method = QStringLiteral("accessibility");
    result.art = VectorDocument::blank(QSizeF(1, 1));
    result.art.background = Qt::transparent;
    const double scale = windowSize.width() > 0 && !screenshot.isNull() ? double(screenshot.width()) / windowSize.width() : 1;
    const QImage pixels = screenshot.convertToFormat(QImage::Format_ARGB32);
    Builder builder{pixels, scale, region, result, 0, 0, progress};
    builder.total = Builder::count(root);
    builder.add(root, result.art.layers().front(), QColor(), QString());
    if (builder.stopped) {
        if (error)
            *error = QStringLiteral("Cancelled.");
        return std::nullopt;
    }
    if (result.root.isNull()) {
        if (error)
            *error = QStringLiteral("There's nothing there to lift.");
        return std::nullopt;
    }
    result.art.find(result.root)->name = label;
    result.name = label;
    result.objects = int(result.art.descendants(result.root).size());
    if (tree["truncated"].toBool())
        result.notes << QStringLiteral("The app's tree is large; only part of it was lifted.");
    const QRectF all = result.art.bounds(result.root, true);
    result.art.size = QSizeF(std::max(1.0, all.right()), std::max(1.0, all.bottom()));
    return result;
}

std::optional<Result> fromTrace(const QImage &screenshot, const QRectF &region, const QString &label, QString *error)
{
    if (screenshot.isNull() || region.isEmpty()) {
        if (error)
            *error = QStringLiteral("The screen couldn't be captured. Is grim installed? sudo pacman -S grim");
        return std::nullopt;
    }
    // Traced at no more than 1,400 pixels a side, which keeps it to seconds.
    QImage image = screenshot;
    if (std::max(image.width(), image.height()) > 1400)
        image = image.scaled(QSize(1400, 1400), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ImageTrace::Options options;
    options.colors = 8;
    options.ignoreWhite = false;
    options.minimumArea = 6;
    options.smoothness = 1;
    std::vector<VectorObject> paths = ImageTrace::trace(image, options);
    if (paths.empty()) {
        if (error)
            *error = QStringLiteral("Tracing found no shapes there.");
        return std::nullopt;
    }
    Result result;
    result.method = QStringLiteral("trace");
    result.art = VectorDocument::blank(QSizeF(1, 1));
    result.art.background = Qt::transparent;
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = label;
    group.liftedFrom = QStringLiteral("trace");
    result.root = group.id;
    result.art.insert(std::move(group), result.art.layers().front());
    const QTransform place = QTransform::fromScale(region.width() / image.width(), region.height() / image.height())
                             * QTransform::fromTranslate(region.left(), region.top());
    for (VectorObject &path : paths) {
        path.path = path.path.transformed(place);
        path.liftedFrom = QStringLiteral("trace");
        result.art.insert(std::move(path), result.root);
    }
    result.name = label;
    result.objects = int(result.art.descendants(result.root).size());
    result.notes << QStringLiteral("This app has no accessibility tree, so it was traced from the screen.");
    const QRectF all = result.art.bounds(result.root, true);
    result.art.size = QSizeF(std::max(1.0, all.right()), std::max(1.0, all.bottom()));
    return result;
}
}
