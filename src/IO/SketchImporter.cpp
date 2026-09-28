#include "IO/SketchImporter.h"
#include "Document/Components.h"
#include "Document/DesignTokens.h"
#include "IO/SketchImporterParts.h"
#include "IO/ZipReader.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {

constexpr qint64 maximumBytes = qint64(256) << 20;

QRectF frameRect(const QJsonObject &layer)
{
    const QJsonObject frame = layer.value(QStringLiteral("frame")).toObject();
    return QRectF(frame.value(QStringLiteral("x")).toDouble(), frame.value(QStringLiteral("y")).toDouble(),
                  frame.value(QStringLiteral("width")).toDouble(), frame.value(QStringLiteral("height")).toDouble());
}

// Rotates the layer's own (0,0)-(w,h) box about its centre, then places that
// centre within its parent's local space. Not verified against a rotated
// fixture rendered by Sketch itself; see docs/import/sketch.md.
QTransform layerTransform(const QRectF &frame, double rotationDegrees)
{
    QTransform t;
    t.translate(frame.x() + frame.width() / 2, frame.y() + frame.height() / 2);
    t.rotate(rotationDegrees);
    t.translate(-frame.width() / 2, -frame.height() / 2);
    return t;
}

void describeCommon(const QJsonObject &layer, VectorObject &object)
{
    object.name = layer.value(QStringLiteral("name")).toString();
    if (object.name.isEmpty())
        object.name = QStringLiteral("Layer");
    object.isVisible = layer.value(QStringLiteral("isVisible")).toBool(true);
    object.isLocked = layer.value(QStringLiteral("isLocked")).toBool(false);
}

class Builder {
public:
    QStringList warnings;
    VectorDocument document;
    QHash<QString, QByteArray> images;

