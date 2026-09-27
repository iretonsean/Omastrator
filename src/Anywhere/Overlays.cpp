#include "Anywhere/Overlays.h"
#include "Agent/Capture.h"
#include "Agent/Island.h"
#include "Document/PathOperations.h"
#include "IO/ProjectStore.h"
#include "Rendering/VectorRenderer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>

namespace {
QString dataHome()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    return given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
}

QString toolTitle(const QString &tool)
{
    static const QHash<QString, QString> titles{{"pen", "Pen"},   {"rectangle", "Rectangle"}, {"ellipse", "Ellipse"}, {"line", "Line"},
                                                {"arrow", "Arrow"}, {"text", "Text"},          {"note", "Note"}};
    return titles.value(tool);
}

QRectF spanned(const std::vector<QPointF> &points)
{
    QRectF rect(points.front(), points.back());
    return rect.normalized();
}

// A surface's label from its key alone, for layers read back from the file.
QString labelFor(const QString &key)
{
    Surface surface;
    const QString kind = key.section(QLatin1Char(':'), 0, 0);
    const QString rest = key.section(QLatin1Char(':'), 1);
    surface.kind = kind == QLatin1String("web") ? Surface::Kind::web : kind == QLatin1String("window") ? Surface::Kind::window : Surface::Kind::desktop;
    surface.app = rest;
    surface.url = QUrl(rest);
    return surface.label();
}
}

OverlayStore::OverlayStore(QObject *parent) : QObject(parent)
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(400);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    connect(&m_session, &EditorSession::documentChanged, this, [this] {
        m_saveTimer->start();
        emit changed();
    });
}

QString OverlayStore::defaultPath()
{
    return QDir(dataHome()).filePath(QStringLiteral("omastrator/overlays.omai"));
}

QColor OverlayStore::ink()
{
    QFile file(QDir(Capture::themeDirectory()).filePath(QStringLiteral("colors.toml")));
    if (file.open(QIODevice::ReadOnly)) {
        for (const auto &[name, color] : Capture::themeColors(QString::fromUtf8(file.readAll()))) {
            if (name == QLatin1String("accent"))
                return color;
        }
    }
    return QColor(0xff, 0x6b, 0x3d);
}

QString OverlayStore::load(const QString &path)
{
    m_path = path;
    m_rendered.clear();
    if (QFileInfo::exists(path)) {
        try {
            m_session.loadDocument(ProjectStore::read(path));
            return {};
        } catch (const FileError &failure) {
            // A damaged file is kept aside, and the overlays start again.
            QFile::rename(path, path + QStringLiteral(".damaged"));
            m_session.createDocument(QSizeF(1920, 1080));
            return failure.message();
        }
    }
    m_session.createDocument(QSizeF(1920, 1080));
    return {};
}

QString OverlayStore::save()
{
    m_saveTimer->stop();
    // A proposal on show isn't the user's yet; it's saved once kept.
    if (m_path.isEmpty() || !m_session.hasDocument() || m_session.isInteracting())
        return {};
    try {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        ProjectStore::write(*m_session.document(), m_path);
        m_session.markSaved();
    } catch (const FileError &failure) {
        return failure.message();
    }
    return {};
}

std::optional<QUuid> OverlayStore::layer(const QString &key) const
{
    if (!m_session.hasDocument())
        return std::nullopt;
    const VectorDocument &document = *m_session.document();
    for (const QUuid &id : document.layers()) {
        if (document.find(id)->name == key)
            return id;
    }
    return std::nullopt;
}

QStringList OverlayStore::surfaces() const
{
    QStringList keys;
    if (!m_session.hasDocument())
        return keys;
    const VectorDocument &document = *m_session.document();
    for (const QUuid &id : document.layers()) {
        const QString name = document.find(id)->name;
        if (name.contains(QLatin1Char(':')) && !document.children(id).empty())
            keys << name;
    }
    return keys;
}

std::vector<QUuid> OverlayStore::art(const QString &key) const
{
    const auto found = layer(key);
    return found ? m_session.document()->children(*found) : std::vector<QUuid>{};
}

QUuid OverlayStore::ensureLayer(const Surface &surface)
{
    if (const auto found = layer(surface.key))
        return *found;
    if (!m_session.hasDocument())
        load(m_path.isEmpty() ? defaultPath() : m_path);
    // Inside a drawing's step it folds into that step; alone it's a step of its own.
    m_session.beginEdit(QStringLiteral("Add Overlay for %1").arg(surface.label()));
    const QUuid id = m_session.addLayer();
    m_session.rename(id, surface.key);
    m_session.endEdit();
    return id;
}

