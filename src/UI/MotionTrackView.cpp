#include "UI/MotionTrackView.h"
#include "UI/MotionTimeline.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

// Where each part sits, from the rows the timeline holds: time rows first, then a divider, a second ruler and the rows
// driven by scrolling.
struct MotionTrackView::Layout {
    struct Row {
        QString id;
        int top = 0;
        bool scroll = false;
        // -1 for a track's own row; else the element of an open group that this row is.
        int bar = -1;
    };
    QList<Row> rows;
    int timeRuler = -1;
    int scrollDivider = -1;
    int scrollRuler = -1;
    int timeBottom = 0;
    int scrollBottom = 0;
    int height = 0;
};

namespace {
// The first "nice" step that keeps ticks at least `minimum` px apart.
double niceStep(double span, double pixels, double minimum, bool time)
{
    static const double times[] = {50, 100, 200, 250, 500, 1000, 2000, 2500, 5000, 10000, 30000, 60000};
    static const double lengths[] = {50, 100, 200, 250, 500, 1000, 2000, 5000, 10000, 20000, 50000};
    const double *steps = time ? times : lengths;
    const int count = time ? 12 : 11;
    for (int i = 0; i < count; ++i)
        if (steps[i] / span * pixels >= minimum)
            return steps[i];
    return steps[count - 1];
}
}

MotionTrackView::MotionTrackView(MotionTimeline &owner, QWidget *parent) : QWidget(parent), m_owner(owner)
{
    setObjectName(QStringLiteral("motionTracks"));
    setAccessibleName(QStringLiteral("Motion tracks"));
    setMouseTracking(false);
    setFocusPolicy(Qt::NoFocus);
    setMinimumWidth(360);
}

QSize MotionTrackView::sizeHint() const
{
    return {720, contentHeight()};
}

QSize MotionTrackView::minimumSizeHint() const
{
    return {360, contentHeight()};
}

MotionTrackView::Layout MotionTrackView::layout() const
{
    Layout out;
    const Motion::Timeline &timeline = m_owner.timeline();
    int y = 0;
    if (timeline.hasTime()) {
        out.timeRuler = y;
        y += rulerHeight;
        for (const Motion::Track &track : timeline.tracks) {
            if (track.isScroll())
                continue;
            out.rows.append({track.id, y, false, -1});
            y += rowHeight;
            if (m_owner.isExpanded(track.id) && track.bars.size() > 1) {
                for (int bar = 0; bar < track.bars.size(); ++bar) {
                    out.rows.append({track.id, y, false, bar});
                    y += rowHeight;
                }
            }
        }
        out.timeBottom = y;
    }
    if (timeline.hasScroll()) {
        out.scrollDivider = y;
        y += rulerHeight;
        out.scrollRuler = y;
        y += rulerHeight;
        for (const Motion::Track &track : timeline.tracks) {
            if (!track.isScroll())
                continue;
            out.rows.append({track.id, y, true, -1});
            y += rowHeight;
            if (m_owner.isExpanded(track.id) && track.bars.size() > 1) {
                for (int bar = 0; bar < track.bars.size(); ++bar) {
                    out.rows.append({track.id, y, true, bar});
                    y += rowHeight;
                }
            }
        }
        out.scrollBottom = y;
    }
    out.height = y;
    return out;
}

int MotionTrackView::contentHeight() const
{
    return std::max(rulerHeight + rowHeight, layout().height) + 6;
}

int MotionTrackView::timeTop() const
{
    return layout().timeRuler;
}

int MotionTrackView::scrollTop() const
{
    return layout().scrollRuler;
}

double MotionTrackView::timeSpan() const
{
    return std::max(500.0, m_owner.timeline().duration) * 1.04;
}

double MotionTrackView::scrollSpan() const
{
    const Motion::Timeline &timeline = m_owner.timeline();
    double span = std::max(1.0, timeline.scrollMax);
    for (const Motion::Track &track : timeline.tracks)
        if (track.isScroll())
            span = std::max(span, track.end);
    return span;
}

int MotionTrackView::xForTime(double ms) const
{
    const double width = std::max(1, this->width() - labelWidth - gutter * 2);
    return int(std::lround(labelWidth + gutter + ms / timeSpan() * width));
}

double MotionTrackView::timeAtX(int x) const
{
    const double width = std::max(1, this->width() - labelWidth - gutter * 2);
    return std::clamp((x - labelWidth - gutter) / width * timeSpan(), 0.0, std::max(0.0, m_owner.timeline().duration));
}

int MotionTrackView::xForScroll(double px) const
{
    const double width = std::max(1, this->width() - labelWidth - gutter * 2);
    return int(std::lround(labelWidth + gutter + px / scrollSpan() * width));
}

double MotionTrackView::scrollAtX(int x) const
{
    const double width = std::max(1, this->width() - labelWidth - gutter * 2);
    return std::clamp((x - labelWidth - gutter) / width * scrollSpan(), 0.0, std::max(0.0, m_owner.timeline().scrollMax));
}

