#include "IO/ExcalidrawImporter.h"
#include "IO/ImageImporter.h"
#include "IO/ProjectStore.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include "Logging.h"
#include "UI/ExportSheet.h"
#include "UI/ProjectWorkspace.h"
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
bool samePlace(const std::optional<QString> &path, const QString &other)
{
    const auto resolved = [](const QString &place) {
        const QString canonical = QFileInfo(place).canonicalFilePath();
        return canonical.isEmpty() ? QFileInfo(place).absoluteFilePath() : canonical;
    };
    return path && resolved(*path) == resolved(other);
}

bool hasSuffix(const QString &path, const QString &suffix)
{
    return QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) == 0;
}

bool isExcalidraw(const QString &path)
{
    return hasSuffix(path, QStringLiteral("excalidraw"));
}

// A raster image opens on an artboard its size.
VectorDocument imageDocument(const QImage &image, const QString &name)
{
    VectorDocument document = VectorDocument::blank(QSizeF(image.size()));
    VectorObject placed;
    placed.kind = ObjectKind::image;
    placed.name = name;
    placed.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    document.insert(placed, document.layers().back());
    return document;
}
}

QStringList ProjectWorkspace::openFilters()
{
    QStringList filters{QStringLiteral("Omastrator documents (*.%1)").arg(QLatin1String(ProjectStore::extension))};
    filters << ImageImporter::nameFilters();
    // One filter with every pattern comes first.
    QStringList patterns;
    static const QRegularExpression inside(QStringLiteral("\\(([^)]*)\\)"));
    for (const QString &filter : filters) {
        const QRegularExpressionMatch match = inside.match(filter);
        for (const QString &pattern : match.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (!patterns.contains(pattern))
                patterns << pattern;
        }
    }
    filters.prepend(QStringLiteral("All supported files (%1)").arg(patterns.join(QLatin1Char(' '))));
    return filters;
}

// What an SVG couldn't bring along, said once after it opens.
void ProjectWorkspace::reportLeftOut(const QString &path, const QStringList &warnings)
{
    if (warnings.isEmpty())
        return;
    showError(QStringLiteral("Some of “%1” was left out").arg(QFileInfo(path).fileName()), warnings.join(QLatin1Char('\n')));
}

bool ProjectWorkspace::openFile(const QString &path)
{
    // "work:Designs/logo.omai", from Open Recent or the command line, when no local file has that name.
    if (!path.startsWith(QLatin1Char('/')) && !QFileInfo::exists(path)) {
        if (const std::optional<CloudLocation> cloud = CloudLocation::parse(path)) {
            openCloud(*cloud);
            return true;
        }
    }
    for (const std::shared_ptr<ProjectTab> &existing : m_tabs) {
        if (samePlace(existing->path, path)) {
            m_selectedID = existing->id;
            emit changed();
            return true;
        }
    }
    const bool native = hasSuffix(path, QLatin1String(ProjectStore::extension));
    VectorDocument document;
    QStringList warnings;
    try {
        if (native)
            document = ProjectStore::read(path);
        else if (ImageImporter::isVector(path))
            document = SvgImporter::read(path, &warnings);
        else if (isExcalidraw(path))
            document = ExcalidrawImporter::read(path, &warnings);
        else
            document = imageDocument(ImageImporter::read(path, &warnings), QFileInfo(path).fileName());
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t open “%1”").arg(QFileInfo(path).fileName()), error.message());
        return false;
    }
    // Loaded apart: a failed open leaves no broken tab.
    const auto opened = std::make_shared<ProjectTab>(ProjectTab::nameWithoutSuffix(path));
    opened->session.loadDocument(std::move(document));
    if (native)
        opened->path = QFileInfo(path).absoluteFilePath();
    adopt(opened);
    noteRecent(path);
    qCInfo(lcIO).noquote() << "opened" << path;
    reportLeftOut(path, warnings);
    // Flagged in passing, not in an alert: the text shows in a stand-in until replaced.
    if (const QStringList missing = opened->session.missingFonts(); !missing.isEmpty())
        setNotice(QStringLiteral("Missing %1: %2. Type ▸ Find/Replace Font… replaces %3.")
                      .arg(missing.size() == 1 ? QStringLiteral("font") : QStringLiteral("fonts"), missing.join(QStringLiteral(", ")),
                           missing.size() == 1 ? QStringLiteral("it") : QStringLiteral("them")));
    return true;
}

bool ProjectWorkspace::saveTo(ProjectTab &tab, const QString &path)
{
    if (!tab.session.hasDocument())
        return false;
    try {
        ProjectStore::write(*tab.session.document(), path);
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t save “%1”").arg(QFileInfo(path).fileName()), error.message());
        return false;
    }
    tab.path = QFileInfo(path).absoluteFilePath();
    tab.session.markSaved();
    if (!syncCloud(tab))
        noteRecent(path);
    emit changed();
    return true;
}

