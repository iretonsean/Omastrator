#include "Document/ShapeBuilder.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>

namespace {
std::vector<int> distinct(const std::vector<int> &values, size_t limit)
{
    std::vector<int> result;
    for (const int value : values) {
        if (value >= 0 && size_t(value) < limit && std::find(result.begin(), result.end(), value) == result.end())
            result.push_back(value);
    }
    return result;
}

int root(std::vector<int> &parent, int index)
{
    while (parent[size_t(index)] != index)
        index = parent[size_t(index)] = parent[size_t(parent[size_t(index)])];
    return index;
}

// Consecutive pieces of one open path joined back into a contour.
Contour joined(const std::vector<const Contour *> &run)
{
    Contour result = *run.front();
    for (size_t index = 1; index < run.size(); ++index) {
        result.nodes.back().out = run[index]->nodes.front().out;
        result.nodes.insert(result.nodes.end(), run[index]->nodes.begin() + 1, run[index]->nodes.end());
    }
    return result;
}
}

bool ShapeBuilder::build(VectorDocument &document, const Arrangement &arrangement, const Gesture &gesture, const ShapeBuilderOptions &options,
                         const Paint &fill, const StrokeStyle &stroke, std::vector<QUuid> *created)
{
    if (arrangement.truncated)
        return false;
    const std::vector<QUuid> &sources = arrangement.sources;
    for (const QUuid &id : sources) {
        if (!document.find(id))
            return false;
    }
    std::vector<int> touched = distinct(gesture.regions, arrangement.regions.size());
    std::vector<int> edges = distinct(gesture.edges, arrangement.edges.size());
    bool splitting = false;
    if (!gesture.erase) {
        // In merge mode edges only split, and only on a click when the option is on.
        if (options.clickingStrokeSplits && gesture.click && !edges.empty()) {
            edges.resize(1);
            touched.clear();
            splitting = true;
        } else {
            edges.clear();
        }
    }
    if (touched.empty() && edges.empty())
        return false;

    // Paths that share a region rebuild together, as one arrangement.
    const int count = int(sources.size());
    std::vector<int> parent(static_cast<size_t>(count));
    std::iota(parent.begin(), parent.end(), 0);
    for (const Region &region : arrangement.regions) {
        for (const int owner : region.owners)
            parent[size_t(root(parent, owner))] = root(parent, region.owners.front());
    }
    std::set<int> clusters;
    for (const int index : touched)
        clusters.insert(root(parent, arrangement.regions[size_t(index)].owners.front()));
    std::vector<bool> rebuilt(size_t(count), false);
    for (int index = 0; index < count; ++index)
        rebuilt[size_t(index)] = clusters.contains(root(parent, index));
    // Merging the only region of a lone path changes nothing.
    if (!gesture.erase && edges.empty() && std::count(rebuilt.begin(), rebuilt.end(), true) == 1) {
        const auto lone = std::find(rebuilt.begin(), rebuilt.end(), true) - rebuilt.begin();
        const auto regions = std::count_if(arrangement.regions.begin(), arrangement.regions.end(),
                                           [&](const Region &region) { return region.owners.front() == lone; });
        if (regions == 1)
            return false;
    }

    struct Made {
        int order;
        VectorObject object;
    };
    std::vector<Made> made;
    auto styledAs = [&](int source, VectorPath path) {
        VectorObject object = *document.find(sources[size_t(source)]);
        object.id = QUuid::createUuid();
        object.kind = ObjectKind::path;
        object.transform = {};
        object.path = std::move(path);
        return object;
    };
    const std::set<int> gone(touched.begin(), touched.end());
    if (!gesture.erase && !touched.empty()) {
        // Pick colour from artwork: the topmost path covering the region the drag began in.
        const int style = arrangement.regions[size_t(touched.front())].owners.back();
        int order = style;
        QPainterPath area;
        area.setFillRule(Qt::OddEvenFill);
        for (const int index : touched) {
            const Region &region = arrangement.regions[size_t(index)];
            area = area.isEmpty() ? region.area : area.united(region.area);
            order = std::max(order, region.owners.back());
        }
        VectorObject merged = styledAs(style, arrangement.shape(area.simplified()));
        if (!options.colorFromArtwork) {
            merged.fill = fill;
            merged.stroke = stroke;
        }
        if (!merged.path.isEmpty())
            made.push_back({order, std::move(merged)});
    }
    // Untouched regions of the rebuilt paths become paths of their own, styled as the topmost path over them.
    for (size_t index = 0; index < arrangement.regions.size(); ++index) {
        const Region &region = arrangement.regions[index];
        if (gone.contains(int(index)) || !rebuilt[size_t(region.owners.front())])
            continue;
        VectorObject piece = styledAs(region.owners.back(), arrangement.shape(region.area));
        if (!piece.path.isEmpty())
            made.push_back({region.owners.back(), std::move(piece)});
    }

    // Open paths: erased pieces go, a split piece stands alone, the rest rejoin into runs.
    std::map<int, std::set<int>> changedContours;
    const std::set<int> cut(edges.begin(), edges.end());
    for (const int index : edges)
        changedContours[arrangement.edges[size_t(index)].source].insert(arrangement.edges[size_t(index)].contour);
    for (const auto &[source, contours] : changedContours) {
        for (const int contour : contours) {
            std::vector<const Contour *> run;
            auto finish = [&] {
                if (!run.empty()) {
                    made.push_back({source, styledAs(source, VectorPath{{joined(run)}, Qt::WindingFill})});
                    run.clear();
                }
            };
            for (size_t index = 0; index < arrangement.edges.size(); ++index) {
                const Edge &edge = arrangement.edges[index];
                if (edge.source != source || edge.contour != contour)
                    continue;
                if (!cut.contains(int(index))) {
                    run.push_back(&edge.path);
                    continue;
                }
                finish();
                if (splitting)
                    made.push_back({source, styledAs(source, VectorPath{{edge.path}, Qt::WindingFill})});
            }
            finish();
        }
    }

    // What each changed path keeps: contours neither rebuilt as regions nor cut as edges.
    std::vector<QUuid> removed;
    int topmost = -1;
    for (int index = 0; index < count; ++index) {
        const bool edged = changedContours.contains(index);
        if (!rebuilt[size_t(index)] && !edged)
            continue;
        topmost = std::max(topmost, index);
        VectorObject &object = *document.find(sources[size_t(index)]);
        VectorPath kept;
        kept.fillRule = object.path.fillRule;
        for (size_t c = 0; c < object.path.contours.size(); ++c) {
            const Contour &contour = object.path.contours[c];
            const bool area = bindsArea(contour, options) && contour.nodes.size() >= 2;
            if (area ? !rebuilt[size_t(index)] : !(edged && changedContours.at(index).contains(int(c))))
                kept.contours.push_back(contour);
        }
        if (kept.isEmpty())
            removed.push_back(object.id);
        else
            object.path = kept;
    }
    if (topmost < 0)
        return false;
    // The new paths go above the topmost changed one, in the order of the paths they came from.
    std::stable_sort(made.begin(), made.end(), [](const Made &a, const Made &b) { return a.order < b.order; });
    const QUuid above = sources[size_t(topmost)];
    const QUuid into = *document.find(above)->parentID;
    QUuid previous = above;
    for (Made &each : made) {
        const QUuid id = each.object.id;
        document.insert(std::move(each.object), into, previous);
        previous = id;
        if (created)
            created->push_back(id);
    }
    document.remove(removed);
    return true;
}
