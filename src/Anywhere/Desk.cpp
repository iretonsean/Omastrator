#include "Anywhere/Desk.h"
#include "Document/PathOperations.h"
#include <QDir>

namespace {
constexpr double gap = 80;
constexpr double margin = 120;
constexpr double rowWidth = 6000;
// The rectangle every frame starts with; frames() finds them by it.
const QString frameMarker = QStringLiteral("Frame");

QString dataHome()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    return given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
}
}

namespace Desk {
QString defaultPath()
{
    return QDir(dataHome()).filePath(QStringLiteral("omastrator/desk.omai"));
}

VectorDocument blank()
{
    VectorDocument document = VectorDocument::blank(QSizeF(3200, 2000));
    document.background = QColor(0xf4, 0xf4, 0xf2);
    document.find(document.layers().front())->name = QStringLiteral("Frames");
    return document;
}

QString label(const QString &source, const QDateTime &time)
{
    return QStringLiteral("%1 · %2").arg(source, time.toString(QStringLiteral("HH:mm")));
}

std::vector<std::pair<QUuid, QString>> frames(const VectorDocument &desk)
{
    std::vector<std::pair<QUuid, QString>> found;
    for (const VectorObject &object : desk.objects) {
        if (object.kind != ObjectKind::group)
            continue;
        const std::vector<QUuid> children = desk.children(object.id);
        if (!children.empty() && desk.find(children.front())->name == frameMarker)
            found.emplace_back(object.id, object.name);
    }
    return found;
}

// Lays one frame out on `next`, after the frames already there.
static QUuid place(VectorDocument &next, const Frame &frame, QString *error)
{
    auto failed = [&](const QString &message) {
        if (error)
            *error = message;
        return QUuid();
    };

    // The art's own extent, from its surface's corner.
    QRectF artBounds;
    std::vector<QUuid> artRoots;
    if (!frame.art.layers().empty()) {
        artRoots = frame.art.children(frame.art.layers().front());
        if (!artRoots.empty())
            artBounds = frame.art.bounds(artRoots, true);
    }
    QSizeF size = frame.size;
    if (size.isEmpty() && !frame.screenshot.isNull())
        size = frame.screenshot.size();
    if (size.isEmpty())
        size = QSizeF(std::max(200.0, artBounds.right() + 24), std::max(140.0, artBounds.bottom() + 24));
    if (size.isEmpty())
        return failed(QStringLiteral("There's nothing to send."));

    // After the last frame, wrapping into a new row past the row width.
    QPointF at(margin, margin + 30);
    double rowBottom = at.y();
    for (const auto &[id, name] : frames(next)) {
        const QRectF placed = next.bounds(id);
        rowBottom = std::max(rowBottom, placed.bottom());
        at = QPointF(placed.right() + gap, placed.top());
    }
    if (at.x() + size.width() > rowWidth && at.x() > margin)
        at = QPointF(margin, rowBottom + gap + 30);

    const QUuid layer = next.layers().back();
    const QString title = label(frame.source, frame.time);
    // Its own artboard, named after the frame; the first frame materialises the Desk's as [0].
    std::vector<Artboard> boards = next.allArtboards();
    boards.push_back({QUuid::createUuid(), title, QRectF(at, size), Qt::white});
    next.setArtboards(boards);
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = title;
    const QUuid groupId = group.id;
    next.insert(group, layer);

    VectorObject border;
    border.name = frameMarker;
    border.path = Shapes::rectangle(QRectF(at, size));
    border.fill = Paint::solid(Qt::white);
    border.stroke.paint = Paint::solid(QColor(0, 0, 0, 40));
    border.stroke.width = 1;
    next.insert(border, groupId);

    if (!frame.screenshot.isNull()) {
        VectorObject shot;
        shot.kind = ObjectKind::image;
        shot.name = QStringLiteral("Screenshot");
        shot.image = frame.screenshot.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        shot.transform = QTransform::fromScale(size.width() / frame.screenshot.width(), size.height() / frame.screenshot.height())
                         * QTransform::fromTranslate(at.x(), at.y());
        next.insert(shot, groupId);
    }

    // The art keeps its place over the screenshot.
    for (const QUuid &root : artRoots) {
        std::vector<VectorObject> copies = frame.art.copySubtree(root);
        if (copies.empty())
            continue;
        copies.front().parentID = groupId;
        const QUuid copyId = copies.front().id;
        for (VectorObject &copy : copies)
            next.objects.insert(next.objects.begin() + next.indexOf(groupId) + 1 + int(next.descendants(groupId).size()), std::move(copy));
        next.transform(copyId, QTransform::fromTranslate(at.x(), at.y()), false, false);
    }

    VectorObject caption;
    caption.kind = ObjectKind::text;
    caption.name = QStringLiteral("Label");
    caption.text.text = title;
    caption.text.size = 14;
    caption.fill = Paint::solid(QColor(0x55, 0x55, 0x55));
    caption.stroke.width = 0;
    caption.stroke.paint = Paint::none();
    caption.transform = QTransform::fromTranslate(at.x(), at.y() - 10);
    next.insert(caption, groupId);

    // The artboard grows to keep every frame on it.
    const QRectF all = next.bounds(groupId, true);
    next.size = QSizeF(std::max(next.size.width(), all.right() + margin), std::max(next.size.height(), all.bottom() + margin));
    return groupId;
}

QUuid addFrame(EditorSession &desk, const Frame &frame, QString *error)
{
    const std::vector<QUuid> ids = addFrames(desk, {frame}, frame.step.isEmpty() ? QStringLiteral("Send to Desk") : frame.step, error);
    return ids.empty() ? QUuid() : ids.front();
}

std::vector<QUuid> addFrames(EditorSession &desk, const std::vector<Frame> &frames, const QString &step, QString *error)
{
    auto failed = [&](const QString &message) {
        if (error)
            *error = message;
        return std::vector<QUuid>{};
    };
    if (!desk.hasDocument())
        return failed(QStringLiteral("The Desk isn't open."));
    if (desk.isInteracting())
        return failed(QStringLiteral("Finish the edit on the Desk first."));
    if (desk.isDocumentLocked())
        return failed(EditorSession::lockedNotice());
    VectorDocument next = *desk.document();
    if (next.layers().empty())
        next = blank();
    std::vector<QUuid> ids;
    for (const Frame &frame : frames) {
        const QUuid id = place(next, frame, error);
        if (id.isNull())
            return {};
        ids.push_back(id);
    }
    desk.beginInteraction(step);
    desk.previewDocument(next, ids);
    desk.commitInteraction();
    return ids;
}
}