// SVG arrives as one group; pictures as image objects.
bool ProjectWorkspace::placeFile(const QString &path)
{
    EditorSession &session = current().session;
    if (!session.hasDocument())
        return false;
    const QString name = QFileInfo(path).fileName();
    const bool asDocument = ImageImporter::isVector(path) || isExcalidraw(path);
    try {
        if (!asDocument) {
            QStringList warnings;
            session.placeImage(ImageImporter::read(path, &warnings), name);
            reportLeftOut(path, warnings);
            return true;
        }
        QStringList warnings;
        const VectorDocument imported = isExcalidraw(path) ? ExcalidrawImporter::read(path, &warnings) : SvgImporter::read(path, &warnings);
        reportLeftOut(path, warnings);
        session.beginEdit(QStringLiteral("Place"));
        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = name;
        const QUuid groupID = session.addObject(group, QStringLiteral("Place"));
        // Parents come first; each object tops its own parent.
        for (const VectorObject &object : imported.objects) {
            if (object.kind == ObjectKind::layer)
                continue;
            const QUuid parent = imported.find(object.parentID.value())->kind == ObjectKind::layer ? groupID : object.parentID.value();
            const QUuid id = session.addObject(object, QStringLiteral("Place"));
            session.moveObject(id, parent, -1);
        }
        session.select({groupID});
        const QPointF middle(session.document()->size.width() / 2, session.document()->size.height() / 2);
        const QPointF shift = middle - session.selectionBounds().center();
        session.transformSelection(QTransform::fromTranslate(shift.x(), shift.y()), QStringLiteral("Place"));
        session.endEdit();
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t place “%1”").arg(name), error.message());
        return false;
    }
    return true;
}

bool ProjectWorkspace::exportTo(const QString &path, DocumentExporter::Format format, const RasterOptions &options)
{
    EditorSession &session = current().session;
    const std::optional<VectorDocument> &document = session.document();
    if (!document)
        return false;
    // Several artboards: the active one, not always the first.
    const VectorDocument page = document->artboards.empty() ? *document : document->artboardDocument(session.activeArtboard());
    try {
        switch (format) {
        case DocumentExporter::Format::pdf: DocumentExporter::writePdf(page, path); break;
        case DocumentExporter::Format::png: DocumentExporter::writePng(page, path, options.scale, options.transparent); break;
        case DocumentExporter::Format::jpeg: DocumentExporter::writeJpeg(page, path, options.scale, options.quality); break;
        case DocumentExporter::Format::svg: SvgExporter::write(page, path); break;
        }
    } catch (const FileError &error) {
        showError(QStringLiteral("Couldn’t export “%1”").arg(QFileInfo(path).fileName()), error.message());
        return false;
    }
    qCInfo(lcIO).noquote() << "exported" << path;
    return true;
}

void ProjectWorkspace::open()
{
    const auto chosen = [this](const QList<CloudLocation> &files) {
        for (const CloudLocation &file : files)
            openCloud(file);
    };
    if (!offerCloud(CloudBrowser::Mode::open, suffixesOf(openFilters()), QString(), [this] { openHere(); }, chosen, {}))
        openHere();
}

