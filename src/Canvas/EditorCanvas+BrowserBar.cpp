#include "Canvas/EditorCanvasState.h"
#include "Document/BrowserAddress.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QFocusEvent>
#include <QKeyEvent>
#include <algorithm>
#include <functional>

// A Browser View's address bar and sign-in strip (docs/BROWSER-VIEW.md, section 4), drawn over the canvas at a constant screen size.

namespace {
constexpr double barHeight = 28;
constexpr double buttonSize = 24;
// Below this on screen the bar is the ordinary frame label with a globe.
constexpr double collapsedBelow = 240;
constexpr double stripHeight = 40;

QRectF buttonAt(double x, const QRectF &bar)
{
    return QRectF(x, bar.top() + (bar.height() - buttonSize) / 2, buttonSize, buttonSize);
}

// A QShortcut would need the window to be active; the field just handles its own keys.
class AddressEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;
    std::function<void()> onCancel;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && onCancel) {
            onCancel();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        QLineEdit::focusOutEvent(event);
        if (event->reason() != Qt::PopupFocusReason && onCancel)
            onCancel();
    }
};

void drawGlobe(QPainter &painter, const QRectF &box, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 1.1));
    painter.setBrush(Qt::NoBrush);
    const QRectF globe = box.adjusted(1, 1, -1, -1);
    painter.drawEllipse(globe);
    painter.drawEllipse(QRectF(globe.center().x() - globe.width() * 0.2, globe.top(), globe.width() * 0.4, globe.height()));
    painter.drawLine(QPointF(globe.left(), globe.center().y()), QPointF(globe.right(), globe.center().y()));
    painter.restore();
}

void drawArrow(QPainter &painter, const QRectF &box, bool back)
{
    const QPointF c = box.center();
    const double d = back ? -1 : 1;
    painter.drawLine(QPointF(c.x() - 5 * d, c.y()), QPointF(c.x() + 5 * d, c.y()));
    painter.drawLine(QPointF(c.x() + 5 * d, c.y()), QPointF(c.x() + 1 * d, c.y() - 4));
    painter.drawLine(QPointF(c.x() + 5 * d, c.y()), QPointF(c.x() + 1 * d, c.y() + 4));
}

void drawReload(QPainter &painter, const QRectF &box)
{
    const QRectF ring = QRectF(0, 0, 11, 11).translated(box.center() - QPointF(5.5, 5.5));
    painter.drawArc(ring, 40 * 16, 290 * 16);
    const QPointF tip = ring.center() + QPointF(4.2, -3.6);
    painter.drawLine(tip, tip + QPointF(3, 0.5));
    painter.drawLine(tip, tip + QPointF(0.3, 3.2));
}

void drawStop(QPainter &painter, const QRectF &box)
{
    const QPointF c = box.center();
    painter.drawLine(c + QPointF(-4, -4), c + QPointF(4, 4));
    painter.drawLine(c + QPointF(-4, 4), c + QPointF(4, -4));
}
}

