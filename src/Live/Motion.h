#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

// The page's motion as the timeline draws it (docs/MOTION.md, section 2): the rows made from what `__oma.motion.list()`
// reports. Nothing here touches a page, so the merging and the labels are tested without a browser.
namespace Motion {

// One element's animation on a row.
struct Bar {
    QString selector;
    // The element as the designer would call it ("article#guji"), its index in the group (`--i`, -1 when it has none), and the
    // extra delay it takes on its own (`--delay-extra`, ms; 0 when it keeps the group's timing).
    QString label;
    int index = -1;
    double extra = 0;
    // Milliseconds on the timeline, or px on the scroll axis for a scroll row. The delay is inside `start`.
    double start = 0;
    double length = 0;
    // Runs for ever; the bar is one iteration.
    bool loops = false;
};

struct Track {
    // Stable across refreshes, so a selected row stays selected while the page is scrubbed.
    QString id;
    // "h1 .word × 5", "p.lede", "a#primary"; and the properties it moves, "opacity, translate".
    QString label;
    QString detail;
    // css-animation, css-transition, script or gsap.
    QString kind;
    // document, or scroll for an animation driven by scrolling.
    QString timeline;
    // load, scroll, hover, focus, active, change or script.
    QString trigger;
    // The @keyframes name, the transitioned properties or the script's animation id.
    QString name;
    QString easing;
    QStringList selectors;
    QStringList properties;
    QList<Bar> bars;
    // The first animation's keyframes: [{offset, easing, props: {name: value}}].
    QJsonArray keyframes;
    double start = 0;
    double end = 0;
    // The state isn't held yet: a transition a hover or focus would start. Holding it makes the row real.
    bool potential = false;
    bool loops = false;
    double delay = 0;
    double duration = 0;
    // The gap between the starts of a row's bars when it is the same throughout (a stagger, ms); 0 otherwise.
    double stagger = 0;

    bool isScroll() const { return timeline == QLatin1String("scroll"); }
    // The pointer or focus state that starts it, for a transition; empty otherwise.
    QString state() const;
};

struct Timeline {
    bool held = false;
    // Where the page had reached when it was held, or the last seek (ms).
    double time = 0;
    // The end of the rows that run on time (ms), never less than 0.
    double duration = 0;
    // The scroll axis: where the page is, how far it goes and how tall its window is (px).
    double scrollY = 0;
    double scrollMax = 0;
    double viewport = 0;
    bool truncated = false;
    // The page has a style rule for visitors who asked for less motion, and whether the page is playing as if one did.
    bool reducedRule = false;
    bool reduced = false;
    QString url;
    // Rows that run on time first, then the rows driven by scrolling.
    QList<Track> tracks;

    bool hasTime() const;
    bool hasScroll() const;
    // "On load", "On scroll", "On hover": what starts the first row on time (else the first on scroll); empty with no rows.
    QString trigger() const;
    const Track *find(const QString &id) const;
};

// A list from the overlay, as rows. An empty or unrelated object gives no rows.
Timeline parse(const QJsonObject &list);

// The order a group's elements start in (docs/MOTION.md, section 5): each gets an index, 0 to n-1, that its rule turns into a delay.
enum class Order { picked, leftToRight, centreOut, shuffle };
struct Element {
    QString selector;
    QRectF box;
};
// The index each of `elements` gets, in the order they are given (the order they were picked in). Left to right sorts by the box's
// middle, centre out by its distance from the middle of them all (ties go left to right), and shuffle is a permutation fixed by `seed`.
QList<int> order(Order mode, const QList<Element> &elements, quint32 seed = 0);
QString orderName(Order mode);
// "Group · 5 words" for a row of several elements (a class name read as words, in the plural); empty for one element.
QString groupName(const Track &track);

// "1.40 s" for a time in ms; "820 ms" under a second is not used: the header always reads in seconds, as the prototype does.
QString seconds(double ms);
// An easing as the inspector names it: "Linear", "Ease out", "Soft out"; "Custom" for the rest.
QString easingName(const QString &easing);
QString triggerText(const QString &trigger);

}