QRect MotionTrackView::rulerRect(bool scroll) const
{
    const Layout out = layout();
    const int top = scroll ? out.scrollRuler : out.timeRuler;
    return top < 0 ? QRect() : QRect(labelWidth, top, width() - labelWidth, rulerHeight);
}

QRect MotionTrackView::rowRect(const QString &id) const
{
    for (const Layout::Row &row : layout().rows)
        if (row.id == id && row.bar < 0)
            return QRect(0, row.top, width(), rowHeight);
    return {};
}

QRect MotionTrackView::childRect(const QString &id, int bar) const
{
    for (const Layout::Row &row : layout().rows)
        if (row.id == id && row.bar == bar && bar >= 0)
            return QRect(0, row.top, width(), rowHeight);
    return {};
}

QRect MotionTrackView::expanderRect(const QString &id) const
{
    const Motion::Track *track = m_owner.timeline().find(id);
    if (!track || track->bars.size() < 2)
        return {};
    const QRect row = rowRect(id);
    return row.isValid() ? QRect(2, row.top() + 6, 14, 18) : QRect();
}

QRect MotionTrackView::barRect(const QString &id, int bar, bool inChild) const
{
    const Layout out = layout();
    const Motion::Track *track = m_owner.timeline().find(id);
    if (!track || bar < 0 || bar >= track->bars.size())
        return {};
    for (const Layout::Row &row : out.rows) {
        // A track's own row holds every bar; an element's row holds only its own.
        if (row.id != id || (inChild ? row.bar != bar : row.bar >= 0))
            continue;
        const Motion::Bar &each = track->bars[bar];
        const int left = row.scroll ? xForScroll(each.start) : xForTime(each.start);
        const int right = row.scroll ? xForScroll(each.start + each.length) : xForTime(each.start + each.length);
        // Bars stack thinly, so a stagger reads at a glance; a row holds at most ten before they overlap.
        const int count = row.bar >= 0 ? 1 : std::min<int>(track->bars.size(), 10);
        const int inner = rowHeight - 10;
        const int height = std::max(3, std::min(12, inner / count));
        const int at = row.bar >= 0 ? 0 : std::min(bar, count - 1);
        const int top = row.top + 5 + (count == 1 ? (inner - height) / 2 : at * (inner - height) / std::max(1, count - 1));
        return QRect(left, top, std::max(3, right - left), height);
    }
    return {};
}