QUuid OverlayStore::draw(const Surface &surface, const Stroke &stroke, QString *error)
{
    auto failed = [&](const QString &message) {
        if (error)
            *error = message;
        return QUuid();
    };
    const QString title = toolTitle(stroke.tool);
    if (title.isEmpty())
        return failed(QStringLiteral("There is no overlay tool “%1”.").arg(stroke.tool));
    if (stroke.points.empty())
        return failed(QStringLiteral("Nothing was drawn."));
    if (surface.key.isEmpty())
        return failed(QStringLiteral("There's no surface under the drawing."));
    if (!m_session.hasDocument())
        load(m_path.isEmpty() ? defaultPath() : m_path);
    if (m_session.isInteracting())
        return failed(QStringLiteral("Keep or discard the proposal on the overlay first."));
    if ((stroke.tool == QLatin1String("text") || stroke.tool == QLatin1String("note")) && stroke.text.trimmed().isEmpty())
        return failed(QStringLiteral("Type something first."));

    // Screen to the surface's own coordinates.
    const QPointF origin = surface.origin();
    std::vector<QPointF> points;
    for (const QPointF &point : stroke.points)
        points.push_back(point - origin);
    const QColor accent = ink();
    StrokeStyle line;
    line.paint = Paint::solid(accent);
    line.width = 3;
    line.cap = Qt::RoundCap;
    line.join = Qt::RoundJoin;

    QUuid made;
    m_session.beginEdit(QStringLiteral("Draw %1 on %2").arg(title, surface.label()));
    const QUuid layerId = ensureLayer(surface);
    m_session.deselectAll();
    m_session.setActiveLayer(layerId);
    auto shape = [&](const VectorPath &path) {
        VectorObject object = m_session.pathObject(path, title);
        object.fill = Paint::none();
        object.stroke = line;
        return object;
    };
    const QRectF box = spanned(points);
    if (stroke.tool == QLatin1String("pen")) {
        made = m_session.addObject(shape(points.size() > 2 ? fitFreehand(points, 1.5) : Shapes::line(points.front(), points.back())), title);
    } else if (stroke.tool == QLatin1String("rectangle")) {
        made = m_session.addObject(shape(Shapes::rectangle(box.width() < 2 && box.height() < 2 ? QRectF(box.topLeft(), QSizeF(120, 80)) : box, 6)), title);
    } else if (stroke.tool == QLatin1String("ellipse")) {
        made = m_session.addObject(shape(Shapes::ellipse(box.width() < 2 && box.height() < 2 ? QRectF(box.topLeft(), QSizeF(100, 100)) : box)), title);
    } else if (stroke.tool == QLatin1String("line") || stroke.tool == QLatin1String("arrow")) {
        VectorObject object = shape(Shapes::line(points.front(), points.back()));
        if (stroke.tool == QLatin1String("arrow"))
            object.stroke.endArrow = Arrowhead::arrow;
        made = m_session.addObject(object, title);
    } else if (stroke.tool == QLatin1String("text")) {
        VectorObject object = m_session.textObject(points.front() + QPointF(0, 22), stroke.text.trimmed());
        object.text.size = 22;
        object.fill = Paint::solid(accent);
        object.stroke = StrokeStyle();
        object.stroke.width = 0;
        made = m_session.addObject(object, title);
    } else {
        // A sticky note: a pale card and its words, grouped.
        const QRectF card = box.width() < 40 || box.height() < 30 ? QRectF(box.topLeft(), QSizeF(200, 120)) : box;
        VectorObject paper = m_session.pathObject(Shapes::rectangle(card, 8), QStringLiteral("Note"));
        paper.fill = Paint::solid(QColor(0xff, 0xe8, 0x8c));
        paper.stroke = StrokeStyle();
        paper.stroke.paint = Paint::solid(QColor(0xd9, 0xb8, 0x3a));
        paper.stroke.width = 1;
        const QUuid paperId = m_session.addObject(paper, title);
        VectorObject words = m_session.textObject(card.topLeft() + QPointF(14, 30), stroke.text.trimmed());
        words.text.size = 16;
        words.text.area = QSizeF(card.width() - 28, card.height() - 24);
        words.transform = QTransform::fromTranslate(card.left() + 14, card.top() + 14);
        words.fill = Paint::solid(QColor(0x33, 0x2b, 0x14));
        words.stroke = StrokeStyle();
        words.stroke.width = 0;
        const QUuid wordsId = m_session.addObject(words, title);
        m_session.select({paperId, wordsId});
        m_session.groupSelection();
        made = m_session.selection().empty() ? wordsId : m_session.selection().front();
        m_session.rename(made, QStringLiteral("Note"));
    }
    m_session.endEdit();
    if (made.isNull())
        return failed(QStringLiteral("It couldn't be drawn."));
    return made;
}

