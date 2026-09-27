#include "Anywhere/LiftDiff.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace {
// Parts Lift makes around an element's own box and content, which no CSS property maps to directly.
bool isDecoration(const QString &name)
{
    static const QStringList names{"Shadow", "Border", "Border Top", "Border Right", "Border Bottom", "Border Left", "Highlight",
                                   "Clipping Path", "Page background"};
    return names.contains(name);
}

QJsonArray rectJson(const QRectF &rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

QRectF rectOf(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    return array.size() == 4 ? QRectF(array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble(), array.at(3).toDouble()) : QRectF();
}

QString solid(const Paint &paint)
{
    return paint.kind == PaintKind::solid ? LiftDiff::cssColor(paint.color) : QString();
}

// The size text draws at: its own size times the object's scale.
double drawnSize(const VectorObject &object)
{
    return object.text.size * std::sqrt(std::abs(object.transform.determinant()));
}

// The element's box and what sits inside it, for a group Lift made from one element.
struct Box {
    QUuid box;
    QRectF boxRect;
    QRectF content;
};

std::optional<Box> boxOf(const VectorDocument &document, const VectorObject &group)
{
    Box found;
    for (const QUuid &child : document.children(group.id)) {
        const VectorObject *object = document.find(child);
        if (!object)
            continue;
        if (object->kind == ObjectKind::path && object->liftedFrom == group.liftedFrom && object->name == QLatin1String("Background") && found.box.isNull()) {
            found.box = child;
            found.boxRect = document.bounds(child);
        } else if (!isDecoration(object->name)) {
            found.content = found.content.isNull() ? document.bounds(child) : found.content.united(document.bounds(child));
        }
    }
    if (found.box.isNull())
        return std::nullopt;
    return found;
}

QString px(double value)
{
    return QStringLiteral("%1px").arg(std::round(value * 100) / 100);
}

QString radiusText(const std::array<double, 4> &radii)
{
    if (radii[0] == radii[1] && radii[1] == radii[2] && radii[2] == radii[3])
        return px(radii[0]);
    return QStringLiteral("%1 %2 %3 %4").arg(px(radii[0]), px(radii[1]), px(radii[2]), px(radii[3]));
}

QJsonArray radiiJson(const std::array<double, 4> &radii)
{
    return {radii[0], radii[1], radii[2], radii[3]};
}

bool differs(double a, double b)
{
    return std::abs(a - b) >= 0.5;
}
}