std::vector<EditorCanvas::State::BrowserBarLayout> EditorCanvas::State::browserBars() const
{
    std::vector<BrowserBarLayout> bars;
    if (!session.hasDocument() || !browserHost)
        return bars;
    const VectorDocument &document = *session.document();
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    for (const VectorObject &object : document.objects) {
        if (!object.browser || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        const QRectF box = documentToView().mapRect(object.path.painterPath().boundingRect());
        BrowserBarLayout layout;
        layout.frame = object.id;
        layout.collapsed = box.width() < collapsedBelow;
        layout.bar = QRectF(box.left(), box.top() - 4 - barHeight, box.width(), barHeight);
        const double top = layout.bar.top();
        if (layout.collapsed) {
            // The label's own place, with the globe before the name.
            layout.name = QRectF(box.left(), box.top() - 5 - metrics.height(), std::min(metrics.horizontalAdvance(object.name) + 20, std::max(36.0, box.width())),
                                 metrics.height());
            bars.push_back(layout);
            continue;
        }
        double x = layout.bar.left() + 2;
        layout.back = buttonAt(x, layout.bar);
        layout.forward = buttonAt(x + buttonSize, layout.bar);
        layout.reload = buttonAt(x + 2 * buttonSize, layout.bar);
        x += 3 * buttonSize + 6;
        double right = layout.bar.right() - 2;
        const BrowserViewHost::Bar state = browserHost->bar(object.id);
        if (!state.deploy.isEmpty()) {
            const double width = std::min(metrics.horizontalAdvance(state.deploy) + 18, 170.0);
            if (right - x - width > 140) {
                layout.deploy = QRectF(right - width, top + 4, width, barHeight - 8);
                right -= width + 4;
            }
        }
        if (!state.build.isEmpty()) {
            const double width = std::min(metrics.horizontalAdvance(state.build) + 18 + (state.buildBusy ? 12 : 0), 190.0);
            if (right - x - width > 140) {
                layout.build = QRectF(right - width, top + 4, width, barHeight - 8);
                right -= width + 4;
            }
        }
        if (state.notYours) {
            const double width = metrics.horizontalAdvance(QStringLiteral("Not your site")) + 14;
            if (right - x - width > 140) {
                layout.tag = QRectF(right - width, top + 5, width, barHeight - 10);
                right -= width + 4;
            }
        }
        if (state.dev) {
            const double width = metrics.horizontalAdvance(QStringLiteral("dev")) + 14;
            if (right - x - width > 140) {
                layout.dev = QRectF(right - width, top + 5, width, barHeight - 10);
                right -= width + 4;
            }
        }
        right = addWidthButtons(layout, right, x, metrics);
        const double nameWidth = std::min(metrics.horizontalAdvance(object.name) + 8, std::max(0.0, (right - x) * 0.3));
        layout.name = QRectF(x, top + (barHeight - metrics.height()) / 2, nameWidth, metrics.height());
        x += nameWidth + 2;
        layout.address = QRectF(x, top + 3, std::max(0.0, right - x), barHeight - 6);
        bars.push_back(layout);
    }
    return bars;
}

void EditorCanvas::State::drawBrowserBars(QPainter &painter) const
{
    const auto bars = browserBars();
    if (bars.empty())
        return;
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QPalette &palette = canvas.palette();
    const QColor text = palette.color(QPalette::WindowText);
    const QColor quiet = palette.color(QPalette::PlaceholderText);
    painter.save();
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetricsF metrics(font);
    for (const BrowserBarLayout &layout : bars) {
        const VectorObject *object = session.document()->find(layout.frame);
        const bool selected = session.isSelected(layout.frame);
        if (layout.collapsed) {
            drawGlobe(painter, QRectF(layout.name.left(), layout.name.center().y() - 6, 12, 12), selected ? accent() : quiet);
            painter.setPen(selected ? accent() : quiet);
            painter.drawText(layout.name.adjusted(17, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(object->name, Qt::ElideRight, layout.name.width() - 17));
            continue;
        }
        const BrowserViewHost::Bar state = browserHost->bar(layout.frame);
        painter.setPen(QPen(palette.color(QPalette::Mid), 1));
        painter.setBrush(palette.color(QPalette::Window));
        painter.drawRoundedRect(layout.bar.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        painter.setBrush(Qt::NoBrush);
        const auto button = [&](const QRectF &rect, bool enabled, const auto &icon) {
            if (hover && rect.contains(*hover) && enabled) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette.color(QPalette::Midlight));
                painter.drawRoundedRect(rect, 5, 5);
                painter.setBrush(Qt::NoBrush);
            }
            painter.setPen(QPen(enabled ? text : quiet.darker(120), 1.4, Qt::SolidLine, Qt::RoundCap));
            icon();
        };
        button(layout.back, state.canGoBack, [&] { drawArrow(painter, layout.back, true); });
        button(layout.forward, state.canGoForward, [&] { drawArrow(painter, layout.forward, false); });
        button(layout.reload, true, [&] { state.loading ? drawStop(painter, layout.reload) : drawReload(painter, layout.reload); });
        painter.setPen(selected ? accent() : quiet);
        painter.drawText(layout.name, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(object->name, Qt::ElideRight, layout.name.width()));
        // The address field.
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.color(QPalette::Base));
        painter.drawRoundedRect(layout.address, 4, 4);
        const bool empty = object->browser->url.isEmpty();
        painter.setPen(empty ? quiet : text);
        const QString shown = empty ? QStringLiteral("Type a URL") : BrowserAddress::shown(object->browser->url);
        painter.drawText(layout.address.adjusted(8, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(shown, Qt::ElideMiddle, layout.address.width() - 14));
        drawWidthButtons(painter, layout);
        if (!layout.deploy.isNull()) {
            const bool hovered = hover && layout.deploy.contains(*hover) && !state.deployBusy;
            const QColor colour = state.deployFailed ? QColor(0xd9, 0x53, 0x4f) : accent();
            painter.setPen(QPen(colour, 1));
            // The idle button is the accent's fill; a stage or a result is only its outline.
            const bool idle = !state.deployBusy && !state.deployFailed && state.deploy == QLatin1String("Deploy");
            painter.setBrush(idle ? colour.lighter(hovered ? 115 : 100) : Qt::NoBrush);
            painter.drawRoundedRect(layout.deploy, 6, 6);
            painter.setPen(idle ? palette.color(QPalette::HighlightedText) : colour);
            painter.drawText(layout.deploy, Qt::AlignCenter, metrics.elidedText(state.deploy, Qt::ElideRight, layout.deploy.width() - 10));
            painter.setBrush(Qt::NoBrush);
        }
        if (!layout.build.isNull()) {
            const bool hovered = hover && layout.build.contains(*hover);
            painter.setPen(QPen(accent(), 1));
            painter.setBrush(hovered ? QBrush(QColor(accent().red(), accent().green(), accent().blue(), 40)) : Qt::NoBrush);
            painter.drawRoundedRect(layout.build, 6, 6);
            QRectF label = layout.build;
            if (state.buildBusy) {
                // The tray light's dot, so it is plain that something is working.
                painter.setPen(Qt::NoPen);
                painter.setBrush(accent());
                painter.drawEllipse(QPointF(label.left() + 11, label.center().y()), 3, 3);
                label.adjust(12, 0, 0, 0);
            }
            painter.setPen(accent());
            painter.setBrush(Qt::NoBrush);
            painter.drawText(label, Qt::AlignCenter, metrics.elidedText(state.build, Qt::ElideRight, label.width() - 10));
        }
        if (!layout.tag.isNull()) {
            painter.setPen(QPen(quiet, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(layout.tag, 8, 8);
            painter.drawText(layout.tag, Qt::AlignCenter, QStringLiteral("Not your site"));
        }
        if (!layout.dev.isNull()) {
            painter.setPen(QPen(accent(), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(layout.dev, 8, 8);
            painter.drawText(layout.dev, Qt::AlignCenter, QStringLiteral("dev"));
        }
        if (state.loading) {
            painter.setPen(QPen(accent(), 2));
            painter.drawLine(QPointF(layout.bar.left() + 6, layout.bar.bottom() + 1), QPointF(layout.bar.right() - 6, layout.bar.bottom() + 1));
        }
    }
    painter.restore();
}

std::optional<QUuid> EditorCanvas::State::browserBarAt(QPointF view) const
{
    for (const BrowserBarLayout &layout : browserBars()) {
        if (!layout.collapsed && layout.bar.contains(view))
            return layout.frame;
    }
    return std::nullopt;
}

QString EditorCanvas::State::browserBarTip(QPointF view) const
{
    for (const BrowserBarLayout &layout : browserBars()) {
        if (layout.collapsed)
            continue;
        if (layout.back.contains(view))
            return QStringLiteral("Back");
        if (layout.forward.contains(view))
            return QStringLiteral("Forward");
        if (layout.reload.contains(view))
            return browserHost->bar(layout.frame).loading ? QStringLiteral("Stop") : QStringLiteral("Reload");
        if (layout.deploy.contains(view))
            return browserHost->bar(layout.frame).deployTip;
        if (layout.build.contains(view))
            return browserHost->bar(layout.frame).buildTip;
        if (layout.tag.contains(view))
            return QStringLiteral("Not your site: changes stay on this machine. Click if it is.");
        if (layout.dev.contains(view))
            return QStringLiteral("Running from the project's dev server\n%1").arg(browserHost->bar(layout.frame).devTip);
        if (layout.editPage.contains(view))
            return editPage == layout.frame ? QStringLiteral("Stop editing the page") : QStringLiteral("Edit Page");
        for (const auto &[rect, width] : layout.widths) {
            if (rect.contains(view))
                return width == layout.designWidth ? QStringLiteral("Design width: %1").arg(width) : QStringLiteral("Preview at %1 wide").arg(width);
        }
    }
    return {};
}

bool EditorCanvas::State::browserBarPress(QPointF view)
{
    if (!browserHost)
        return false;
    if (signInPress(view))
        return true;
    for (const BrowserBarLayout &layout : browserBars()) {
        if (layout.collapsed || !layout.bar.contains(view))
            continue;
        if (layout.back.contains(view)) {
            browserHost->act(layout.frame, BrowserViewHost::Action::back);
        } else if (layout.forward.contains(view)) {
            browserHost->act(layout.frame, BrowserViewHost::Action::forward);
        } else if (layout.reload.contains(view)) {
            browserHost->act(layout.frame, browserHost->bar(layout.frame).loading ? BrowserViewHost::Action::stop : BrowserViewHost::Action::reload);
        } else if (layout.deploy.contains(view)) {
            // In Edit Page the frame stays unselected: its handles would sit over the page.
            if (editPage != layout.frame)
                session.select({layout.frame});
            browserHost->act(layout.frame, BrowserViewHost::Action::deployButton);
        } else if (layout.build.contains(view)) {
            if (editPage != layout.frame)
                session.select({layout.frame});
            browserHost->act(layout.frame, BrowserViewHost::Action::buildButton);
        } else if (layout.tag.contains(view)) {
            browserHost->act(layout.frame, BrowserViewHost::Action::thisIsMySite);
        } else if (layout.editPage.contains(view)) {
            if (editPage == layout.frame)
                leaveEditPage();
            else
                enterEditPage(layout.frame);
        } else if (const auto pressed = std::find_if(layout.widths.begin(), layout.widths.end(), [&](const auto &each) { return each.first.contains(view); });
                   pressed != layout.widths.end()) {
            // The same button, or the design width's, lets the preview go.
            const int width = pressed->second;
            if (width == layout.designWidth || (held && held->frame == layout.frame && held->width == width && session.isPreviewOnly()))
                endHeldPreview();
            else
                holdPreview(layout.frame, width);
        } else if (layout.address.contains(view)) {
            // The address is a design edit, so a locked frame keeps it; the other controls are view state, like Browse.
            if (!session.document()->isEffectivelyLocked(layout.frame)) {
                session.select({layout.frame});
                openAddressEditor(layout.frame);
            }
        } else if (layout.name.contains(view)) {
            // The name is the frame's handle, as any frame's label is.
            return false;
        }
        canvas.update();
        return true;
    }
    return false;
}

// The address field -------------------------------------------------------------------

void EditorCanvas::State::openAddressEditor(const QUuid &frame)
{
    closeAddressEditor();
    const VectorObject *object = session.hasDocument() ? session.document()->find(frame) : nullptr;
    if (!object || !object->browser)
        return;
    QRectF field;
    for (const BrowserBarLayout &layout : browserBars()) {
        if (layout.frame == frame && !layout.collapsed)
            field = layout.address;
    }
    // A frame too small for its bar still takes an address, in a field over its label.
    if (field.isNull()) {
        const QRectF box = documentToView().mapRect(object->path.painterPath().boundingRect());
        field = QRectF(box.left(), box.top() - 4 - barHeight + 3, std::max(200.0, box.width()), barHeight - 6);
    }
    addressFrame = frame;
    auto *edit = new AddressEdit(&canvas);
    edit->setObjectName(QStringLiteral("browserAddressEdit"));
    edit->setPlaceholderText(QStringLiteral("Type a URL"));
    edit->setText(object->browser->url.isEmpty() ? QString() : object->browser->url.toString());
    edit->setGeometry(field.toRect());
    edit->selectAll();
    edit->show();
    edit->setFocus(Qt::OtherFocusReason);
    addressEdit = edit;
    QObject::connect(edit, &QLineEdit::returnPressed, edit, [this, edit] {
        const std::optional<QUrl> url = BrowserAddress::parse(edit->text());
        if (!url) {
            emit canvas.notice(QStringLiteral("Browser View opens http and https pages only."));
            return;
        }
        const QUuid frame = addressFrame;
        closeAddressEditor();
        session.setBrowserUrl(frame, *url);
        canvas.setFocus(Qt::OtherFocusReason);
    });
    // Escape, or clicking elsewhere, leaves the address as it was.
    edit->onCancel = [this, edit] {
        if (addressEdit != edit)
            return;
        closeAddressEditor();
        canvas.setFocus(Qt::OtherFocusReason);
    };
}

void EditorCanvas::State::closeAddressEditor()
{
    if (addressEdit) {
        // Cleared first: hiding the field loses its focus, which asks to close it again.
        QLineEdit *edit = addressEdit;
        addressEdit = nullptr;
        edit->hide();
        edit->deleteLater();
    }
    addressFrame = QUuid();
}

void EditorCanvas::openAddressEditor(const QUuid &frame)
{
    m_state->openAddressEditor(frame);
}

bool EditorCanvas::isEditingAddress() const
{
    return m_state->addressEdit != nullptr;
}

// The sign-in strip -----------------------------------------------------------------------

std::optional<EditorCanvas::State::SignInStrip> EditorCanvas::State::signInStrip() const
{
    if (!browserHost || !browserHost->signInOffered() || !session.hasDocument())
        return std::nullopt;
    const VectorDocument &document = *session.document();
    for (const VectorObject &object : document.objects) {
        if (!object.browser || !document.isOnCurrentPage(object.id) || !document.isEffectivelyVisible(object.id))
            continue;
        const QRectF box = documentToView().mapRect(object.path.painterPath().boundingRect());
        // The offer waits for a frame big enough to read it in; it stays with the first one.
        if (box.width() < collapsedBelow || box.height() < 3 * stripHeight)
            return std::nullopt;
        SignInStrip strip;
        strip.strip = QRectF(box.left() + 8, box.bottom() - 8 - stripHeight, box.width() - 16, stripHeight);
        strip.signIn = QRectF(strip.strip.right() - 8 - 84 - 74 - 6, strip.strip.top() + 8, 74, stripHeight - 16);
        strip.notNow = QRectF(strip.strip.right() - 8 - 84, strip.strip.top() + 8, 84, stripHeight - 16);
        return strip;
    }
    return std::nullopt;
}

void EditorCanvas::State::drawSignInStrip(QPainter &painter) const
{
    const auto strip = signInStrip();
    if (!strip)
        return;
    QFont font = canvas.font();
    font.setPixelSize(12);
    const QPalette &palette = canvas.palette();
    painter.save();
    painter.setFont(font);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette.color(QPalette::Mid), 1));
    painter.setBrush(palette.color(QPalette::Window));
    painter.drawRoundedRect(strip->strip, 8, 8);
    painter.setPen(palette.color(QPalette::WindowText));
    const QRectF words(strip->strip.left() + 12, strip->strip.top(), strip->signIn.left() - strip->strip.left() - 20, strip->strip.height());
    painter.drawText(words, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                     QStringLiteral("Sign in to Omastrator's browser to see sites you're logged in to."));
    painter.setBrush(Qt::NoBrush);
    const auto button = [&](const QRectF &rect, const QString &label, bool primary) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(primary ? accent() : palette.color(QPalette::Midlight));
        painter.drawRoundedRect(rect, 5, 5);
        painter.setPen(primary ? QColor(Qt::white) : palette.color(QPalette::WindowText));
        painter.drawText(rect, Qt::AlignCenter, label);
    };
    button(strip->signIn, QStringLiteral("Sign In…"), true);
    button(strip->notNow, QStringLiteral("Not Now"), false);
    painter.restore();
}

bool EditorCanvas::State::signInPress(QPointF view)
{
    const auto strip = signInStrip();
    if (!strip || !strip->strip.contains(view))
        return false;
    if (strip->signIn.contains(view))
        browserHost->signIn();
    else if (strip->notNow.contains(view))
        browserHost->dismissSignIn();
    canvas.update();
    return true;
}

// The bar's right-click menu ------------------------------------------------------------------

bool EditorCanvas::State::browserBarMenu(QPointF view, QPoint global)
{
    const std::optional<QUuid> frame = browserBarAt(view);
    if (!frame || !browserHost)
        return false;
    const VectorObject *object = session.document()->find(*frame);
    if (!object || !object->browser)
        return false;
    if (editPage != *frame)
        session.select({*frame});
    auto *menu = new QMenu(&canvas);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QUrl url = object->browser->url;
    QAction *copy = menu->addAction(QStringLiteral("Copy URL"));
    copy->setEnabled(!url.isEmpty());
    QObject::connect(copy, &QAction::triggered, menu, [url] { QApplication::clipboard()->setText(url.toString()); });
    QAction *open = menu->addAction(QStringLiteral("Open in My Chromium"));
    open->setEnabled(!url.isEmpty());
    QObject::connect(open, &QAction::triggered, menu, [url] { QDesktopServices::openUrl(url); });
    QAction *page = menu->addAction(QStringLiteral("Edit Page"));
    page->setCheckable(true);
    page->setChecked(editPage == *frame);
    QObject::connect(page, &QAction::triggered, menu, [this, id = *frame] {
        if (editPage == id)
            leaveEditPage();
        else
            enterEditPage(id);
    });
    menu->addSeparator();
    // On a button it sets that width; elsewhere, the width a preview is showing.
    const auto button = [&] {
        for (const BrowserBarLayout &layout : browserBars()) {
            if (layout.frame != *frame)
                continue;
            for (const auto &[rect, width] : layout.widths) {
                if (rect.contains(view))
                    return std::optional<int>(width);
            }
        }
        return std::optional<int>();
    }();
    QAction *design = menu->addAction(QStringLiteral("Set as Design Width"));
    design->setEnabled(button || previewedWidth(*frame));
    QObject::connect(design, &QAction::triggered, menu, [this, id = *frame, button] {
        if (const std::optional<int> width = button ? button : previewedWidth(id))
            setDesignWidth(id, *width);
    });
    QAction *hard = menu->addAction(QStringLiteral("Reload Ignoring Cache"));
    QObject::connect(hard, &QAction::triggered, menu, [this, id = *frame] { browserHost->act(id, BrowserViewHost::Action::reloadIgnoringCache); });
    QAction *signIn = menu->addAction(QStringLiteral("Sign in to Omastrator's browser…"));
    QObject::connect(signIn, &QAction::triggered, menu, [this] { browserHost->signIn(); });
    browserHost->extendBarMenu(*frame, menu);
    menu->popup(global);
    return true;
}