QUuid OverlayStore::place(const Surface &surface, const VectorDocument &art, const QUuid &root, const QString &step, QString *error)
{
    if (!m_session.hasDocument())
        load(m_path.isEmpty() ? defaultPath() : m_path);
    if (m_session.isInteracting()) {
        if (error)
            *error = QStringLiteral("Keep or discard the preview on the overlay first.");
        return {};
    }
    std::vector<VectorObject> copies = art.copySubtree(root);
    if (copies.empty()) {
        if (error)
            *error = QStringLiteral("There's nothing to place.");
        return {};
    }
    VectorDocument next = *m_session.document();
    QUuid layerId;
    if (const auto found = layer(surface.key)) {
        layerId = *found;
    } else {
        VectorObject made;
        made.kind = ObjectKind::layer;
        made.name = surface.key;
        made.layerColor = nextLayerColor(int(next.layers().size()));
        layerId = made.id;
        next.objects.push_back(made);
    }
    const QUuid placed = copies.front().id;
    copies.front().parentID = layerId;
    for (VectorObject &copy : copies) {
        const QUuid parent = *copy.parentID;
        next.insert(std::move(copy), parent);
    }
    // The layer and the art arrive together: one step.
    m_session.beginInteraction(step);
    m_session.previewDocument(next, {placed});
    m_session.commitInteraction();
    m_session.setActiveLayer(layerId);
    return placed;
}

bool OverlayStore::selectArt(const QString &key, const std::vector<QUuid> &ids)
{
    const std::vector<QUuid> all = art(key);
    if (all.empty())
        return false;
    std::vector<QUuid> chosen;
    for (const QUuid &id : ids) {
        if (std::find(all.begin(), all.end(), id) != all.end())
            chosen.push_back(id);
    }
    m_session.select(chosen.empty() ? all : chosen);
    return true;
}

QString OverlayStore::selectedSurface() const
{
    if (!m_session.hasDocument() || m_session.selection().empty())
        return {};
    const auto layerId = m_session.document()->layerOf(m_session.selection().front());
    return layerId ? m_session.document()->find(*layerId)->name : QString();
}

void OverlayStore::clear(const QString &key)
{
    const std::vector<QUuid> all = art(key);
    if (all.empty() || m_session.isInteracting())
        return;
    m_session.beginEdit(QStringLiteral("Clear Overlay on %1").arg(labelFor(key)));
    m_session.deleteObjects(all);
    m_session.endEdit();
}

VectorDocument OverlayStore::extract(const QString &key, const std::vector<QUuid> &ids) const
{
    VectorDocument result = VectorDocument::blank(QSizeF(1, 1));
    result.background = Qt::transparent;
    const QUuid into = result.layers().front();
    result.find(into)->name = labelFor(key);
    const std::vector<QUuid> roots = ids.empty() ? art(key) : ids;
    if (!m_session.hasDocument())
        return result;
    const VectorDocument &source = *m_session.document();
    QRectF bounds;
    for (const QUuid &id : roots) {
        std::vector<VectorObject> copies = source.copySubtree(id);
        if (copies.empty())
            continue;
        copies.front().parentID = into;
        for (VectorObject &copy : copies)
            result.objects.push_back(std::move(copy));
        bounds = bounds.united(source.bounds(id, true));
    }
    result.size = QSizeF(std::max(1.0, bounds.right()), std::max(1.0, bounds.bottom()));
    return result;
}

std::optional<OverlayStore::Picture> OverlayStore::picture(const QString &key, double scale)
{
    const auto layerId = layer(key);
    if (!layerId || !m_session.hasDocument())
        return std::nullopt;
    const VectorDocument &document = *m_session.document();
    const std::vector<QUuid> roots = document.children(*layerId);
    if (roots.empty())
        return std::nullopt;
    std::vector<VectorObject> objects;
    for (const QUuid &id : document.descendants(*layerId))
        objects.push_back(*document.find(id));
    Rendered &cached = m_rendered[key];
    if (cached.scale == scale && cached.objects == objects && QFileInfo::exists(cached.picture.path))
        return cached.picture;

    // Room for arrowheads and round caps past the paths' own bounds.
    const QRectF bounds = document.bounds(roots, true).adjusted(-12, -12, 12, 12);
    VectorDocument alone = document;
    alone.objects.erase(std::remove_if(alone.objects.begin(), alone.objects.end(),
                                       [&](const VectorObject &object) {
                                           return object.id != *layerId && std::find_if(objects.begin(), objects.end(), [&](const VectorObject &kept) {
                                                                                   return kept.id == object.id;
                                                                               }) == objects.end();
                                       }),
                        alone.objects.end());
    QImage image((bounds.size() * scale).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        painter.translate(-bounds.topLeft());
        VectorRenderer::Options options;
        options.drawBackground = false;
        VectorRenderer::draw(painter, alone, options);
    }
    const QString folder = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("overlays"));
    QDir().mkpath(folder);
    const QString stem = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    // A new name each time, so the shell's image cache never shows the old one.
    const QString path = QDir(folder).filePath(QStringLiteral("%1-%2.png").arg(stem).arg(++m_version));
    if (!image.save(path, "PNG"))
        return std::nullopt;
    if (!cached.picture.path.isEmpty() && cached.picture.path != path)
        QFile::remove(cached.picture.path);
    cached.objects = objects;
    cached.scale = scale;
    cached.picture = Picture{path, bounds, m_version};
    return cached.picture;
}