namespace LiftDiff {
QString cssColor(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name();
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(std::round(color.alphaF() * 1000) / 1000);
}

QJsonObject snapshot(const VectorDocument &document, const QUuid &root)
{
    QJsonObject all;
    std::vector<QUuid> ids = document.descendants(root);
    ids.insert(ids.begin(), root);
    for (const QUuid &id : ids) {
        const VectorObject *object = document.find(id);
        if (!object || object->liftedFrom.isEmpty())
            continue;
        QJsonObject shot{{"selector", object->liftedFrom}, {"name", object->name}, {"kind", rawValue(object->kind)},
                         {"bounds", rectJson(document.bounds(id))}};
        if (object->kind == ObjectKind::text) {
            shot["text"] = object->text.text;
            shot["fill"] = solid(object->fill);
            shot["size"] = drawnSize(*object);
            shot["style"] = object->text.style;
        } else if (object->kind == ObjectKind::path) {
            shot["fill"] = solid(object->fill);
            if (const LiveRectangle *shape = object->liveShape())
                shot["radii"] = radiiJson(shape->radii);
        } else if (object->kind == ObjectKind::group) {
            if (const auto box = boxOf(document, *object)) {
                shot["box"] = rectJson(box->boxRect);
                shot["content"] = rectJson(box->content);
            }
        }
        all[id.toString(QUuid::WithoutBraces)] = shot;
    }
    return all;
}

std::vector<Change> changes(const VectorDocument &document, const std::vector<QUuid> &roots, const QJsonObject &baseline, QStringList *notes)
{
    std::vector<Change> found;
    auto add = [&](const QString &selector, const QString &property, const QString &value, const QString &was) {
        found.push_back({selector, property, value, false, 0, QStringLiteral("%1: %2 %3 → %4").arg(selector, property, was, value)});
    };
    auto addRelative = [&](const QString &selector, const QString &property, double delta) {
        found.push_back({selector, property, QString(), true, delta,
                         QStringLiteral("%1: %2 %3%4").arg(selector, property, delta > 0 ? QStringLiteral("+") : QString(), px(delta))});
    };
    auto note = [&](const QString &line) {
        if (notes && !notes->contains(line))
            notes->append(line);
    };
    std::vector<QUuid> ids;
    for (const QUuid &root : roots) {
        ids.push_back(root);
        for (const QUuid &id : document.descendants(root))
            ids.push_back(id);
    }
    for (const QUuid &id : ids) {
        const VectorObject *object = document.find(id);
        if (!object || object->liftedFrom.isEmpty())
            continue;
        const QJsonObject was = baseline[id.toString(QUuid::WithoutBraces)].toObject();
        if (was.isEmpty() || was["selector"].toString() != object->liftedFrom)
            continue;
        const QString selector = object->liftedFrom;
        if (object->kind == ObjectKind::text) {
            if (object->text.text != was["text"].toString()) {
                // The page broke the lines; the element takes the words, and breaks them again itself.
                QString text = object->text.text;
                text.replace(QLatin1Char('\n'), QLatin1Char(' '));
                add(selector, QStringLiteral("text"), text.simplified(), QStringLiteral("“%1”").arg(was["text"].toString().simplified()));
            }
            const QString fill = solid(object->fill);
            if (!fill.isEmpty() && fill != was["fill"].toString())
                add(selector, QStringLiteral("color"), fill, was["fill"].toString());
            if (differs(drawnSize(*object), was["size"].toDouble()))
                add(selector, QStringLiteral("font-size"), px(drawnSize(*object)), px(was["size"].toDouble()));
            if (object->text.style != was["style"].toString()) {
                const int weight = QFontDatabase::weight(object->text.family, object->text.style);
                if (weight > 0)
                    add(selector, QStringLiteral("font-weight"), QString::number(weight), was["style"].toString());
            }
        } else if (object->kind == ObjectKind::path && !isDecoration(object->name)) {
            const QString fill = solid(object->fill);
            if (fill != was["fill"].toString()) {
                if (fill.isEmpty())
                    note(QStringLiteral("%1: only a flat colour can be applied as its background.").arg(selector));
                else
                    add(selector, QStringLiteral("background-color"), fill, was["fill"].toString().isEmpty() ? QStringLiteral("none") : was["fill"].toString());
            }
            const QJsonArray radii = was["radii"].toArray();
            if (const LiveRectangle *shape = object->liveShape(); shape && radii.size() == 4) {
                bool changed = false;
                std::array<double, 4> before{};
                for (int i = 0; i < 4; ++i) {
                    before[size_t(i)] = radii.at(i).toDouble();
                    changed = changed || differs(shape->radii[size_t(i)], before[size_t(i)]);
                }
                if (changed)
                    add(selector, QStringLiteral("border-radius"), radiusText(shape->radii), radiusText(before));
            }
            // A box on its own (nothing inside it) is sized directly; one around content is spaced (below).
            const VectorObject *parent = object->parentID ? document.find(*object->parentID) : nullptr;
            const bool inGroup = parent && parent->kind == ObjectKind::group && parent->liftedFrom == selector && object->name == QLatin1String("Background")
                                 && boxOf(document, *parent) && !boxOf(document, *parent)->content.isNull();
            if (!inGroup) {
                const QRectF now = document.bounds(id), then = rectOf(was["bounds"]);
                if (differs(now.width(), then.width()))
                    addRelative(selector, QStringLiteral("width"), now.width() - then.width());
                if (differs(now.height(), then.height()))
                    addRelative(selector, QStringLiteral("height"), now.height() - then.height());
            }
        } else if (object->kind == ObjectKind::group && was.contains("box")) {
            const auto box = boxOf(document, *object);
            const QRectF thenBox = rectOf(was["box"]), thenContent = rectOf(was["content"]);
            if (!box || box->content.isNull() || thenContent.isNull())
                continue;
            // The box's edges moved around the content: its padding changed by as much. Content that grew (longer
            // text, a bigger size) only counts where the box's own edge moved too.
            const QRectF &nowBox = box->boxRect, &nowContent = box->content;
            const bool resized = differs(nowContent.width(), thenContent.width()) || differs(nowContent.height(), thenContent.height());
            auto side = [&](double boxOut, double contentOut) { return resized ? (differs(boxOut, 0) ? boxOut : 0.0) : boxOut - contentOut; };
            const double top = side(thenBox.top() - nowBox.top(), thenContent.top() - nowContent.top());
            const double right = side(nowBox.right() - thenBox.right(), nowContent.right() - thenContent.right());
            const double bottom = side(nowBox.bottom() - thenBox.bottom(), nowContent.bottom() - thenContent.bottom());
            const double left = side(thenBox.left() - nowBox.left(), thenContent.left() - nowContent.left());
            const std::array<std::pair<const char *, double>, 4> sides{{{"padding-top", top}, {"padding-right", right}, {"padding-bottom", bottom},
                                                                         {"padding-left", left}}};
            for (const auto &[property, delta] : sides)
                if (differs(delta, 0))
                    addRelative(selector, QLatin1String(property), delta);
            const QRectF nowAll = box->boxRect.united(box->content), thenAll = thenBox.united(thenContent);
            if ((differs(nowAll.left(), thenAll.left()) || differs(nowAll.top(), thenAll.top())) && !differs(nowAll.width(), thenAll.width())
                && !differs(nowAll.height(), thenAll.height()))
                note(QStringLiteral("%1 was moved; moves aren't applied to the page. Hand it to the agent for layout changes.").arg(selector));
        }
    }
    return found;
}

QJsonObject readBaselines(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QString writeBaselines(const QString &path, const QJsonObject &baselines)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(baselines).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Couldn't save %1: %2").arg(path, file.errorString());
    return {};
}
}
