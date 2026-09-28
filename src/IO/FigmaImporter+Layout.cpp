#include "IO/FigmaMapperInternal.h"

// Field names follow Figma's own (Kiwi) vocabulary throughout (docs/import/figma.md);
// FigmaImporter+Rest.cpp translates the REST API's different names into these before
// a node ever reaches this mapper.
namespace FigmaMap {

void applyAutoLayout(Context &ctx, const QVariantMap &node, VectorObject &frame)
{
    Q_UNUSED(ctx);
    const QString stack = str(node, "stackMode", QStringLiteral("NONE"));
    if (stack != QLatin1String("HORIZONTAL") && stack != QLatin1String("VERTICAL"))
        return;
    AutoLayout layout;
    layout.direction = stack == QLatin1String("HORIZONTAL") ? LayoutDirection::horizontal : LayoutDirection::vertical;
    layout.gap = num(node, "stackSpacing");
    const QString primary = str(node, "stackPrimaryAlignItems", QStringLiteral("MIN"));
    layout.spaceBetween = primary.contains(QLatin1String("SPACE"));
    static const std::map<QString, LayoutAlign> aligns{
        {QStringLiteral("MIN"), LayoutAlign::start}, {QStringLiteral("CENTER"), LayoutAlign::center}, {QStringLiteral("MAX"), LayoutAlign::end}};
    const auto p = aligns.find(primary);
    layout.primary = p == aligns.end() ? LayoutAlign::start : p->second;
    const auto c = aligns.find(str(node, "stackCounterAlignItems", QStringLiteral("MIN")));
    layout.counter = c == aligns.end() ? LayoutAlign::start : c->second;
    layout.paddingTop = num(node, "stackVerticalPadding");
    layout.paddingLeft = num(node, "stackHorizontalPadding");
    layout.paddingRight = num(node, "stackPaddingRight");
    layout.paddingBottom = num(node, "stackPaddingBottom");
    layout.wrap = str(node, "stackWrap") == QLatin1String("WRAP");
    layout.counterGap = num(node, "stackCounterSpacing", layout.gap);
    frame.autoLayout = layout;
}

void applyLayoutItem(Context &ctx, const QVariantMap &node, const QVariantMap *parentNode, VectorObject &object)
{
    Q_UNUSED(ctx);
    LayoutItem item;
    // Hug/fixed, on the axes this object lays its own children along (only if it has
    // auto layout itself; a plain frame's size is always fixed here).
    const QString ownStack = str(node, "stackMode", QStringLiteral("NONE"));
    if (ownStack == QLatin1String("HORIZONTAL") || ownStack == QLatin1String("VERTICAL")) {
        const LayoutSizing primaryHug = str(node, "stackPrimarySizing") == QLatin1String("FIXED") ? LayoutSizing::fixed : LayoutSizing::hug;
        const LayoutSizing counterHug = str(node, "stackCounterSizing") == QLatin1String("FIXED") ? LayoutSizing::fixed : LayoutSizing::hug;
        if (ownStack == QLatin1String("HORIZONTAL")) {
            item.width = primaryHug;
            item.height = counterHug;
        } else {
            item.height = primaryHug;
            item.width = counterHug;
        }
    }
    // Fill, on the axes the parent's auto layout flows this object along; otherwise,
    // constraints (how it follows a plain frame's resize).
    const QString parentStack = parentNode ? str(*parentNode, "stackMode", QStringLiteral("NONE")) : QStringLiteral("NONE");
    if (parentStack == QLatin1String("HORIZONTAL") || parentStack == QLatin1String("VERTICAL")) {
        item.absolute = str(node, "stackPositioning") == QLatin1String("ABSOLUTE");
        const bool primaryFill = boolean(node, "stackChildPrimaryGrow") || num(node, "stackChildPrimaryGrow") > 0;
        const bool counterFill = str(node, "stackChildAlignSelf") == QLatin1String("STRETCH");
        if (parentStack == QLatin1String("HORIZONTAL")) {
            if (primaryFill)
                item.width = LayoutSizing::fill;
            if (counterFill)
                item.height = LayoutSizing::fill;
        } else {
            if (primaryFill)
                item.height = LayoutSizing::fill;
            if (counterFill)
                item.width = LayoutSizing::fill;
        }
    } else {
        static const std::map<QString, LayoutConstraint> constraints{
            {QStringLiteral("MIN"), LayoutConstraint::start},    {QStringLiteral("MAX"), LayoutConstraint::end},
            {QStringLiteral("CENTER"), LayoutConstraint::center}, {QStringLiteral("STRETCH"), LayoutConstraint::both},
            {QStringLiteral("SCALE"), LayoutConstraint::scale}};
        const auto h = constraints.find(str(node, "horizontalConstraint", QStringLiteral("MIN")));
        const auto v = constraints.find(str(node, "verticalConstraint", QStringLiteral("MIN")));
        item.horizontal = h == constraints.end() ? LayoutConstraint::start : h->second;
        item.vertical = v == constraints.end() ? LayoutConstraint::start : v->second;
    }
    object.layout = item;
}
}
