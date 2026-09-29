#include "IO/PenpotImporter.h"
#include "Document/Components.h"
#include "Document/PathOperations.h"
#include "IO/PenpotImporterParts.h"
#include "IO/ZipReader.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <map>

namespace {

constexpr qint64 maximumBytes = qint64(256) << 20;
// Deeper than any real design nests; a guard beside the visited set.
constexpr int maximumDepth = 256;
// Penpot's well-known root id: the page's implicit container, holding the
// true top-level shape order in its own `shapes` array.
const QString zeroID = QStringLiteral("00000000-0000-0000-0000-000000000000");

QRectF shapeRect(const QJsonObject &shape)
{
    return QRectF(shape.value(QStringLiteral("x")).toDouble(), shape.value(QStringLiteral("y")).toDouble(),
                  shape.value(QStringLiteral("width")).toDouble(), shape.value(QStringLiteral("height")).toDouble());
}

// Rotates the shape's own box about its centre; Penpot's x/y/width/height
// are always the unrotated bounding box, rotation degrees about its middle.
// Places a shape whose geometry is built in local (0,0)-(w,h) space (rect,
// circle, frame, text, image) at its own absolute position.
QTransform shapeTransform(const QRectF &rect, double rotationDegrees)
{
    QTransform t;
    t.translate(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
    t.rotate(rotationDegrees);
    t.translate(-rect.width() / 2, -rect.height() / 2);
    return t;
}

// Rotation only, no translation: for path/bool content, whose points are
// already absolute page coordinates and so need no repositioning, only
// rotation about the shape's own centre.
QTransform rotateAboutCenter(const QRectF &rect, double rotationDegrees)
{
    QTransform t;
    const QPointF center = rect.center();
    t.translate(center.x(), center.y());
    t.rotate(rotationDegrees);
    t.translate(-center.x(), -center.y());
    return t;
}

void describeCommon(const QJsonObject &shape, VectorObject &object)
{
    object.name = shape.value(QStringLiteral("name")).toString();
    if (object.name.isEmpty())
        object.name = QStringLiteral("Layer");
    object.isVisible = !shape.value(QStringLiteral("hidden")).toBool(false);
    object.isLocked = shape.value(QStringLiteral("locked")).toBool(false);
    object.opacity = std::clamp(shape.value(QStringLiteral("opacity")).toDouble(1), 0.0, 1.0);
    object.blendMode = PenpotImport::blendModeFor(shape.value(QStringLiteral("blendMode")).toString());
}

LayoutAlign alignFor(const QString &value)
{
    if (value == QLatin1String("end"))
        return LayoutAlign::end;
    if (value == QLatin1String("center") || value.startsWith(QLatin1String("space")))
        return LayoutAlign::center;
    return LayoutAlign::start;
}

AutoLayout autoLayoutFrom(const QJsonObject &shape)
{
    AutoLayout layout;
    layout.direction =
        shape.value(QStringLiteral("layoutFlexDir")).toString().startsWith(QLatin1String("column")) ? LayoutDirection::vertical : LayoutDirection::horizontal;
    const QJsonObject gap = shape.value(QStringLiteral("layoutGap")).toObject();
    layout.gap = gap.value(QStringLiteral("rowGap")).toDouble(gap.value(QStringLiteral("columnGap")).toDouble(10));
    const QJsonObject padding = shape.value(QStringLiteral("layoutPadding")).toObject();
    layout.paddingTop = padding.value(QStringLiteral("p1")).toDouble(0);
    layout.paddingRight = padding.value(QStringLiteral("p2")).toDouble(0);
    layout.paddingBottom = padding.value(QStringLiteral("p3")).toDouble(0);
    layout.paddingLeft = padding.value(QStringLiteral("p4")).toDouble(0);
    layout.primary = alignFor(shape.value(QStringLiteral("layoutJustifyContent")).toString());
    layout.counter = alignFor(shape.value(QStringLiteral("layoutAlignItems")).toString());
    layout.wrap = shape.value(QStringLiteral("layoutWrapType")).toString() == QLatin1String("wrap");
    return layout;
}

class Builder {
public:
    QStringList warnings;
    VectorDocument document;
    ZipReader *zip = nullptr;
    QString fileID;

    void build()
    {
        const QString pagesPrefix = QStringLiteral("files/") + fileID + QStringLiteral("/pages/");
        QStringList pageEntries;
        for (const QString &entry : zip->entries()) {
            if (!entry.startsWith(pagesPrefix) || !entry.endsWith(QStringLiteral(".json")))
                continue;
            const QString remainder = entry.mid(pagesPrefix.size());
            if (!remainder.contains(QLatin1Char('/')))
                pageEntries << entry;
        }
        std::vector<QJsonObject> pages;
        for (const QString &entry : pageEntries) {
            const QJsonDocument doc = QJsonDocument::fromJson(zip->read(entry));
            if (doc.isObject())
                pages.push_back(doc.object());
        }
        std::stable_sort(pages.begin(), pages.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("index")).toInt() < b.value(QStringLiteral("index")).toInt();
        });

        // Several Penpot pages become pages; one stays the plain document.
        paged = pages.size() > 1;
        std::vector<std::pair<std::pair<int, int>, Artboard>> orderedArtboards;
        for (int p = 0; p < int(pages.size()); ++p)
            buildPage(pages[size_t(p)], pagesPrefix, p, orderedArtboards);

        // A component copy can be built before, or on an earlier page than,
        // the main component it refers to (unlike Sketch's symbolMasters, a
        // Penpot main component is an ordinary shape anywhere in the tree, so
        // there's no single "build these first" pass to make this moot).
        // Resolved now that every page's shapes are built and builtByShapeID
        // is complete.
        for (const PendingInstance &pending : pendingInstances) {
            VectorObject *object = document.find(pending.object);
            if (!object || !object->instance)
                continue;
            // A copy's componentId is the component's id, which the main shape
            // carries too; shapeRef (the main shape's id) and, in older or
            // hand-made files, a componentId that is the shape's id, are fallbacks.
            QUuid master = mainByComponentID.value(pending.componentID);
            if (master.isNull())
                master = builtByShapeID.value(pending.shapeRef);
            if (master.isNull())
                master = builtByShapeID.value(pending.componentID);
            const VectorObject *masterObject = master.isNull() ? nullptr : document.find(master);
            if (!masterObject || !masterObject->component) {
                warnings << QStringLiteral("A component instance's original component wasn't found in this file; it stayed a plain group.");
                object->instance.reset();
            } else if (!(frameByObject.value(pending.object) == frameByObject.value(master))) {
                warnings << QStringLiteral("Some component copies were resized or rotated from their main component; they stayed plain groups.");
                object->instance.reset();
            } else {
                object->instance->master = master;
                recordOverrides(pending.object, master);
            }
        }
        // Copies are rebuilt from their main component, as the app does on open;
        // doing it here keeps what the designer changed in each copy as overrides.
        Components::sync(document);

        std::stable_sort(orderedArtboards.begin(), orderedArtboards.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<Artboard> finalArtboards;
        for (auto &entry : orderedArtboards) {
            if (paged)
                entry.second.page = pageList[size_t(entry.first.first)].id;
            finalArtboards.push_back(std::move(entry.second));
        }
        if (paged) {
            document.pages = pageList;
            document.repairPageNames();
            document.currentPage = pageList.front().id;
            document.artboards = std::move(finalArtboards);
            document.size = document.artboards.front().rect.size();
            document.background = document.artboards.front().background;
        } else {
            document.setArtboards(std::move(finalArtboards));
        }
        warnings.removeDuplicates();
    }

private:
    bool paged = false;
    std::vector<Page> pageList;
    struct PendingInstance {
        QUuid object;
        QString componentID;
        QString shapeRef;
    };
    // Copies wait for a final resolve, once every main component is built.
    std::vector<PendingInstance> pendingInstances;
    QHash<QString, QUuid> builtByShapeID;
    QHash<QString, QUuid> mainByComponentID;
    // Shape ids already built: a hostile file can list a shape under itself or twice.
    QSet<QString> built;
    int depth = 0;
    bool warnedRepeat = false;
    bool warnedDepth = false;
    // A copy is rebuilt from its main at the main's size and angle, so one that differs stays a plain group.
    struct Frame {
        QSizeF size;
        double rotation = 0;
        bool operator==(const Frame &other) const
        {
            return qFuzzyCompare(size.width() + 1, other.size.width() + 1) && qFuzzyCompare(size.height() + 1, other.size.height() + 1)
                && qFuzzyCompare(rotation + 1, other.rotation + 1);
        }
    };
    QHash<QUuid, Frame> frameByObject;

    // What a copy changed from its main component, by layer name path. Layers the
    // main doesn't have, or geometry changes, can't be kept: the copy is rebuilt from the main.
    void recordOverrides(const QUuid &instanceID, const QUuid &masterID)
    {
        const auto masterKeys = Components::keys(document, masterID);
        const auto copyKeys = Components::keys(document, instanceID);
        std::map<QString, QUuid> masterByKey(masterKeys.begin(), masterKeys.end());
        InstanceInfo &info = *document.find(instanceID)->instance;
        bool lost = masterKeys.size() != copyKeys.size();
        for (const auto &[key, id] : copyKeys) {
            const auto match = masterByKey.find(key);
            if (match == masterByKey.end()) {
                lost = true;
                continue;
            }
            const VectorObject *copy = document.find(id);
            const VectorObject *main = document.find(match->second);
            InstanceOverride change;
            if (copy->kind == ObjectKind::text && main->kind == ObjectKind::text && copy->text.text != main->text.text)
                change.text = copy->text.text;
            if (copy->hasPaint() && main->hasPaint()) {
                if (copy->fill != main->fill)
                    change.fill = copy->fill;
                if (copy->stroke.paint != main->stroke.paint)
                    change.stroke = copy->stroke.paint;
            }
            if (copy->isVisible != main->isVisible)
                change.visible = copy->isVisible;
            if (!change.isEmpty())
                info.overrides[key] = change;
        }
        if (lost)
            warnings << QStringLiteral("Some component copies had layers that differ from their main component; they follow the main component.");
    }

    bool claim(const QString &shapeID)
    {
        if (shapeID == zeroID)
            return false;
        if (depth >= maximumDepth) {
            if (!warnedDepth)
                warnings << QStringLiteral("Some shapes were nested too deeply and were left out.");
            warnedDepth = true;
            return false;
        }
        if (!shapeID.isEmpty() && built.contains(shapeID)) {
            if (!warnedRepeat)
                warnings << QStringLiteral("Some shapes listed themselves or appeared twice; each was imported once.");
            warnedRepeat = true;
            return false;
        }
        built.insert(shapeID);
        return true;
    }

    QUuid add(VectorObject object, const QUuid &parent, const QString &shapeID)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent);
        if (!shapeID.isEmpty())
            builtByShapeID[shapeID] = id;
        return id;
    }

    void buildPage(const QJsonObject &page, const QString &pagesPrefix, int pageIndex,
                   std::vector<std::pair<std::pair<int, int>, Artboard>> &artboards)
    {
        const QString pageID = page.value(QStringLiteral("id")).toString();
        const QString shapesPrefix = pagesPrefix + pageID + QLatin1Char('/');
        QHash<QString, QJsonObject> shapes;
        QStringList shapeIDsInFileOrder;
        for (const QString &entry : zip->entries()) {
            if (!entry.startsWith(shapesPrefix) || !entry.endsWith(QStringLiteral(".json")))
                continue;
            const QString shapeID = entry.mid(shapesPrefix.size(), entry.size() - shapesPrefix.size() - 5);
            if (shapeID.contains(QLatin1Char('/')))
                continue;
            const QJsonDocument doc = QJsonDocument::fromJson(zip->read(entry));
            if (!doc.isObject())
                continue;
            shapes.insert(shapeID, doc.object());
            shapeIDsInFileOrder << shapeID;
        }

        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = page.value(QStringLiteral("name")).toString();
        if (layer.name.isEmpty())
            layer.name = QStringLiteral("Page");
        layer.layerColor = nextLayerColor(int(document.layers().size()));
        if (paged) {
            pageList.push_back(Page{QUuid::createUuid(), layer.name});
            layer.page = pageList.back().id;
        }
        document.objects.push_back(layer);
        const QUuid layerID = document.objects.back().id;

        QStringList topLevel;
        if (const auto root = shapes.constFind(zeroID); root != shapes.constEnd()) {
            for (const QJsonValue &value : root->value(QStringLiteral("shapes")).toArray())
                topLevel << value.toString();
        } else {
            for (const QString &id : shapeIDsInFileOrder) {
                const QString parentID = shapes.value(id).value(QStringLiteral("parentId")).toString();
                if (parentID.isEmpty() || !shapes.contains(parentID))
                    topLevel << id;
            }
        }

        // A board becomes its own artboard (see docs/import/penpot.md); shapes
        // that sit loose on the page, outside any board, are common in Penpot
        // (unlike Sketch, where nearly everything lives on an artboard) and
        // still need one to land in, so they get a synthetic one of their own
        // rather than only falling back to it when the page has no boards at all.
        int frameCount = 0;
        int firstLooseIndex = -1;
        QRectF looseBounds;
        for (int i = 0; i < topLevel.size(); ++i) {
            const QJsonObject shape = shapes.value(topLevel[i]);
            if (shape.isEmpty() || !claim(topLevel[i]))
                continue;
            if (shape.value(QStringLiteral("type")).toString() == QLatin1String("frame")) {
                ++frameCount;
                artboards.push_back({{pageIndex, i}, artboardFor(shape)});
            } else {
                if (firstLooseIndex < 0)
                    firstLooseIndex = i;
                const QRectF rect = shapeRect(shape);
                looseBounds = looseBounds.isNull() ? rect : looseBounds.united(rect);
            }
            buildItem(shape, shapes, layerID);
        }
        if (!looseBounds.isNull() || frameCount == 0) {
            Artboard board;
            board.name = layer.name;
            board.rect = looseBounds.isNull() ? QRectF(0, 0, 100, 100) : looseBounds;
            artboards.push_back({{pageIndex, std::max(firstLooseIndex, 0)}, board});
        }
    }

    Artboard artboardFor(const QJsonObject &frame)
    {
        Artboard board;
        board.name = frame.value(QStringLiteral("name")).toString();
        if (board.name.isEmpty())
            board.name = QStringLiteral("Board");
        board.rect = shapeRect(frame);
        const QJsonArray fills = frame.value(QStringLiteral("fills")).toArray();
        board.background = !fills.isEmpty() && fills.first().toObject().contains(QStringLiteral("fillColor"))
                                ? PenpotImport::color(fills.first().toObject().value(QStringLiteral("fillColor")).toString(), 1)
                                : QColor(Qt::white);
        return board;
    }

    void buildChildren(const QJsonArray &childIDs, const QHash<QString, QJsonObject> &shapes, const QUuid &parent)
    {
        for (const QJsonValue &value : childIDs) {
            const QJsonObject child = shapes.value(value.toString());
            if (child.isEmpty() || !claim(value.toString()))
                continue;
            ++depth;
            buildItem(child, shapes, parent);
            --depth;
        }
    }

    // No parent transform is composed in here: every shape's x/y/width/height
    // are its own absolute page position regardless of nesting (confirmed from
    // source, see docs/import/penpot.md), so each build* function below places
    // itself from its own rect and rotation alone.
    void buildItem(const QJsonObject &shape, const QHash<QString, QJsonObject> &shapes, const QUuid &parent)
    {
        const QString type = shape.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("frame"))
            buildFrame(shape, shapes, parent);
        else if (type == QLatin1String("group"))
            buildGroup(shape, shapes, parent);
        else if (type == QLatin1String("rect"))
            buildRect(shape, parent);
        else if (type == QLatin1String("circle"))
            buildCircle(shape, parent);
        else if (type == QLatin1String("path") || type == QLatin1String("bool"))
            buildPath(shape, parent);
        else if (type == QLatin1String("text"))
            buildText(shape, parent);
        else if (type == QLatin1String("image"))
            buildImage(shape, parent);
        else if (type == QLatin1String("svg-raw"))
            warnings << QStringLiteral("Embedded raw SVG content was left out.");
        else if (!type.isEmpty())
            warnings << QStringLiteral("Some shapes had no equivalent and were left out.");
    }

    void applyComponent(const QJsonObject &shape, VectorObject &object)
    {
        const bool isMain = shape.value(QStringLiteral("componentRoot")).toBool(false) && shape.value(QStringLiteral("mainInstance")).toBool(false);
        const QString componentID = shape.value(QStringLiteral("componentId")).toString();
        // Children are absolute page coordinates, so a component's frame is just its top-left corner.
        const QTransform placement = QTransform::fromTranslate(shapeRect(shape).x(), shapeRect(shape).y());
        if (isMain || !componentID.isEmpty())
            frameByObject[object.id] = {shapeRect(shape).size(), shape.value(QStringLiteral("rotation")).toDouble(0)};
        if (isMain) {
            ComponentInfo info;
            info.set = object.name;
            info.placement = placement;
            object.component = info;
            if (!componentID.isEmpty())
                mainByComponentID[componentID] = object.id;
        } else if (!componentID.isEmpty()) {
            // The master may not be built yet (a different page, or later in this
            // one): resolved for real once every shape exists, in build().
            InstanceInfo info;
            info.placement = placement;
            object.instance = info;
            pendingInstances.push_back({object.id, componentID, shape.value(QStringLiteral("shapeRef")).toString()});
        }
    }

    void buildFrame(const QJsonObject &shape, const QHash<QString, QJsonObject> &shapes, const QUuid &parent)
    {
        const QTransform ctm = shapeTransform(shapeRect(shape), shape.value(QStringLiteral("rotation")).toDouble(0));
        LiveRectangle live = PenpotImport::rectangleShape(shape, shapeRect(shape).size());
        live.placement = ctm;
        VectorObject object;
        object.kind = ObjectKind::frame;
        describeCommon(shape, object);
        object.shape = live;
        object.path = live.path();
        object.clipsContent = !shape.value(QStringLiteral("showContent")).toBool(false);
        PenpotImport::applyStyle(shape, object, warnings);
        // A board with no fill of its own still shows white paper, as Figma's do.
        if (!object.hasVisibleFill() && shape.value(QStringLiteral("fills")).toArray().isEmpty())
            object.fill = Paint::solid(Qt::white);
        const QString layoutKind = shape.value(QStringLiteral("layout")).toString();
        if (layoutKind == QLatin1String("flex"))
            object.autoLayout = autoLayoutFrom(shape);
        else if (layoutKind == QLatin1String("grid"))
            warnings << QStringLiteral("Grid layout was left out; its content kept its absolute position.");
        applyComponent(shape, object);
        const QUuid id = add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
        buildChildren(shape.value(QStringLiteral("shapes")).toArray(), shapes, id);
    }

    void buildGroup(const QJsonObject &shape, const QHash<QString, QJsonObject> &shapes, const QUuid &parent)
    {
        VectorObject object;
        object.kind = ObjectKind::group;
        describeCommon(shape, object);
        applyComponent(shape, object);
        const QUuid id = add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
        buildChildren(shape.value(QStringLiteral("shapes")).toArray(), shapes, id);
    }

    void buildRect(const QJsonObject &shape, const QUuid &parent)
    {
        const QTransform ctm = shapeTransform(shapeRect(shape), shape.value(QStringLiteral("rotation")).toDouble(0));
        LiveRectangle live = PenpotImport::rectangleShape(shape, shapeRect(shape).size());
        live.placement = ctm;
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(shape, object);
        object.shape = live;
        object.path = live.path();
        PenpotImport::applyStyle(shape, object, warnings);
        add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
    }

    void buildCircle(const QJsonObject &shape, const QUuid &parent)
    {
        const QTransform ctm = shapeTransform(shapeRect(shape), shape.value(QStringLiteral("rotation")).toDouble(0));
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(shape, object);
        object.path = Shapes::ellipse(QRectF(0, 0, shapeRect(shape).width(), shapeRect(shape).height())).transformed(ctm);
        PenpotImport::applyStyle(shape, object, warnings);
        add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
    }

    void buildPath(const QJsonObject &shape, const QUuid &parent)
    {
        // Rotation only: the content's points are already absolute page
        // coordinates, so (unlike rect/circle) they need no repositioning.
        const QTransform ctm = rotateAboutCenter(shapeRect(shape), shape.value(QStringLiteral("rotation")).toDouble(0));
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(shape, object);
        object.path = PenpotImport::contentGeometry(shape.value(QStringLiteral("content")).toArray()).transformed(ctm);
        PenpotImport::applyStyle(shape, object, warnings);
        add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
    }

    void buildText(const QJsonObject &shape, const QUuid &parent)
    {
        const QTransform ctm = shapeTransform(shapeRect(shape), shape.value(QStringLiteral("rotation")).toDouble(0));
        VectorObject object;
        object.kind = ObjectKind::text;
        describeCommon(shape, object);
        PenpotImport::readText(shape.value(QStringLiteral("content")).toObject(), object.text, warnings);
        object.fill = object.text.fill ? Paint::solid(*object.text.fill) : Paint::solid(Qt::black);
        object.stroke.paint = Paint::none();
        object.transform = QTransform::fromTranslate(0, object.text.size * 0.8) * ctm;
        add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
    }

    void buildImage(const QJsonObject &shape, const QUuid &parent)
    {
        const QRectF rect = shapeRect(shape);
        const QTransform ctm = shapeTransform(rect, shape.value(QStringLiteral("rotation")).toDouble(0));
        const QString mediaID = shape.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("id")).toString();
        QImage image;
        bool found = false;
        if (!mediaID.isEmpty()) {
            for (const QString &entry : zip->entries()) {
                if (entry.startsWith(QStringLiteral("objects/") + mediaID) && !entry.endsWith(QStringLiteral(".json"))) {
                    image.loadFromData(zip->read(entry));
                    found = !image.isNull();
                    break;
                }
            }
        }
        if (!found) {
            warnings << QStringLiteral("An embedded image could not be found and was left out.");
            return;
        }
        VectorObject object;
        object.kind = ObjectKind::image;
        describeCommon(shape, object);
        object.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        object.fill = Paint::none();
        object.stroke.paint = Paint::none();
        const double sx = image.width() > 0 ? rect.width() / image.width() : 1.0;
        const double sy = image.height() > 0 ? rect.height() / image.height() : 1.0;
        object.transform = QTransform::fromScale(sx, sy) * ctm;
        add(std::move(object), parent, shape.value(QStringLiteral("id")).toString());
    }
};

