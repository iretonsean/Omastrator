#include "Live/Motion.h"
#include <algorithm>
#include <QSet>
#include <cmath>
#include <limits>

namespace Motion {
namespace {

QString shortName(const QJsonObject &each)
{
    const QString tag = each["tag"].toString();
    const QString id = each["id"].toString();
    const QString cls = each["cls"].toString();
    if (!id.isEmpty())
        return tag + QLatin1Char('#') + id;
    return cls.isEmpty() ? tag : tag + QLatin1Char('.') + cls;
}

// The row's name. One element reads as its tag and id or class; several siblings read "parent .class × n".
QString labelFor(const QList<QJsonObject> &members)
{
    const QJsonObject &first = members.first();
    // Two transitions on one element are one element.
    QSet<QString> elements;
    for (const QJsonObject &member : members)
        elements.insert(member["selector"].toString());
    if (elements.size() == 1)
        return shortName(first);
    const QString cls = first["cls"].toString();
    const QString tag = first["tag"].toString();
    const QString parent = first["parent"].toObject()["tag"].toString();
    const QString many = QStringLiteral(" × %1").arg(elements.size());
    if (!cls.isEmpty())
        return (parent.isEmpty() ? QString() : parent + QLatin1Char(' ')) + QLatin1Char('.') + cls + many;
    return tag + many;
}

QStringList strings(const QJsonArray &array)
{
    QStringList out;
    for (const QJsonValue &value : array)
        out << value.toString();
    return out;
}

// What makes two animations one row: the same motion, on siblings, on the same clock. Transitions of one element are one row,
// whatever they move, and whether or not a state is holding them yet.
QString groupKey(const QJsonObject &each)
{
    const QString kind = each["kind"].toString();
    if (kind == QLatin1String("css-transition"))
        return QStringList{kind, each["selector"].toString(), each["timeline"].toString()}.join(QLatin1Char('|'));
    const QString parent = each["parent"].toObject()["selector"].toString(each["selector"].toString());
    QString name = each["name"].toString();
    if (name.isEmpty() || kind != QLatin1String("css-animation"))
        name += QLatin1Char('|') + strings(each["properties"].toArray()).join(QLatin1Char(','));
    return QStringList{kind, name, parent, each["timeline"].toString()}.join(QLatin1Char('|'));
}

Bar barOf(const QJsonObject &each, double &delay, double &duration)
{
    Bar bar;
    bar.selector = each["selector"].toString();
    if (each["timeline"].toString() != QLatin1String("document")) {
        const QJsonObject range = each["range"].toObject();
        bar.start = range["from"].toDouble();
        bar.length = std::max(0.0, range["to"].toDouble() - bar.start);
        return bar;
    }
    delay = each["delay"].toDouble();
    duration = each["duration"].toDouble();
    const double iterations = each["iterations"].toDouble(1);
    bar.loops = iterations < 0;
    bar.start = each["offset"].toDouble() + delay;
    bar.length = duration * (bar.loops ? 1.0 : std::max(iterations, 0.0));
    return bar;
}

}

QString Track::state() const
{
    return kind == QLatin1String("css-transition") && (trigger == QLatin1String("hover") || trigger == QLatin1String("focus")
                                                      || trigger == QLatin1String("active") || trigger == QLatin1String("focus-visible")
                                                      || trigger == QLatin1String("focus-within"))
        ? trigger
        : QString();
}

bool Timeline::hasTime() const
{
    return std::any_of(tracks.cbegin(), tracks.cend(), [](const Track &track) { return !track.isScroll(); });
}

bool Timeline::hasScroll() const
{
    return std::any_of(tracks.cbegin(), tracks.cend(), [](const Track &track) { return track.isScroll(); });
}

QString Timeline::trigger() const
{
    for (const Track &track : tracks)
        if (!track.isScroll())
            return triggerText(track.trigger);
    return tracks.isEmpty() ? QString() : triggerText(tracks.first().trigger);
}

const Track *Timeline::find(const QString &id) const
{
    for (const Track &track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

QString triggerText(const QString &trigger)
{
    if (trigger == QLatin1String("load"))
        return QStringLiteral("On load");
    if (trigger == QLatin1String("scroll"))
        return QStringLiteral("On scroll");
    if (trigger == QLatin1String("hover"))
        return QStringLiteral("On hover");
    if (trigger == QLatin1String("focus") || trigger == QLatin1String("focus-visible") || trigger == QLatin1String("focus-within"))
        return QStringLiteral("On focus");
    if (trigger == QLatin1String("active"))
        return QStringLiteral("On press");
    if (trigger == QLatin1String("script"))
        return QStringLiteral("From a script");
    return trigger.isEmpty() ? QString() : QStringLiteral("On change");
}

QString seconds(double ms)
{
    return QStringLiteral("%1 s").arg(std::max(0.0, ms) / 1000.0, 0, 'f', 2);
}

QString easingName(const QString &easing)
{
    const QString value = easing.simplified();
    static const QList<std::pair<QString, QString>> names{
        {QStringLiteral("linear"), QStringLiteral("Linear")},
        {QStringLiteral("ease"), QStringLiteral("Ease")},
        {QStringLiteral("ease-in"), QStringLiteral("Ease in")},
        {QStringLiteral("ease-out"), QStringLiteral("Ease out")},
        {QStringLiteral("ease-in-out"), QStringLiteral("Ease in and out")},
        {QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"), QStringLiteral("Soft out")},
        {QStringLiteral("cubic-bezier(0.25, 1, 0.5, 1)"), QStringLiteral("Ease out quart")},
        {QStringLiteral("cubic-bezier(0.34, 1.56, 0.64, 1)"), QStringLiteral("Spring")}};
    for (const auto &name : names)
        if (name.first == value)
            return name.second;
    return QStringLiteral("Custom");
}

Timeline parse(const QJsonObject &list)
{
    Timeline timeline;
    if (!list.contains(QLatin1String("animations")))
        return timeline;
    timeline.held = list["held"].toBool();
    timeline.time = list["time"].toDouble();
    timeline.truncated = list["truncated"].toBool();
    timeline.url = list["url"].toString();
    const QJsonObject scroll = list["scroll"].toObject();
    timeline.scrollY = scroll["y"].toDouble();
    timeline.scrollMax = scroll["max"].toDouble();
    timeline.viewport = scroll["viewport"].toDouble();

    // Members of a row, in the order the page listed them.
    struct Row {
        QString key;
        QList<QJsonObject> members;
    };
    QList<Row> rows;
    for (const QJsonValue &value : list["animations"].toArray()) {
        const QJsonObject each = value.toObject();
        const QString key = groupKey(each);
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const Row &row) { return row.key == key; });
        if (found == rows.end())
            rows.append({key, {each}});
        else
            found->members.append(each);
    }

    for (const Row &row : std::as_const(rows)) {
        const QJsonObject &first = row.members.first();
        Track track;
        track.id = row.key;
        track.label = labelFor(row.members);
        track.kind = first["kind"].toString();
        track.timeline = first["timeline"].toString() == QLatin1String("document") ? QStringLiteral("document") : QStringLiteral("scroll");
        track.trigger = first["trigger"].toString();
        track.name = first["name"].toString();
        track.easing = first["easing"].toString();
        for (const QJsonObject &member : row.members)
            for (const QString &property : strings(member["properties"].toArray()))
                if (!track.properties.contains(property))
                    track.properties << property;
        // A transition row is named for what it moves.
        if (track.kind == QLatin1String("css-transition"))
            track.name = track.properties.join(QStringLiteral(", "));
        track.detail = track.properties.join(QStringLiteral(", "));
        track.keyframes = first["keyframes"].toArray();
        track.potential = first["potential"].toBool();
        track.start = std::numeric_limits<double>::max();
        for (const QJsonObject &member : row.members) {
            double delay = 0;
            double duration = 0;
            const Bar bar = barOf(member, delay, duration);
            track.selectors << bar.selector;
            track.bars.append(bar);
            track.start = std::min(track.start, bar.start);
            track.end = std::max(track.end, bar.start + bar.length);
            track.loops = track.loops || bar.loops;
            if (&member == &row.members.first()) {
                track.delay = delay;
                track.duration = duration;
            }
        }
        if (track.bars.isEmpty())
            track.start = 0;
        if (track.bars.size() > 1 && !track.isScroll()) {
            QList<double> starts;
            for (const Bar &bar : std::as_const(track.bars))
                starts.append(bar.start);
            std::sort(starts.begin(), starts.end());
            const double gap = starts[1] - starts[0];
            bool even = gap > 0;
            for (int i = 2; i < starts.size() && even; ++i)
                even = std::abs((starts[i] - starts[i - 1]) - gap) < 0.5;
            track.stagger = even ? gap : 0;
        }
        timeline.tracks.append(track);
    }

    // GSAP runs its own clock: one bar for the whole of it, which can be scrubbed and not tuned.
    const QJsonObject gsap = list["gsap"].toObject();
    if (gsap["count"].toInt() > 0) {
        Track track;
        track.id = QStringLiteral("gsap");
        track.label = QStringLiteral("GSAP timeline");
        track.detail = QStringLiteral("%1 tweens").arg(gsap["count"].toInt());
        track.kind = QStringLiteral("gsap");
        track.timeline = QStringLiteral("document");
        track.trigger = QStringLiteral("script");
        track.start = gsap["start"].toDouble();
        track.end = gsap["end"].toDouble();
        track.duration = track.end - track.start;
        track.bars.append({QString(), track.start, track.duration, false});
        timeline.tracks.append(track);
    }

    // Time first, then scrolling, each in the order the page gave.
    std::stable_partition(timeline.tracks.begin(), timeline.tracks.end(), [](const Track &track) { return !track.isScroll(); });
    for (const Track &track : std::as_const(timeline.tracks))
        if (!track.isScroll())
            timeline.duration = std::max(timeline.duration, track.end);
    return timeline;
}

}
