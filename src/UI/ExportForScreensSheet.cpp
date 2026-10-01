#include "UI/ExportForScreensSheet.h"
#include "IO/ScreenExport.h"
#include "UI/BrowserViews.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
const QString scalesKey = QStringLiteral("screenExport/scales");
const QString formatsKey = QStringLiteral("screenExport/formats");
const QString folderKey = QStringLiteral("screenExport/folder");

QListWidget *checklist(const QString &name, QWidget *parent)
{
    auto *list = new QListWidget(parent);
    list->setObjectName(name);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setMaximumHeight(110);
    return list;
}

QListWidgetItem *checkableItem(QListWidget *list, const QString &text, const QVariant &data, bool checked)
{
    auto *item = new QListWidgetItem(text, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, data);
    return item;
}
}

ExportForScreensSheet::ExportForScreensSheet(EditorSession &session, QWidget *parent)
    : QDialog(parent), m_session(session), m_artboards(checklist(QStringLiteral("screenExportArtboards"), this)),
      m_assets(checklist(QStringLiteral("screenExportAssets"), this)), m_folder(new QLineEdit(this)), m_progress(new QProgressBar(this)),
      m_status(new QLabel(this)), m_export(new QPushButton(QStringLiteral("Export"), this)), m_openFolder(new QPushButton(QStringLiteral("Open Folder"), this))
{
    setObjectName(QStringLiteral("exportForScreensDialog"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(QStringLiteral("Export for Screens"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    if (const std::optional<VectorDocument> &document = m_session.document()) {
        // Every page's artboards, under a heading per page once there are two or more.
        const std::vector<Page> pages = document->allPages();
        for (const Page &page : pages) {
            std::vector<std::pair<QString, QUuid>> exported;
            for (const Artboard &board : document->artboardsOn(page.id)) {
                if (board.exported)
                    exported.emplace_back(board.name, board.id);
            }
            if (document->artboardsOn(page.id).empty()) {
                // No artboard on this page: its top-level frames, else its content as one picture (listed by the page's id).
                VectorDocument shown = *document;
                shown.currentPage = page.id;
                for (const QUuid &frame : shown.topLevelFrames())
                    exported.emplace_back(shown.find(frame)->name, frame);
                if (exported.empty() && !shown.topLevelArt().empty())
                    exported.emplace_back(QStringLiteral("%1 (content)").arg(page.name), page.id);
            }
            if (pages.size() > 1 && !exported.empty()) {
                auto *heading = new QListWidgetItem(page.name, m_artboards);
                heading->setFlags(Qt::ItemIsEnabled);
                QFont bold = heading->font();
                bold.setBold(true);
                heading->setFont(bold);
            }
            for (const auto &[name, id] : exported)
                checkableItem(m_artboards, name, id.toString(QUuid::WithoutBraces), true);
        }
    }
    layout->addWidget(new QLabel(QStringLiteral("Artboards"), this));
    layout->addWidget(m_artboards);

    const std::vector<QUuid> assets = m_session.document() ? m_session.document()->exportAssets : std::vector<QUuid>();
    for (const QUuid &id : assets) {
        const VectorObject *object = m_session.document()->find(id);
        checkableItem(m_assets, object ? object->name : id.toString(QUuid::WithoutBraces), id.toString(QUuid::WithoutBraces), true);
    }
    auto *assetsLabel = new QLabel(QStringLiteral("Export Assets (Object ▸ Collect for Export)"), this);
    layout->addWidget(assetsLabel);
    layout->addWidget(m_assets);
    assetsLabel->setVisible(!assets.empty());
    m_assets->setVisible(!assets.empty());

    auto *scaleRow = new QHBoxLayout;
    scaleRow->addWidget(new QLabel(QStringLiteral("Scales:"), this));
    for (double scale : {0.5, 1.0, 2.0, 3.0, 4.0}) {
        auto *box = new QCheckBox(scale == std::floor(scale) ? QStringLiteral("%1×").arg(scale) : QStringLiteral("%1×").arg(scale, 0, 'g', 2), this);
        box->setObjectName(QStringLiteral("screenScale%1x").arg(scale, 0, 'g', 2));
        box->setChecked(scale == 1.0);
        scaleRow->addWidget(box);
        m_scales.push_back({scale, box});
    }
    scaleRow->addStretch(1);
    layout->addLayout(scaleRow);

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel(QStringLiteral("Formats:"), this));
    for (const QString &format : {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("svg"), QStringLiteral("pdf"), QStringLiteral("webp")}) {
        auto *box = new QCheckBox(format.toUpper(), this);
        box->setObjectName(QStringLiteral("screenFormat") + format);
        box->setChecked(format == QLatin1String("png"));
        formatRow->addWidget(box);
        m_formats.push_back({format, box});
    }
    formatRow->addStretch(1);
    layout->addLayout(formatRow);

    auto *folderRow = new QHBoxLayout;
    m_folder->setObjectName(QStringLiteral("screenExportFolder"));
    folderRow->addWidget(new QLabel(QStringLiteral("Folder:"), this));
    folderRow->addWidget(m_folder, 1);
    auto *browseButton = new QPushButton(QStringLiteral("Browse…"), this);
    browseButton->setObjectName(QStringLiteral("screenExportBrowse"));
    connect(browseButton, &QPushButton::clicked, this, &ExportForScreensSheet::browse);
    folderRow->addWidget(browseButton);
    layout->addLayout(folderRow);

    restoreSettings();

    m_progress->setObjectName(QStringLiteral("screenExportProgress"));
    m_progress->setVisible(false);
    layout->addWidget(m_progress);
    m_status->setObjectName(QStringLiteral("screenExportStatus"));
    m_status->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    m_openFolder->setObjectName(QStringLiteral("screenExportOpenFolder"));
    m_openFolder->setEnabled(false);
    connect(m_openFolder, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder->text())); });
    buttons->addWidget(m_openFolder);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("screenExportCancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    m_export->setObjectName(QStringLiteral("screenExportRun"));
    m_export->setDefault(true);
    connect(m_export, &QPushButton::clicked, this, &ExportForScreensSheet::runExport);
    buttons->addWidget(m_export);
    layout->addLayout(buttons);
}

void ExportForScreensSheet::browse()
{
    const QString chosen = QFileDialog::getExistingDirectory(this, QStringLiteral("Export for Screens"), m_folder->text());
    if (!chosen.isEmpty())
        m_folder->setText(chosen);
}

std::vector<QUuid> ExportForScreensSheet::checkedArtboards() const
{
    std::vector<QUuid> result;
    for (int row = 0; row < m_artboards->count(); ++row) {
        if (m_artboards->item(row)->checkState() == Qt::Checked)
            result.push_back(QUuid::fromString(m_artboards->item(row)->data(Qt::UserRole).toString()));
    }
    return result;
}

std::vector<QUuid> ExportForScreensSheet::checkedAssets() const
{
    std::vector<QUuid> result;
    for (int row = 0; row < m_assets->count(); ++row) {
        if (m_assets->item(row)->checkState() == Qt::Checked)
            result.push_back(QUuid::fromString(m_assets->item(row)->data(Qt::UserRole).toString()));
    }
    return result;
}

std::vector<double> ExportForScreensSheet::checkedScales() const
{
    std::vector<double> result;
    for (const auto &[scale, box] : m_scales) {
        if (box->isChecked())
            result.push_back(scale);
    }
    return result;
}

QStringList ExportForScreensSheet::checkedFormats() const
{
    QStringList result;
    for (const auto &[format, box] : m_formats) {
        if (box->isChecked())
            result << format;
    }
    return result;
}

void ExportForScreensSheet::restoreSettings()
{
    const QSettings settings;
    const QVariantList scales = settings.value(scalesKey).toList();
    if (!scales.isEmpty()) {
        for (auto &[scale, box] : m_scales)
            box->setChecked(std::any_of(scales.begin(), scales.end(), [scale](const QVariant &v) { return std::abs(v.toDouble() - scale) < 1e-9; }));
    }
    const QStringList formats = settings.value(formatsKey).toStringList();
    if (!formats.isEmpty()) {
        for (auto &[format, box] : m_formats)
            box->setChecked(formats.contains(format));
    }
    const QString folder = settings.value(folderKey).toString();
    m_folder->setText(!folder.isEmpty() ? folder : QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
}

void ExportForScreensSheet::saveSettings() const
{
    QSettings settings;
    QVariantList scales;
    for (double scale : checkedScales())
        scales << scale;
    settings.setValue(scalesKey, scales);
    settings.setValue(formatsKey, checkedFormats());
    settings.setValue(folderKey, m_folder->text());
}

void ExportForScreensSheet::runExport()
{
    if (!m_session.document() || m_folder->text().trimmed().isEmpty())
        return;
    const std::vector<QUuid> artboards = checkedArtboards();
    const std::vector<QUuid> assets = checkedAssets();
    const std::vector<double> scales = checkedScales();
    const QStringList formats = checkedFormats();
    if (m_artboards->count() == 0 && assets.empty()) {
        m_status->setText(QStringLiteral("Nothing to export. The document has no artboard and no objects."));
        return;
    }
    if ((artboards.empty() && assets.empty()) || scales.empty() || formats.isEmpty()) {
        m_status->setText(QStringLiteral("Choose at least one artboard or asset, one scale and one format."));
        return;
    }
    saveSettings();
    BrowserViews::of(m_session)->flushPictures();
    ScreenExport::Settings settings;
    settings.scales = scales;
    settings.formats = formats;
    settings.folder = m_folder->text();
    m_export->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setValue(0);
    QStringList skipped;
    const QStringList written = ScreenExport::run(m_session.designDocument(), artboards, assets, settings, [this](const ScreenExport::Progress &progress) {
        m_progress->setMaximum(std::max(1, progress.total));
        m_progress->setValue(progress.done);
        m_status->setText(QStringLiteral("Writing %1…").arg(progress.name));
        QCoreApplication::processEvents();
    }, &skipped);
    m_export->setEnabled(true);
    m_status->setText(QStringLiteral("Wrote %1 file(s) to %2").arg(written.size()).arg(m_folder->text())
                      + (skipped.isEmpty() ? QString() : QStringLiteral(". Skipped: ") + skipped.join(QLatin1Char(' '))));
    m_openFolder->setEnabled(!written.isEmpty());
}