VectorDocument import(const QByteArray &data, QStringList *warnings)
{
    ZipReader zip(data);
    if (!zip.isValid())
        throw FileError(QStringLiteral("This is not a valid .penpot file (a zip archive)."));
    const QByteArray manifestBytes = zip.read(QStringLiteral("manifest.json"));
    if (manifestBytes.isEmpty()) {
        throw FileError(QStringLiteral("This looks like an older Penpot export this app can't read. "
                                        "Re-export it from a current Penpot (File ▸ Download ▸ Penpot file)."));
    }
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes);
    if (!manifestDoc.isObject())
        throw FileError(QStringLiteral("This .penpot file's manifest.json is not valid JSON."));
    const QJsonObject manifest = manifestDoc.object();
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty())
        throw FileError(QStringLiteral("This .penpot file's manifest lists no files."));
    if (files.size() > 1) {
        // A detached export with library files alongside the main one: only the first is imported.
    }
    const QString fileID = files.first().toObject().value(QStringLiteral("id")).toString();

    Builder builder;
    builder.zip = &zip;
    builder.fileID = fileID;
    builder.build();
    if (files.size() > 1)
        builder.warnings << QStringLiteral("This file's linked libraries were left out; only the file itself was imported.");
    if (warnings)
        *warnings = builder.warnings;
    qCInfo(lcIO) << "parsed Penpot into" << builder.document.objects.size() << "objects;" << builder.warnings.size() << "warnings";
    return std::move(builder.document);
}

}

namespace PenpotImporter {
VectorDocument parse(const QByteArray &data, QStringList *warnings)
{
    return import(data, warnings);
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to import.").arg(QFileInfo(path).fileName()));
    try {
        return import(file.readAll(), warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

bool canRead(const QByteArray &data)
{
    ZipReader zip(data);
    return zip.isValid() && zip.entries().contains(QStringLiteral("manifest.json"));
}
}