void MotionTrackView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &colors = palette();
    const QColor text = colors.color(QPalette::WindowText);
    QColor dim = text;
    dim.setAlphaF(0.55f);
    QColor line = text;
    line.setAlphaF(0.14f);
    const QColor accent = colors.color(QPalette::Highlight);
    const Motion::Timeline &timeline = m_owner.timeline();
    const Layout out = layout();
    QFont small = font();
    small.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
    QFont bold = font();
    bold.setBold(true);

    if (out.rows.isEmpty()) {
        painter.setPen(dim);
        const QString message = m_owner.status().isEmpty() ? tr("No motion on this page yet.") : m_owner.status();
        painter.drawText(rect().adjusted(16, 0, -16, 0), Qt::AlignCenter | Qt::TextWordWrap, message);
        return;
    }

    const auto ruler = [&](int top, bool scroll) {
        painter.setFont(small);
        const double span = scroll ? scrollSpan() : timeSpan();
        const double pixels = std::max(1, width() - labelWidth - gutter * 2);
        const double step = niceStep(span, pixels, 64, !scroll);
        painter.setPen(line);
        painter.drawLine(labelWidth, top + rulerHeight - 1, width(), top + rulerHeight - 1);
        for (double value = 0; value <= span; value += step) {
            const int x = scroll ? xForScroll(value) : xForTime(value);
            painter.setPen(line);
            painter.drawLine(x, top + rulerHeight - 6, x, top + rulerHeight - 1);
            painter.setPen(dim);
            const QString label = scroll ? QStringLiteral("%1 px").arg(qRound(value)) : (step >= 1000 ? QStringLiteral("%1 s").arg(value / 1000) : QStringLiteral("%1 s").arg(value / 1000, 0, 'f', 2));
            painter.drawText(QRect(x + 3, top, 60, rulerHeight - 6), Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    };

    if (out.timeRuler >= 0)
        ruler(out.timeRuler, false);
    if (out.scrollDivider >= 0) {
        painter.setFont(small);
        painter.setPen(dim);
        painter.drawText(QRect(gutter, out.scrollDivider, labelWidth, rulerHeight), Qt::AlignLeft | Qt::AlignVCenter, tr("On scroll"));
        painter.setPen(line);
        painter.drawLine(0, out.scrollDivider + 2, width(), out.scrollDivider + 2);
        ruler(out.scrollRuler, true);
    }

    for (const Layout::Row &row : out.rows) {
        const Motion::Track *track = timeline.find(row.id);
        if (!track)
            continue;
        const bool child = row.bar >= 0;
        const QRect area(0, row.top, width(), rowHeight);
        const bool picked = row.id == m_owner.selectedId() && (child ? m_owner.selectedBar() == row.bar : m_owner.selectedBar() < 0);
        if (picked) {
            QColor plate = accent;
            plate.setAlphaF(0.16f);
            painter.fillRect(area, plate);
        }
        painter.setPen(line);
        painter.drawLine(0, row.top + rowHeight - 1, width(), row.top + rowHeight - 1);
        painter.setFont(child ? small : bold);
        painter.setPen(text);
        const int indent = child ? gutter + 14 : gutter + (track->bars.size() > 1 ? 8 : 0);
        const QFontMetrics metrics(child ? small : bold);
        if (child) {
            painter.drawText(QRect(indent, row.top + 3, labelWidth - indent - 8, rowHeight - 6), Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(track->bars[row.bar].label, Qt::ElideRight, labelWidth - indent - 8));
        } else {
            painter.drawText(QRect(indent, row.top + 3, labelWidth - indent - 8, 14), Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(track->label, Qt::ElideRight, labelWidth - indent - 8));
            // A group opens into its elements.
            if (track->bars.size() > 1) {
                painter.setFont(small);
                painter.setPen(dim);
                painter.drawText(expanderRect(row.id), Qt::AlignCenter, m_owner.isExpanded(row.id) ? QStringLiteral("▾") : QStringLiteral("▸"));
            }
            painter.setFont(small);
            painter.setPen(dim);
            const QString detail = track->potential ? tr("hover to preview") : track->detail;
            painter.drawText(QRect(indent, row.top + 15, labelWidth - indent - 8, 12), Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetrics(small).elidedText(detail, Qt::ElideRight, labelWidth - indent - 8));
        }
        const int first = child ? row.bar : 0;
        const int last = child ? row.bar + 1 : int(track->bars.size());
        for (int i = first; i < last; ++i) {
            const QRect bar = barRect(row.id, i, child);
            QColor fill = track->kind == QLatin1String("gsap") ? dim : accent;
            if (track->potential) {
                painter.setPen(QPen(fill, 1, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
            }
            painter.drawRoundedRect(bar, 2, 2);
            if (track->bars[i].loops) {
                painter.setPen(text);
                painter.setFont(small);
                painter.drawText(bar.adjusted(0, 0, 14, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("↻"));
            }
        }
    }

    // The playheads: a line down each region and a handle on its ruler.
    const auto head = [&](int x, int top, int bottom) {
        painter.setPen(QPen(text, 1.5));
        painter.drawLine(x, top, x, bottom);
        QPolygon handle;
        handle << QPoint(x - 5, top) << QPoint(x + 5, top) << QPoint(x, top + 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(text);
        painter.drawPolygon(handle);
    };
    if (out.timeRuler >= 0)
        head(xForTime(m_owner.playhead()), out.timeRuler, out.timeBottom);
    if (out.scrollRuler >= 0)
        head(xForScroll(m_owner.scrollPlayhead()), out.scrollRuler, out.scrollBottom);
}

void MotionTrackView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint at = event->position().toPoint();
    const Layout out = layout();
    // A press on a ruler, or on a playhead's line, scrubs; anywhere else on a row picks it.
    const bool onTimeRuler = out.timeRuler >= 0 && at.y() >= out.timeRuler && at.y() < out.timeRuler + rulerHeight && at.x() >= labelWidth;
    const bool onScrollRuler = out.scrollRuler >= 0 && at.y() >= out.scrollRuler && at.y() < out.scrollRuler + rulerHeight && at.x() >= labelWidth;
    const bool nearTime = out.timeRuler >= 0 && at.y() < out.timeBottom && std::abs(at.x() - xForTime(m_owner.playhead())) <= 5;
    const bool nearScroll = out.scrollRuler >= 0 && at.y() >= out.scrollRuler && std::abs(at.x() - xForScroll(m_owner.scrollPlayhead())) <= 5;
    if (onTimeRuler || nearTime) {
        m_drag = Drag::time;
        m_owner.m_scrubbing = true;
        m_owner.scrubTo(timeAtX(at.x()));
        return;
    }
    if (onScrollRuler || nearScroll) {
        m_drag = Drag::scroll;
        m_owner.m_scrubbing = true;
        m_owner.scrubScrollTo(scrollAtX(at.x()));
        return;
    }
    for (const Layout::Row &row : out.rows) {
        if (at.y() >= row.top && at.y() < row.top + rowHeight) {
            if (row.bar < 0 && expanderRect(row.id).contains(at)) {
                m_owner.setExpanded(row.id, !m_owner.isExpanded(row.id));
                return;
            }
            m_owner.selectBar(row.id, row.bar);
            return;
        }
    }
}

void MotionTrackView::mouseMoveEvent(QMouseEvent *event)
{
    const int x = event->position().toPoint().x();
    if (m_drag == Drag::time)
        m_owner.scrubTo(timeAtX(x));
    else if (m_drag == Drag::scroll)
        m_owner.scrubScrollTo(scrollAtX(x));
}

void MotionTrackView::mouseReleaseEvent(QMouseEvent *)
{
    m_drag = Drag::none;
    m_owner.m_scrubbing = false;
}
