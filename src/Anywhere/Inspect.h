#pragma once
#include <QColor>
#include <QJsonObject>
#include <QLine>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QUrl>
#include <optional>
#include <vector>

// A surface design mode can point at (docs/ANYWHERE.md): a page in
// Omastrator's browser, any other window, or the desktop under no window.
// Art drawn on it is anchored to its key, so it follows the window or page.
struct Surface {
    enum class Kind { web, window, desktop };
    Kind kind = Kind::desktop;
    // "web:https://example.com/pricing", "window:foot" or "desktop:HDMI-A-1".
    QString key;
    // The window's class, or the monitor's name for the desktop.
    QString app;
    QString title;
    QUrl url;
    qint64 pid = 0;
    QString address;
    // Where it is on screen now, in layout coordinates.
    QRect rect;
    QString monitor;
    // Web: where the page's viewport starts on screen, and how far it's scrolled, so art scrolls with the page.
    QPoint viewport;
    QPointF scroll;
    // A browser window that isn't Omastrator's: its page can't be read, but can be opened in Omastrator's.
    bool otherBrowser = false;

    static QString kindName(Kind kind);
    // Web pages by address without the fragment; windows by class; the desktop by monitor.
    static QString keyFor(Kind kind, const QString &app, const QUrl &url);
    // "example.com/pricing", "foot", "Desktop (HDMI-A-1)": what the Desk's frames and the bar call it.
    QString label() const;
    // Art is kept relative to this point: the page's top-left (scrolled) or the window's.
    QPointF origin() const;
    QJsonObject toJson() const;
    static Surface fromJson(const QJsonObject &json);
};

// What is under the pointer: an element of a page, an accessible widget, a
// window, or just the desktop, with what is known about its look.
struct Inspection {
    // Recent inspections are kept by id, so the floating bar acts on what it showed.
    int id = 0;
    Surface surface;
    QRect bounds;
    // "dom", "accessibility", "window" or "screen".
    QString source;
    // Web: the tag; accessibility: the role ("push button"); a window: "window".
    QString role;
    // A selector for web elements, the accessible name, or the window's title.
    QString name;
    QString text;
    QColor color;
    QColor background;
    // The screen's colour under the pointer, where grim could read it.
    QColor pixel;
    QString fontFamily;
    double fontSize = 0;
    QString fontWeight;
    // Web: padding, margin, radius, line height and the rest, as the page computed them.
    QJsonObject styles;

    QJsonObject toJson() const;
    // "button.primary 120 × 40", as the hover label reads.
    QString summary() const;
};

namespace Inspect {
// One line of a measurement: its ends on screen and its length in pixels.
struct Measure {
    QLine line;
    int value = 0;
    QJsonObject toJson() const;
};
// Figma's Alt distances: the gaps between two boxes apart, or the insets of one inside the other.
std::vector<Measure> distances(const QRect &from, const QRect &to);

// Where a page's viewport starts in its browser window: the toolbars are on top, the sides are even.
QPoint viewportOrigin(const QRect &window, const QSizeF &inner);
// JavaScript answering the element under a point of the browser window: tag, selector, rect, colours, font and
// box styles. The page finds its own viewport in the window, as viewportOrigin does.
QString webScript(QPoint windowPoint, QSize windowSize);
// The page's answer as an inspection on `surface`, whose viewport and scroll are set from it.
std::optional<Inspection> fromWeb(const QJsonObject &answer, Surface surface);

// A Python program that prints the accessible object at a window point as JSON: `pid x y` with x and y inside the window.
QString accessibleScript();
// $OMASTRATOR_ATSPI, else python3 running accessibleScript(); empty when neither can run.
std::optional<QJsonObject> accessibleAt(qint64 pid, QPoint windowPoint, QString *error = nullptr);
// The helper's answer, placed on screen inside `window`.
std::optional<Inspection> fromAccessible(const QJsonObject &answer, const Surface &surface);

// A one-pixel PPM from grim, as a colour.
std::optional<QColor> parsePpm(const QByteArray &ppm);
}
