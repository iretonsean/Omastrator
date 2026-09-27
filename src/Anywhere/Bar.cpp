#include "Anywhere/Bar.h"
#include <QJsonObject>

namespace {
QJsonObject action(const char *id, const char *label, const char *tip)
{
    return {{"id", QLatin1String(id)}, {"label", QString::fromUtf8(label)}, {"tip", QString::fromUtf8(tip)}};
}

QJsonObject later(const char *id, const char *label, const char *reason)
{
    QJsonObject made = action(id, label, reason);
    made["enabled"] = false;
    return made;
}

struct Chip {
    const char *id;
    const char *label;
    // A bar action it runs, or "ask" with the prompt.
    const char *action;
    const char *prompt;
    bool ai;
    int weight;
};
}

namespace Bar {
QString kindOf(const QString &surfaceKind, bool otherBrowser)
{
    if (surfaceKind == QLatin1String("window") && otherBrowser)
        return QStringLiteral("browser");
    return surfaceKind;
}

QJsonArray actions(const QString &kind)
{
    if (kind == QLatin1String("web"))
        return {action("inspect", "Inspect", "Every style of this element, with Copy CSS"),
                later("lift", "Lift", "Lift into vectors comes in the next build."),
                action("mockup", "Mock Up", "Draw over this element with the rectangle tool"),
                action("measure", "Measure", "Distances from this element to the next one you point at"),
                action("extractSystem", "Extract Design System", "This page's colours, type, spacing, radii, shadows and repeated components, as a system to review")};
    if (kind == QLatin1String("window") || kind == QLatin1String("browser")) {
        QJsonArray list{action("capture", "Capture to Desk", "A screenshot of this window as a frame on the Desk"),
                        action("measure", "Measure", "Distances from this to the next thing you point at")};
        if (kind == QLatin1String("browser"))
            list.append(action("openInBrowser", "Open in Omastrator's Browser",
                               "This browser's pages can't be read. Open the page in Omastrator's own browser to inspect it."));
        return list;
    }
    if (kind == QLatin1String("desktop"))
        return {action("capture", "Capture to Desk", "A screenshot of this monitor as a frame on the Desk"),
                action("measure", "Measure", "Distances from this to the next thing you point at")};
    if (!kind.startsWith(QLatin1String("art:")))
        return {};
    // The in-app task bar's actions for the same kind of selection, then the ones every selection has.
    const QString art = kind.mid(4);
    QJsonArray list;
    if (art == QLatin1String("paths")) {
        list.append(action("unite", "Unite", "Pathfinder: Unite"));
        list.append(action("group", "Group", "Group"));
    } else if (art == QLatin1String("objects")) {
        list.append(action("group", "Group", "Group"));
    } else if (art.startsWith(QLatin1String("text"))) {
        list.append(action("createOutlines", "Create Outlines", "Type: Create Outlines"));
    } else if (art == QLatin1String("image")) {
        list.append(action("imageTrace", "Image Trace", "Object: Image Trace: Make"));
    } else if (art == QLatin1String("group")) {
        list.append(action("ungroup", "Ungroup", "Ungroup"));
    } else if (art == QLatin1String("clipGroup")) {
        list.append(action("releaseClippingMask", "Release Clipping Mask", "Object: Clipping Mask: Release"));
        list.append(action("ungroup", "Ungroup", "Ungroup"));
    }
    list.append(action("makeComponent", "Make Component", "Make this art a component, with instances that follow it"));
    list.append(action("designSystem", "Design System", "Tokens and components from your library, for this art"));
    list.append(action("duplicate", "Duplicate", "Duplicate"));
    list.append(action("delete", "Delete", "Delete"));
    list.append(action("undo", "Undo", "Undo the last change to this surface's art"));
    return list;
}

QJsonArray suggestions(const QString &kind, const AnywhereSettings::Answers &answers)
{
    const bool web = answers.makes.contains(QStringLiteral("web"));
    const bool rice = answers.makes.contains(QStringLiteral("rice"));
    const bool icons = answers.makes.contains(QStringLiteral("icons"));
    const bool marketing = answers.makes.contains(QStringLiteral("marketing"));
    const bool coder = answers.from.contains(QStringLiteral("code"));
    std::vector<Chip> chips;
    if (kind == QLatin1String("web")) {
        chips.push_back({"measure", "Measure spacing", "measure", "", false, web || coder ? 30 : 12});
        chips.push_back({"tokens", "Extract tokens", "ask",
                         "Pull this page's colours, type scale and spacing into swatches and a small type specimen beside it.", true,
                         web ? 28 : 14});
        chips.push_back({"variant", "Mock up a variant", "ask",
                         "Mock up a cleaner variant of this element on the overlay: same content, tighter spacing and hierarchy.", true,
                         web ? 24 : 16});
        chips.push_back({"capture", "Capture to Desk", "capture", "", false, marketing ? 32 : 10});
        chips.push_back({"icons", "Redraw its icons", "ask", "Redraw the icons in this element as clean vector icons on the overlay.", true,
                         icons ? 26 : 2});
    } else if (kind == QLatin1String("window") || kind == QLatin1String("browser")) {
        chips.push_back({"palette", "Pull its palette", "ask",
                         "Pull this window's colour palette into swatches on the overlay, named by role (background, text, accent).", true,
                         rice ? 32 : 12});
        chips.push_back({"capture", "Capture to Desk", "capture", "", false, marketing || icons ? 28 : 18});
        chips.push_back({"measure", "Measure spacing", "measure", "", false, web ? 24 : 14});
        chips.push_back({"critique", "Suggest fixes", "ask",
                         "Mark on the overlay the three spacing, contrast or alignment problems in this window that matter most, "
                         "each with a one-line note.",
                         true, web || rice ? 20 : 8});
    } else if (kind == QLatin1String("desktop")) {
        chips.push_back({"palette", "Pull the wallpaper's colours", "ask",
                         "Pull a palette of five to eight colours from what's on this monitor into swatches on the overlay.", true,
                         rice ? 34 : 12});
        chips.push_back({"capture", "Capture to Desk", "capture", "", false, 20});
    } else if (kind.startsWith(QLatin1String("art:"))) {
        chips.push_back({"desk", "Send to Desk", "sendDesk", "", false, 26});
        chips.push_back({"tidy", "Tidy it up", "ask", "Straighten, align and evenly space these shapes, keeping what they say.", true,
                         icons ? 24 : 16});
        chips.push_back({"polish", "Make it presentable", "ask",
                         "Turn this sketch into clean, presentable shapes: consistent stroke weights and corner radii.", true,
                         marketing || icons ? 22 : 12});
    }
    std::stable_sort(chips.begin(), chips.end(), [](const Chip &a, const Chip &b) { return a.weight > b.weight; });
    const int limit = answers.ai == QLatin1String("quiet") ? 1 : answers.ai == QLatin1String("lots") ? 3 : 2;
    QJsonArray result;
    for (const Chip &chip : chips) {
        if (result.size() >= limit)
            break;
        // Kept quiet, AI waits to be asked.
        if (chip.ai && answers.ai == QLatin1String("quiet"))
            continue;
        QJsonObject made{{"id", QLatin1String(chip.id)}, {"label", QString::fromUtf8(chip.label)}, {"action", QLatin1String(chip.action)},
                         {"ai", chip.ai}};
        if (chip.prompt[0] != '\0')
            made["prompt"] = QString::fromUtf8(chip.prompt);
        result.append(made);
    }
    return result;
}

QString askPlaceholder(const QString &kind)
{
    if (kind == QLatin1String("web"))
        return QStringLiteral("Ask about this element…");
    if (kind.startsWith(QLatin1String("art:")))
        return QStringLiteral("Ask AI to change this…");
    return QStringLiteral("Ask about this…");
}
}
