#include "Document/VectorDocument.h"
#include "IO/FigmaImporter.h"
#include "IO/FigmaMapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The REST API's JSON (docs.figma.com), translated field by field into Figma's
// own Kiwi vocabulary so it reuses FigmaMap's mapper unchanged (docs/import/figma.md).
// The REST tree nests `children` directly, so this builds a FigmaMap::Tree by
// hand instead of going through FigmaMap::buildTree (which assumes a flat,
// guid-and-parentIndex list, the Kiwi shape).
namespace FigmaImporter {
namespace {
using namespace FigmaMap;

QString restConstraint(const QString &value)
{
    if (value == QLatin1String("LEFT") || value == QLatin1String("TOP"))
        return QStringLiteral("MIN");
    if (value == QLatin1String("RIGHT") || value == QLatin1String("BOTTOM"))
        return QStringLiteral("MAX");
    if (value == QLatin1String("LEFT_RIGHT") || value == QLatin1String("TOP_BOTTOM"))
        return QStringLiteral("STRETCH");
    if (value == QLatin1String("SCALE"))
        return QStringLiteral("SCALE");
    return QStringLiteral("CENTER"); // CENTER, or an unknown value: centered is the least surprising default.
}

QVariantMap restPaint(const QJsonObject &p)
{
    QVariantMap paint = p.toVariantMap();
    if (p.contains(QStringLiteral("gradientStops"))) {
        QVariantList stops;
        for (const QJsonValue &s : p.value(QStringLiteral("gradientStops")).toArray())
            stops.append(s.toObject().toVariantMap());
        paint.insert(QStringLiteral("stops"), stops);
    }
    return paint;
}

QVariantList restPaints(const QJsonObject &node, const char *key)
{
    QVariantList result;
    for (const QJsonValue &p : node.value(QLatin1String(key)).toArray())
        result.append(restPaint(p.toObject()));
    return result;
}

QVariantMap restFontName(const QJsonObject &style)
{
    QVariantMap fontName;
    const QString family = style.value(QStringLiteral("fontFamily")).toString(QStringLiteral("Inter"));
    fontName.insert(QStringLiteral("family"), family);
    const int weight = style.contains(QStringLiteral("fontWeight")) ? style.value(QStringLiteral("fontWeight")).toInt(400) : 400;
    fontName.insert(QStringLiteral("style"), TextContent::styleFor(family, weight, style.value(QStringLiteral("italic")).toBool()));
    return fontName;
}

QVariantMap restLetterSpacing(const QJsonObject &style)
{
    if (!style.contains(QStringLiteral("letterSpacing")))
        return {};
    // REST always resolves letterSpacing to pixels.
    return {{QStringLiteral("units"), QStringLiteral("PIXELS")}, {QStringLiteral("value"), style.value(QStringLiteral("letterSpacing")).toDouble()}};
}

QVariantMap restLineHeight(const QJsonObject &style)
{
    const QString unit = style.value(QStringLiteral("lineHeightUnit")).toString();
    if (unit == QLatin1String("PIXELS"))
        return {{QStringLiteral("units"), QStringLiteral("PIXELS")}, {QStringLiteral("value"), style.value(QStringLiteral("lineHeightPx")).toDouble()}};
    if (unit == QLatin1String("FONT_SIZE_%")) {
        return {{QStringLiteral("units"), QStringLiteral("PERCENT")},
                {QStringLiteral("value"), style.value(QStringLiteral("lineHeightPercentFontSize")).toDouble(100)}};
    }
    // INTRINSIC_%, or absent: Figma's own automatic leading.
    return {{QStringLiteral("units"), QStringLiteral("PERCENT")}, {QStringLiteral("value"), 100}};
}

// A TypeStyle's fields, onto `fields` under Kiwi's own names (fontName, fontSize, …).
void applyRestStyle(QVariantMap &fields, const QJsonObject &style)
{
    fields.insert(QStringLiteral("fontName"), restFontName(style));
    if (style.contains(QStringLiteral("fontSize")))
        fields.insert(QStringLiteral("fontSize"), style.value(QStringLiteral("fontSize")).toDouble());
    if (style.contains(QStringLiteral("textCase")))
        fields.insert(QStringLiteral("textCase"), style.value(QStringLiteral("textCase")));
    if (style.contains(QStringLiteral("textDecoration")))
        fields.insert(QStringLiteral("textDecoration"), style.value(QStringLiteral("textDecoration")));
    if (const QVariantMap spacing = restLetterSpacing(style); !spacing.isEmpty())
        fields.insert(QStringLiteral("letterSpacing"), spacing);
    if (style.contains(QStringLiteral("lineHeightPx")) || style.contains(QStringLiteral("lineHeightUnit")))
        fields.insert(QStringLiteral("lineHeight"), restLineHeight(style));
    if (style.contains(QStringLiteral("textAlignHorizontal")))
        fields.insert(QStringLiteral("textAlignHorizontal"), style.value(QStringLiteral("textAlignHorizontal")));
    if (style.contains(QStringLiteral("textAlignVertical")))
        fields.insert(QStringLiteral("textAlignVertical"), style.value(QStringLiteral("textAlignVertical")));
    if (style.contains(QStringLiteral("paragraphSpacing")))
        fields.insert(QStringLiteral("paragraphSpacing"), style.value(QStringLiteral("paragraphSpacing")).toDouble());
}

QVariantMap restTextData(const QJsonObject &node)
{
    QVariantMap textData;
    textData.insert(QStringLiteral("characters"), node.value(QStringLiteral("characters")).toString());
    QVariantList ids;
    for (const QJsonValue &id : node.value(QStringLiteral("characterStyleOverrides")).toArray())
        ids.append(id.toInt());
    textData.insert(QStringLiteral("characterStyleIDs"), ids);
    QVariantList overrides;
    const QJsonObject table = node.value(QStringLiteral("styleOverrideTable")).toObject();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        QVariantMap entry;
        entry.insert(QStringLiteral("styleID"), it.key());
        applyRestStyle(entry, it.value().toObject());
        overrides.append(entry);
    }
    textData.insert(QStringLiteral("styleOverrideTable"), overrides);
    return textData;
}

// One node's own fields (not its guid, parent or children), translated into
// Kiwi's field names.
QVariantMap translateFields(const QJsonObject &node)
{
    QVariantMap fields;
    const QString restType = node.value(QStringLiteral("type")).toString();
    // REST names components differently; the shared mapper expects Kiwi's own SYMBOL
    // (a master) and a FRAME with isStateGroup (a set) instead of COMPONENT/COMPONENT_SET.
    fields.insert(QStringLiteral("type"), restType == QLatin1String("COMPONENT") ? QStringLiteral("SYMBOL")
        : restType == QLatin1String("COMPONENT_SET")                            ? QStringLiteral("FRAME")
                                                                                  : restType);
    if (restType == QLatin1String("COMPONENT_SET"))
        fields.insert(QStringLiteral("isStateGroup"), true);
    fields.insert(QStringLiteral("name"), node.value(QStringLiteral("name")).toString());
    fields.insert(QStringLiteral("visible"), node.contains(QStringLiteral("visible")) ? node.value(QStringLiteral("visible")).toBool() : true);
    fields.insert(QStringLiteral("locked"), node.value(QStringLiteral("locked")).toBool());
    fields.insert(QStringLiteral("opacity"), node.contains(QStringLiteral("opacity")) ? node.value(QStringLiteral("opacity")).toDouble() : 1.0);
    if (node.contains(QStringLiteral("blendMode")))
        fields.insert(QStringLiteral("blendMode"), node.value(QStringLiteral("blendMode")));
    if (node.contains(QStringLiteral("size")))
        fields.insert(QStringLiteral("size"), node.value(QStringLiteral("size")).toObject().toVariantMap());
    if (node.contains(QStringLiteral("relativeTransform")))
        fields.insert(QStringLiteral("transform"), node.value(QStringLiteral("relativeTransform")).toArray().toVariantList());
    if (node.contains(QStringLiteral("fillPaints")) || node.contains(QStringLiteral("fills")))
        fields.insert(QStringLiteral("fillPaints"), restPaints(node, node.contains(QStringLiteral("fills")) ? "fills" : "fillPaints"));
    if (node.contains(QStringLiteral("strokePaints")) || node.contains(QStringLiteral("strokes")))
        fields.insert(QStringLiteral("strokePaints"), restPaints(node, node.contains(QStringLiteral("strokes")) ? "strokes" : "strokePaints"));
    if (node.contains(QStringLiteral("strokeWeight")))
        fields.insert(QStringLiteral("strokeWeight"), node.value(QStringLiteral("strokeWeight")).toDouble());
    for (const char *key : {"strokeAlign", "strokeCap", "strokeJoin", "strokeDashes", "effects"}) {
        if (node.contains(QLatin1String(key)))
            fields.insert(QLatin1String(key), node.value(QLatin1String(key)).toVariant());
    }
    if (node.contains(QStringLiteral("cornerRadius")))
        fields.insert(QStringLiteral("cornerRadius"), node.value(QStringLiteral("cornerRadius")).toDouble());
    if (const QJsonArray radii = node.value(QStringLiteral("rectangleCornerRadii")).toArray(); radii.size() == 4) {
        fields.insert(QStringLiteral("rectangleCornerRadiiIndependent"), true);
        static const char *names[4] = {"rectangleTopLeftCornerRadius", "rectangleTopRightCornerRadius", "rectangleBottomRightCornerRadius",
                                        "rectangleBottomLeftCornerRadius"};
        for (int i = 0; i < 4; ++i)
            fields.insert(QLatin1String(names[i]), radii[i].toDouble());
    }
    if (node.contains(QStringLiteral("constraints"))) {
        const QJsonObject constraints = node.value(QStringLiteral("constraints")).toObject();
        fields.insert(QStringLiteral("horizontalConstraint"), restConstraint(constraints.value(QStringLiteral("horizontal")).toString()));
        fields.insert(QStringLiteral("verticalConstraint"), restConstraint(constraints.value(QStringLiteral("vertical")).toString()));
    }
    if (node.contains(QStringLiteral("clipsContent")))
        fields.insert(QStringLiteral("clipsContent"), node.value(QStringLiteral("clipsContent")).toBool());
    if (node.value(QStringLiteral("isMask")).toBool())
        fields.insert(QStringLiteral("mask"), true);

    // Auto layout: REST's own names, folded onto Kiwi's stack* fields (docs/import/figma.md).
    const QString layoutMode = node.value(QStringLiteral("layoutMode")).toString();
    if (layoutMode == QLatin1String("HORIZONTAL") || layoutMode == QLatin1String("VERTICAL")) {
        fields.insert(QStringLiteral("stackMode"), layoutMode);
        fields.insert(QStringLiteral("stackSpacing"), node.value(QStringLiteral("itemSpacing")).toDouble());
        fields.insert(QStringLiteral("stackCounterSpacing"), node.value(QStringLiteral("counterAxisSpacing")).toDouble());
        fields.insert(QStringLiteral("stackVerticalPadding"), node.value(QStringLiteral("paddingTop")).toDouble());
        fields.insert(QStringLiteral("stackHorizontalPadding"), node.value(QStringLiteral("paddingLeft")).toDouble());
        fields.insert(QStringLiteral("stackPaddingRight"), node.value(QStringLiteral("paddingRight")).toDouble());
        fields.insert(QStringLiteral("stackPaddingBottom"), node.value(QStringLiteral("paddingBottom")).toDouble());
        fields.insert(QStringLiteral("stackPrimaryAlignItems"), node.value(QStringLiteral("primaryAxisAlignItems")).toVariant());
        const QString counterAlign = node.value(QStringLiteral("counterAxisAlignItems")).toString();
        fields.insert(QStringLiteral("stackCounterAlignItems"), counterAlign == QLatin1String("BASELINE") ? QStringLiteral("CENTER") : counterAlign);
        fields.insert(QStringLiteral("stackWrap"), node.value(QStringLiteral("layoutWrap")).toString() == QLatin1String("WRAP") ? QStringLiteral("WRAP") : QString());
        fields.insert(QStringLiteral("stackPrimarySizing"), node.value(QStringLiteral("primaryAxisSizingMode")).toVariant());
        fields.insert(QStringLiteral("stackCounterSizing"), node.value(QStringLiteral("counterAxisSizingMode")).toString() == QLatin1String("AUTO")
                          ? QStringLiteral("RESIZE_TO_FIT_WITH_IMPLICIT_SIZE")
                          : QStringLiteral("FIXED"));
    }
    fields.insert(QStringLiteral("stackPositioning"), node.value(QStringLiteral("layoutPositioning")).toString() == QLatin1String("ABSOLUTE")
                      ? QStringLiteral("ABSOLUTE")
                      : QStringLiteral("AUTO"));
    fields.insert(QStringLiteral("stackChildPrimaryGrow"), node.value(QStringLiteral("layoutGrow")).toDouble() > 0);
    fields.insert(QStringLiteral("stackChildAlignSelf"), node.value(QStringLiteral("layoutAlign")).toString() == QLatin1String("STRETCH")
                      ? QStringLiteral("STRETCH")
                      : QString());

    if (node.contains(QStringLiteral("characters")))
        fields.insert(QStringLiteral("textData"), restTextData(node));
    if (node.contains(QStringLiteral("style")))
        applyRestStyle(fields, node.value(QStringLiteral("style")).toObject());
    if (node.contains(QStringLiteral("textAutoResize")))
        fields.insert(QStringLiteral("textAutoResize"), node.value(QStringLiteral("textAutoResize")));

    if (node.contains(QStringLiteral("fillGeometry")))
        fields.insert(QStringLiteral("fillGeometry"), node.value(QStringLiteral("fillGeometry")).toArray().toVariantList());
    if (node.contains(QStringLiteral("booleanOperation")))
        fields.insert(QStringLiteral("booleanOperation"), node.value(QStringLiteral("booleanOperation")));
    if (node.contains(QStringLiteral("componentId"))) {
        QVariantMap symbolData;
        symbolData.insert(QStringLiteral("symbolID"), node.value(QStringLiteral("componentId")).toVariant());
        fields.insert(QStringLiteral("symbolData"), symbolData);
    }
    return fields;
}

// Builds the tree directly (REST already nests `children`, unlike Kiwi's flat
// nodeChanges): each node's REST id is used as its guid as-is.
void walk(const QJsonObject &node, const Guid &parent, int index, Tree &tree)
{
    const Guid guid = node.value(QStringLiteral("id")).toString();
    // A repeated id (a hostile or buggy response) would make a node its own descendant.
    if (guid.isEmpty() || tree.nodes.count(guid))
        return;
    Node built;
    built.fields = translateFields(node);
    built.guid = guid;
    built.parent = parent;
    // Zero-padded so plain string comparison still sorts correctly (FigmaMapper.h).
    built.position = QStringLiteral("%1").arg(index, 6, 10, QLatin1Char('0'));
    tree.nodes[guid] = built;
    if (!parent.isEmpty())
        tree.nodes[parent].children.push_back(guid);
    if (str(built.fields, "type") == QLatin1String("CANVAS"))
        tree.pages.push_back(guid);
    int childIndex = 0;
    for (const QJsonValue &child : node.value(QStringLiteral("children")).toArray())
        walk(child.toObject(), guid, childIndex++, tree);
}
}

VectorDocument parseRestFile(const QByteArray &json, const QString &nodeID, QStringList *warnings)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        throw FileError(QStringLiteral("Figma didn’t return a file."));
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("err"))
        || (root.contains(QStringLiteral("message")) && !root.contains(QStringLiteral("document")) && !root.contains(QStringLiteral("nodes"))))
        throw FileError(root.value(QStringLiteral("message")).toString(QStringLiteral("Figma returned an error.")));

    Tree tree;
    if (root.contains(QStringLiteral("nodes"))) {
        // GET .../nodes?ids=: {"nodes": {"1:2": {"document": {...}}}}.
        const QJsonObject nodes = root.value(QStringLiteral("nodes")).toObject();
        int index = 0;
        for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it)
            walk(it.value().toObject().value(QStringLiteral("document")).toObject(), {}, index++, tree);
    } else {
        walk(root.value(QStringLiteral("document")).toObject(), {}, 0, tree);
    }

    QStringList localWarnings;
    VectorDocument document = nodeID.isEmpty() ? FigmaMap::map(tree, localWarnings) : FigmaMap::mapNode(tree, nodeID, localWarnings);
    localWarnings.removeDuplicates();
    if (warnings)
        *warnings = localWarnings;
    return document;
}
}
