#include "Canvas/EditorCanvasState.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

// The Browser View switch (docs/BROWSER-VIEW.md, "The Browser View switch"): the Frame tool's frames show a web page when it's
// on. A Browser View carries it at its bar's right end; any other frame shows it as a pill at its top-right while it's
// selected or under the pointer.

namespace {
constexpr double pillHeight = 18;
constexpr double knobTrack = 22;

QString pillWord()
{
    return QStringLiteral("Browser View");
}

void drawTrack(QPainter &painter, const QRectF &track, bool on, const QColor &accent, const QPalette &palette)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(on ? accent : palette.color(QPalette::Mid));
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const double knob = track.height() - 4;
    const QPointF centre(on ? track.right() - 2 - knob / 2 : track.left() + 2 + knob / 2, track.center().y());
    painter.setBrush(palette.color(QPalette::Base));
    painter.drawEllipse(centre, knob / 2, knob / 2);
}
}

std::optional<QUuid> EditorCanvas::State::hoveredFrame() const
{
    if (!hover || !session.hasDocument())
        return std::nullopt;
    const VectorDocument &document = *session.document();
    std::optional<QUuid> found;
    double smallest = 0;
    for (const VectorObject &object : document.objects) {
        if (object.kind != ObjectKind::frame || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        const QRectF box = documentToView().mapRect(object.path.bounds());
        // The row above the frame, where its name and switch sit, counts as the frame.
        if (!box.adjusted(0, -(pillHeight + 8), 0, 0).contains(*hover))
            continue;
        // The innermost frame under the pointer, as nesting has it.
        const double area = box.width() * box.height();
        if (!found || area < smallest) {
            found = object.id;
            smallest = area;
        }
    }
    return found;
}

std::vector<EditorCanvas::State::BrowserSwitch> EditorCanvas::State::browserSwitches() const
{
    std::vector<BrowserSwitch> switches;
    if (!browserHost || !session.hasDocument())
        return switches;
    const VectorDocument &document = *session.document();
    // A Browser View with its whole bar has the switch in it, always.
    for (const BrowserBarLayout &layout : browserBars()) {
        if (!layout.collapsed && !layout.toggle.isNull())
            switches.push_back({layout.frame, layout.toggle, true, false});
    }
    const std::optional<QUuid> hovered = switchHover;
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    const double width = metrics.horizontalAdvance(pillWord()) + knobTrack + 18;
    for (const VectorObject &object : document.objects) {
        if (object.kind != ObjectKind::frame || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        if (std::any_of(switches.begin(), switches.end(), [&](const BrowserSwitch &each) { return each.frame == object.id; }))
            continue;
        // Only the frame being worked on shows it, so a canvas of frames stays quiet.
        if (!session.isSelected(object.id) && hovered != object.id)
            continue;
        if (editPage == object.id || document.isEffectivelyLocked(object.id))
            continue;
        const QRectF box = documentToView().mapRect(object.path.bounds());
        const double left = std::max(box.left(), box.right() - width);
        switches.push_back({object.id, QRectF(left, box.top() - 4 - pillHeight, width, pillHeight), object.showsPage(), true});
    }
    return switches;
}

void EditorCanvas::State::drawBrowserSwitches(QPainter &painter) const
{
    const auto switches = browserSwitches();
    if (switches.empty())
        return;
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QPalette &palette = canvas.palette();
    painter.save();
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const BrowserSwitch &each : switches) {
        const bool hovered = hover && each.rect.contains(*hover);
        QRectF track(each.rect.right() - 4 - knobTrack, each.rect.center().y() - 6, knobTrack, 12);
        if (each.pill) {
            painter.setPen(QPen(palette.color(QPalette::Mid), 1));
            painter.setBrush(hovered ? palette.color(QPalette::Midlight) : palette.color(QPalette::Window));
            painter.drawRoundedRect(each.rect.adjusted(0.5, 0.5, -0.5, -0.5), pillHeight / 2, pillHeight / 2);
            painter.setPen(each.on ? accent() : palette.color(QPalette::WindowText));
            painter.drawText(QRectF(each.rect.left() + 8, each.rect.top(), track.left() - each.rect.left() - 12, each.rect.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, pillWord());
        } else if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(palette.color(QPalette::Midlight));
            painter.drawRoundedRect(each.rect, 5, 5);
        }
        drawTrack(painter, track, each.on, accent(), palette);
    }
    painter.restore();
}

QString EditorCanvas::State::browserSwitchTip(QPointF view) const
{
    for (const BrowserSwitch &each : browserSwitches()) {
        if (each.rect.contains(view))
            return each.on ? QStringLiteral("Turn off Browser View: the page and its project's server freeze until it's back on")
                           : QStringLiteral("Turn on Browser View: this frame shows a live page, run from its project");
    }
    return {};
}

bool EditorCanvas::State::browserSwitchPress(QPointF view)
{
    for (const BrowserSwitch &each : browserSwitches()) {
        if (!each.rect.contains(view))
            continue;
        flipBrowserView(each.frame);
        return true;
    }
    return false;
}

void EditorCanvas::State::flipBrowserView(const QUuid &frame)
{
    if (!session.hasDocument() || session.document()->isEffectivelyLocked(frame))
        return;
    const bool on = !session.browserViewOn(frame);
    if (!on && editPage == frame)
        leaveEditPage();
    closeAddressEditor();
    session.setBrowserViewOn(frame, on);
    // A locked document keeps the switch where it was.
    if (session.browserViewOn(frame) != on)
        return;
    if (browserHost)
        browserHost->browserViewSwitched(frame, on);
    session.select({frame});
    const VectorObject *object = session.document()->find(frame);
    // A frame with no page yet asks for one, as a new Browser View always has.
    if (on && object && object->browser && object->browser->url.isEmpty())
        openAddressEditor(frame);
    canvas.update();
}

void EditorCanvas::flipBrowserView(const QUuid &frame)
{
    m_state->flipBrowserView(frame);
}