void ProjectWorkspace::openHere()
{
    auto *panel = new QFileDialog(window, QStringLiteral("Open"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setFileMode(QFileDialog::ExistingFiles);
    panel->setNameFilters(openFilters());
    connect(panel, &QDialog::finished, this, [this, panel](int result) {
        if (result == QDialog::Accepted)
            receive(panel->selectedFiles());
    });
    panel->open();
}

void ProjectWorkspace::save(QUuid id, bool asNew, std::function<void(bool)> done)
{
    const std::shared_ptr<ProjectTab> saving = tab(id);
    if (!saving || !saving->session.hasDocument()) {
        finish(done, false);
        return;
    }
    if (!asNew && saving->path) {
        finish(done, saveTo(*saving, *saving->path));
        return;
    }
    const QString suffix = QLatin1String(ProjectStore::extension);
    const auto chosenSave = [this, saving, done](const CloudLocation &file, const CloudStamp &existing) {
        finish(done, saveToCloud(*saving, file, existing));
    };
    if (!offerCloud(CloudBrowser::Mode::save, {suffix}, saving->title() + QLatin1Char('.') + suffix, [this, saving, asNew, done] { saveHere(saving, asNew, done); },
                    {}, chosenSave, [this, done] { finish(done, false); }))
        saveHere(saving, asNew, done);
}

void ProjectWorkspace::saveHere(const std::shared_ptr<ProjectTab> &saving, bool asNew, std::function<void(bool)> done)
{
    const QString suffix = QLatin1String(ProjectStore::extension);
    auto *panel = new QFileDialog(window, asNew ? QStringLiteral("Save As") : QStringLiteral("Save"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptSave);
    panel->setNameFilter(QStringLiteral("Omastrator document (*.%1)").arg(suffix));
    panel->setDefaultSuffix(suffix);
    panel->selectFile(saving->title() + QLatin1Char('.') + suffix);
    connect(panel, &QDialog::finished, this, [this, panel, saving, suffix, done](int result) {
        QString chosen = panel->selectedFiles().value(0);
        if (result != QDialog::Accepted || chosen.isEmpty()) {
            finish(done, false);
            return;
        }
        if (!hasSuffix(chosen, suffix))
            chosen += QLatin1Char('.') + suffix;
        finish(done, saveTo(*saving, chosen));
    });
    panel->open();
}

void ProjectWorkspace::place()
{
    if (!current().session.hasDocument())
        return;
    // Each file comes down to the cache first, then places as a local one would.
    const auto chosen = [this](const QList<CloudLocation> &files) {
        for (const CloudLocation &file : files) {
            QString local;
            try {
                local = CloudCache::localPath(file);
            } catch (const FileError &error) {
                showError(QStringLiteral("Couldn’t place “%1”").arg(file.fileName()), error.message());
                continue;
            }
            m_cloud->download(file, local, [this, file, local](const QString &error) {
                if (!error.isEmpty())
                    showError(QStringLiteral("Couldn’t place “%1” from %2").arg(file.fileName(), m_cloud->serviceName(file.remote)), error);
                else
                    placeFile(local);
            });
        }
    };
    if (!offerCloud(CloudBrowser::Mode::place, suffixesOf(ImageImporter::nameFilters()), QString(), [this] { placeHere(); }, chosen, {}))
        placeHere();
}

void ProjectWorkspace::placeHere()
{
    auto *panel = new QFileDialog(window, QStringLiteral("Place"));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setFileMode(QFileDialog::ExistingFiles);
    panel->setNameFilters(ImageImporter::nameFilters());
    connect(panel, &QDialog::finished, this, [this, panel](int result) {
        if (result != QDialog::Accepted)
            return;
        for (const QString &path : panel->selectedFiles())
            placeFile(path);
    });
    panel->open();
}

QString ProjectWorkspace::suggestedName(const QString &suffix) const
{
    const EditorSession &session = current().session;
    if (const std::optional<VectorDocument> &document = session.document(); document && document->artboardCount() > 1)
        return document->artboard(session.activeArtboard()).name + QLatin1Char('.') + suffix;
    return current().title() + QLatin1Char('.') + suffix;
}

void ProjectWorkspace::exportAs(DocumentExporter::Format format)
{
    if (!current().session.hasDocument())
        return;
    const auto [filter, suffix] = [format]() -> std::pair<QString, QString> {
        switch (format) {
        case DocumentExporter::Format::pdf: return {QStringLiteral("PDF document (*.pdf)"), QStringLiteral("pdf")};
        case DocumentExporter::Format::png: return {QStringLiteral("PNG image (*.png)"), QStringLiteral("png")};
        case DocumentExporter::Format::jpeg: return {QStringLiteral("JPEG image (*.jpg *.jpeg)"), QStringLiteral("jpg")};
        case DocumentExporter::Format::svg: return {QStringLiteral("SVG image (*.svg)"), QStringLiteral("svg")};
        }
        return {};
    }();
    const auto choosePath = [this, format, filter, suffix](RasterOptions options) {
        // To a remote: written to the cache, then uploaded in the background.
        const auto chosenSave = [this, format, options](const CloudLocation &file, const CloudStamp &) {
            QString local;
            try {
                local = CloudCache::localPath(file);
            } catch (const FileError &error) {
                showError(QStringLiteral("Couldn’t export “%1”").arg(file.fileName()), error.message());
                return;
            }
            QDir().mkpath(QFileInfo(local).absolutePath());
            if (!exportTo(local, format, options))
                return;
            setNotice(QStringLiteral("Exporting to %1…").arg(m_cloud->serviceName(file.remote)));
            m_uploader->upload(QStringLiteral("export:") + file.toString(), local, file, std::nullopt);
        };
        const auto here = [this, filter, suffix, format, options] {
            auto *panel = new QFileDialog(window, QStringLiteral("Export"));
            panel->setAttribute(Qt::WA_DeleteOnClose);
            panel->setAcceptMode(QFileDialog::AcceptSave);
            panel->setNameFilter(filter);
            panel->setDefaultSuffix(suffix);
            panel->selectFile(suggestedName(suffix));
            connect(panel, &QDialog::finished, this, [this, panel, format, options](int result) {
                if (result == QDialog::Accepted && !panel->selectedFiles().isEmpty())
                    exportTo(panel->selectedFiles().constFirst(), format, options);
            });
            panel->open();
        };
        if (!offerCloud(CloudBrowser::Mode::exportFile, {suffix}, suggestedName(suffix), here, {}, chosenSave))
            here();
    };
    if (format == DocumentExporter::Format::pdf || format == DocumentExporter::Format::svg) {
        choosePath({});
        return;
    }
    // Pictures ask their size and quality first, window-modal.
    auto *dialog = new QDialog(window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(format == DocumentExporter::Format::png ? QStringLiteral("Export PNG") : QStringLiteral("Export JPEG"));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    const VectorDocument &shown = *current().session.document();
    const VectorDocument page = shown.artboards.empty() ? shown : shown.artboardDocument(current().session.activeArtboard());
    layout->addWidget(new ExportSheet(page, format, [dialog, choosePath](std::optional<RasterOptions> chosen) {
        dialog->done(chosen ? QDialog::Accepted : QDialog::Rejected);
        if (chosen)
            choosePath(*chosen);
    }, dialog));
    dialog->open();
}