    void build(const std::vector<QJsonObject> &orderedPages, const QJsonObject &documentJson)
    {
        registerColorAssets(documentJson);

        struct PageInfo {
            QJsonObject json;
            QUuid layerID;
            double offsetX = 0;
        };
        std::vector<PageInfo> pages;
        double nextX = 0;
        for (const QJsonObject &pageJson : orderedPages) {
            PageInfo info;
            info.json = pageJson;
            info.offsetX = nextX;
            nextX = info.offsetX + std::max(100.0, contentBounds(pageJson).width()) + 200;

            VectorObject layer;
            layer.kind = ObjectKind::layer;
            layer.name = pageJson.value(QStringLiteral("name")).toString();
            if (layer.name.isEmpty())
                layer.name = QStringLiteral("Page");
            layer.layerColor = nextLayerColor(int(document.layers().size()));
            document.objects.push_back(layer);
            info.layerID = document.objects.back().id;
            pages.push_back(info);
        }

        // Symbol masters first, wherever their page falls in file order, so an
        // instance on a page processed later (or earlier) always finds its master.
        // Each artboard remembers its (page, layer) file position so the final
        // artboard order follows the file, not this build order.
        for (int p = 0; p < int(pages.size()); ++p) {
            const QJsonArray layers = pages[size_t(p)].json.value(QStringLiteral("layers")).toArray();
            for (int i = 0; i < layers.size(); ++i) {
                const QJsonObject layer = layers[i].toObject();
                if (layer.value(QStringLiteral("_class")).toString() == QLatin1String("symbolMaster"))
                    buildSymbolMaster(layer, pages[size_t(p)].layerID, pages[size_t(p)].offsetX, {p, i});
            }
        }

        for (int p = 0; p < int(pages.size()); ++p) {
            const PageInfo &page = pages[size_t(p)];
            const QJsonArray layers = page.json.value(QStringLiteral("layers")).toArray();
            bool anyArtboard = false;
            for (int i = 0; i < layers.size(); ++i) {
                const QJsonObject layer = layers[i].toObject();
                const QString cls = layer.value(QStringLiteral("_class")).toString();
                if (cls == QLatin1String("symbolMaster")) {
                    anyArtboard = true; // already built above
                } else if (cls == QLatin1String("artboard")) {
                    anyArtboard = true;
                    buildArtboard(layer, page.layerID, page.offsetX, {p, i});
                } else {
                    buildItem(layer, page.layerID, QTransform::fromTranslate(page.offsetX, 0), nullptr);
                }
            }
            if (!anyArtboard) {
                Artboard board;
                board.name = document.find(page.layerID)->name;
                QRectF bounds = contentBounds(page.json).translated(page.offsetX, 0);
                board.rect = bounds.isEmpty() ? QRectF(page.offsetX, 0, 100, 100) : bounds;
                artboards.push_back({{p, 0}, board});
            }
        }

        Components::sync(document);
        // setArtboards(), not a direct push: it's what sets document.size/background
        // to the first artboard's, which every reader of a one-page document relies on.
        // Sorted back into file order: symbol masters were built first (above) so
        // instances can resolve them, regardless of which page they live on.
        std::stable_sort(artboards.begin(), artboards.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<Artboard> ordered;
        for (auto &entry : artboards)
            ordered.push_back(std::move(entry.second));
        document.setArtboards(std::move(ordered));
        warnings.removeDuplicates();
    }

private:
    // Each artboard's file position (page index, layer index), so the final list
    // can be sorted back into file order after symbol masters build out of turn.
    std::vector<std::pair<std::pair<int, int>, Artboard>> artboards;
    QHash<QString, QUuid> masterByID;
    QHash<QString, QHash<QString, QString>> masterOverrideKeys;
    QHash<QString, QSizeF> masterSize;

    QUuid add(VectorObject object, const QUuid &parent)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent);
        return id;
    }

    void registerColorAssets(const QJsonObject &documentJson)
    {
        const QJsonObject assets = documentJson.value(QStringLiteral("assets")).toObject();
        for (const QJsonValue &value : assets.value(QStringLiteral("colors")).toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            document.tokens.push_back(DesignToken::color(name, SketchImport::color(asset.value(QStringLiteral("color")).toObject())));
        }
    }

    static QRectF contentBounds(const QJsonObject &pageJson)
    {
        QRectF bounds;
        for (const QJsonValue &value : pageJson.value(QStringLiteral("layers")).toArray()) {
            const QRectF rect = frameRect(value.toObject());
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        }
        return bounds;
    }

    // A layer with hasClippingMask clips every layer that follows it, up to
    // (not including) the next clipping-mask layer or the end of the list --
    // Sketch's own mask semantics, not this app's "first child clips" one.
    void buildChildren(const QJsonArray &layers, const QUuid &parent, const QTransform &ctm, QHash<QString, QUuid> *idMap)
    {
        for (int i = 0; i < layers.size(); ++i) {
            const QJsonObject layer = layers[i].toObject();
            if (!layer.value(QStringLiteral("hasClippingMask")).toBool(false)) {
                buildItem(layer, parent, ctm, idMap);
                continue;
            }
            VectorObject clipGroup;
            clipGroup.kind = ObjectKind::group;
            clipGroup.isClipGroup = true;
            clipGroup.name = QStringLiteral("Clip Group");
            const QUuid clipID = add(std::move(clipGroup), parent);
            buildItem(layer, clipID, ctm, idMap);
            int j = i + 1;
            for (; j < layers.size(); ++j) {
                const QJsonObject next = layers[j].toObject();
                if (next.value(QStringLiteral("hasClippingMask")).toBool(false))
                    break;
                buildItem(next, clipID, ctm, idMap);
            }
            i = j - 1;
        }
    }

    void buildArtboard(const QJsonObject &artboard, const QUuid &layerID, double offsetX, std::pair<int, int> fileOrder)
    {
        const QRectF rect = frameRect(artboard).translated(offsetX, 0);
        Artboard board;
        board.name = artboard.value(QStringLiteral("name")).toString();
        if (board.name.isEmpty())
            board.name = QStringLiteral("Artboard");
        board.background = artboard.value(QStringLiteral("hasBackgroundColor")).toBool(false)
                                ? SketchImport::color(artboard.value(QStringLiteral("backgroundColor")).toObject())
                                : QColor(Qt::white);
        board.rect = rect;
        artboards.push_back({fileOrder, board});
        buildChildren(artboard.value(QStringLiteral("layers")).toArray(), layerID, QTransform::fromTranslate(rect.left(), rect.top()), nullptr);
    }

    void buildSymbolMaster(const QJsonObject &master, const QUuid &layerID, double offsetX, std::pair<int, int> fileOrder)
    {
        const QRectF rect = frameRect(master).translated(offsetX, 0);
        Artboard board;
        board.name = master.value(QStringLiteral("name")).toString();
        if (board.name.isEmpty())
            board.name = QStringLiteral("Symbol");
        board.rect = rect;
        artboards.push_back({fileOrder, board});

        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = board.name;
        ComponentInfo info;
        info.set = board.name;
        info.placement = QTransform::fromTranslate(rect.left(), rect.top());
        group.component = info;
        const QUuid groupID = add(std::move(group), layerID);

        QHash<QString, QUuid> sketchIdToBuiltId;
        buildChildren(master.value(QStringLiteral("layers")).toArray(), groupID, QTransform::fromTranslate(rect.left(), rect.top()), &sketchIdToBuiltId);

        QHash<QUuid, QString> builtIdToSketchId;
        for (auto it = sketchIdToBuiltId.constBegin(); it != sketchIdToBuiltId.constEnd(); ++it)
            builtIdToSketchId[it.value()] = it.key();
        QHash<QString, QString> keys;
        for (const auto &[key, id] : Components::keys(document, groupID)) {
            const auto found = builtIdToSketchId.constFind(id);
            if (found != builtIdToSketchId.constEnd())
                keys[found.value()] = key;
        }
        const QString symbolID = master.value(QStringLiteral("symbolID")).toString();
        masterByID[symbolID] = groupID;
        masterOverrideKeys[symbolID] = keys;
        masterSize[symbolID] = rect.size();
    }

    void buildSymbolInstance(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        VectorObject group;
        group.kind = ObjectKind::group;
        describeCommon(layer, group);
        const QString symbolID = layer.value(QStringLiteral("symbolID")).toString();
        if (!masterByID.contains(symbolID)) {
            warnings << QStringLiteral("A symbol instance's original symbol is missing; it was left empty.");
            const QUuid id = add(std::move(group), parent);
            if (idMap)
                (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
            return;
        }
        InstanceInfo info;
        info.master = masterByID.value(symbolID);
        info.placement = ctm;
        const QSizeF fromSize = masterSize.value(symbolID);
        if (fromSize.width() > 0 && fromSize.height() > 0
            && (std::abs(frame.width() - fromSize.width()) > 0.5 || std::abs(frame.height() - fromSize.height()) > 0.5))
            warnings << QStringLiteral("Symbol instances resized from their master keep the master's own size.");
        const QHash<QString, QString> keys = masterOverrideKeys.value(symbolID);
        bool droppedOverride = false;
        for (const QJsonValue &value : layer.value(QStringLiteral("overrideValues")).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString overrideName = entry.value(QStringLiteral("overrideName")).toString();
            static const QString suffix = QStringLiteral("_stringValue");
            if (!overrideName.endsWith(suffix)) {
                droppedOverride = true;
                continue;
            }
            const QString idChain = overrideName.left(overrideName.size() - suffix.size());
            const QString key = keys.value(idChain.split(QLatin1Char('/')).last());
            if (key.isEmpty()) {
                droppedOverride = true;
                continue;
            }
            InstanceOverride change;
            change.text = entry.value(QStringLiteral("value")).toString();
            info.overrides[key] = change;
        }
        if (droppedOverride)
            warnings << QStringLiteral("Some symbol overrides (fills, images, text styles) have no equivalent and were left out.");
        group.instance = info;
        const QUuid id = add(std::move(group), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }

    void buildItem(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QString cls = layer.value(QStringLiteral("_class")).toString();
        if (cls == QLatin1String("group"))
            buildGroup(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("shapeGroup"))
            buildShapeGroup(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("rectangle"))
            buildRectangle(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("oval") || cls == QLatin1String("polygon") || cls == QLatin1String("star") || cls == QLatin1String("shapePath"))
            buildShapePath(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("text"))
            buildText(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("bitmap"))
            buildBitmap(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("symbolInstance"))
            buildSymbolInstance(layer, parent, parentCTM, idMap);
        else if (cls == QLatin1String("slice"))
            warnings << QStringLiteral("Slices were left out.");
        else if (!cls.isEmpty())
            warnings << QStringLiteral("Some layers had no equivalent and were left out.");
    }

    void buildGroup(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        VectorObject group;
        group.kind = ObjectKind::group;
        describeCommon(layer, group);
        const QTransform ctm = layerTransform(frameRect(layer), layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        const QUuid id = add(std::move(group), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
        buildChildren(layer.value(QStringLiteral("layers")).toArray(), id, ctm, idMap);
    }

    void buildShapeGroup(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(layer, object);
        object.path = SketchImport::booleanGroupGeometry(layer.value(QStringLiteral("layers")).toArray(), warnings).transformed(ctm);
        SketchImport::applyStyle(layer.value(QStringLiteral("style")).toObject(), object, warnings);
        const QUuid id = add(std::move(object), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }

    void buildRectangle(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        LiveRectangle shape = SketchImport::rectangleShape(layer, frame.size());
        shape.placement = ctm;
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(layer, object);
        object.shape = shape;
        object.path = shape.path();
        SketchImport::applyStyle(layer.value(QStringLiteral("style")).toObject(), object, warnings);
        const QUuid id = add(std::move(object), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }

    void buildShapePath(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        VectorObject object;
        object.kind = ObjectKind::path;
        describeCommon(layer, object);
        object.path = SketchImport::shapePathGeometry(layer, frame.size(), warnings).transformed(ctm);
        SketchImport::applyStyle(layer.value(QStringLiteral("style")).toObject(), object, warnings);
        const QUuid id = add(std::move(object), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }

    void buildText(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        VectorObject object;
        object.kind = ObjectKind::text;
        describeCommon(layer, object);
        SketchImport::readText(layer, object.text, warnings);
        object.fill = object.text.fill ? Paint::solid(*object.text.fill) : Paint::solid(Qt::black);
        object.stroke.paint = Paint::none();
        // The model's text origin is the first baseline; Sketch's frame is the box's
        // top. A fixed ascent approximation, like ExcalidrawImporter's.
        object.transform = QTransform::fromTranslate(0, object.text.size * 0.8) * ctm;
        const QUuid id = add(std::move(object), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }

    void buildBitmap(const QJsonObject &layer, const QUuid &parent, const QTransform &parentCTM, QHash<QString, QUuid> *idMap)
    {
        const QRectF frame = frameRect(layer);
        const QTransform ctm = layerTransform(frame, layer.value(QStringLiteral("rotation")).toDouble(0)) * parentCTM;
        const QString ref = layer.value(QStringLiteral("image")).toObject().value(QStringLiteral("_ref")).toString();
        QImage image;
        if (!images.contains(ref) || !image.loadFromData(images.value(ref))) {
            warnings << QStringLiteral("An embedded image could not be read.");
            return;
        }
        VectorObject object;
        object.kind = ObjectKind::image;
        describeCommon(layer, object);
        object.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        object.fill = Paint::none();
        object.stroke.paint = Paint::none();
        const double sx = image.width() > 0 ? frame.width() / image.width() : 1.0;
        const double sy = image.height() > 0 ? frame.height() / image.height() : 1.0;
        object.transform = QTransform::fromScale(sx, sy) * ctm;
        const QUuid id = add(std::move(object), parent);
        if (idMap)
            (*idMap)[layer.value(QStringLiteral("do_objectID")).toString()] = id;
    }
};

VectorDocument import(const QByteArray &data, QStringList *warnings)
{
    ZipReader zip(data);
    if (!zip.isValid())
        throw FileError(QStringLiteral("This is not a valid .sketch file (a zip archive)."));
    const QByteArray documentBytes = zip.read(QStringLiteral("document.json"));
    if (documentBytes.isEmpty())
        throw FileError(QStringLiteral("This .sketch file is missing its document.json."));
    QJsonParseError error;
    const QJsonDocument documentDoc = QJsonDocument::fromJson(documentBytes, &error);
    if (error.error != QJsonParseError::NoError || !documentDoc.isObject())
        throw FileError(QStringLiteral("This .sketch file's document.json is not valid JSON."));
    const QJsonObject documentJson = documentDoc.object();

    std::vector<QJsonObject> orderedPages;
    for (const QJsonValue &value : documentJson.value(QStringLiteral("pages")).toArray()) {
        const QString ref = value.toObject().value(QStringLiteral("_ref")).toString();
        if (ref.isEmpty())
            continue;
        const QByteArray pageBytes = zip.read(ref + QStringLiteral(".json"));
        if (pageBytes.isEmpty())
            continue;
        const QJsonDocument pageDoc = QJsonDocument::fromJson(pageBytes);
        if (pageDoc.isObject())
            orderedPages.push_back(pageDoc.object());
    }
    if (orderedPages.empty())
        throw FileError(QStringLiteral("This .sketch file has no pages to import."));

    Builder builder;
    for (const QString &entry : zip.entries()) {
        if (entry.startsWith(QStringLiteral("images/")))
            builder.images[entry] = zip.read(entry);
    }
    builder.build(orderedPages, documentJson);
    if (warnings)
        *warnings = builder.warnings;
    qCInfo(lcIO) << "parsed Sketch into" << builder.document.objects.size() << "objects;" << builder.warnings.size() << "warnings";
    return std::move(builder.document);
}

}

namespace SketchImporter {
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
    return zip.isValid() && zip.entries().contains(QStringLiteral("document.json"));
}
}
