#include "UI/CloudBrowser.h"
#include "Cloud/CloudProviders.h"
#include "UI/CloudBadge.h"
#include <QApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const QString lastLocationKey = QStringLiteral("cloud/lastLocation");
const QString folderKey = QStringLiteral("cloud/folder/");
constexpr int localRow = 0;

QString actionText(CloudBrowser::Mode mode)
{
    switch (mode) {
    case CloudBrowser::Mode::open: return QStringLiteral("Open");
    case CloudBrowser::Mode::save: return QStringLiteral("Save");
    case CloudBrowser::Mode::place: return QStringLiteral("Place");
    case CloudBrowser::Mode::exportFile: return QStringLiteral("Export");
    }
    return {};
}

bool saving(CloudBrowser::Mode mode)
{
    return mode == CloudBrowser::Mode::save || mode == CloudBrowser::Mode::exportFile;
}
}

CloudBrowser::CloudBrowser(CloudStorage &storage, Mode mode, QStringList suffixes, const QString &name, QWidget *parent)
    : QDialog(parent), m_storage(storage), m_mode(mode), m_suffixes(std::move(suffixes)), m_locations(new QListWidget(this)),
      m_pages(new QStackedWidget(this)), m_breadcrumbs(new QWidget(this)), m_entries(new QListWidget(this)), m_status(new QLabel(this)),
      m_name(new QLineEdit(name, this)), m_choose(new QPushButton(actionText(mode), this))
{
    setObjectName(QStringLiteral("cloudBrowser"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(mode == Mode::save ? QStringLiteral("Save As") : actionText(mode));
    resize(720, 460);
    m_locations->setObjectName(QStringLiteral("locations"));
    m_locations->setFixedWidth(190);
    m_locations->setIconSize(QSize(18, 18));
    m_entries->setObjectName(QStringLiteral("entries"));
    m_entries->setSelectionMode(mode == Mode::open || mode == Mode::place ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    m_status->setObjectName(QStringLiteral("browserStatus"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_name->setObjectName(QStringLiteral("fileName"));
    m_choose->setObjectName(QStringLiteral("chooseButton"));
    m_choose->setDefault(true);

    // This Computer: one button to the usual dialog.
    auto *local = new QWidget(m_pages);
    auto *localColumn = new QVBoxLayout(local);
    localColumn->addStretch(1);
    auto *localNote = new QLabel(QStringLiteral("Files on this computer."), local);
    localNote->setAlignment(Qt::AlignCenter);
    localColumn->addWidget(localNote);
    auto *browse = new QPushButton(QStringLiteral("Browse…"), local);
    browse->setObjectName(QStringLiteral("browseLocal"));
    localColumn->addWidget(browse, 0, Qt::AlignCenter);
    localColumn->addStretch(1);
    m_pages->addWidget(local);

    auto *remote = new QWidget(m_pages);
    auto *remoteColumn = new QVBoxLayout(remote);
    remoteColumn->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_breadcrumbs->setObjectName(QStringLiteral("breadcrumbs"));
    m_crumbRow = new QHBoxLayout(m_breadcrumbs);
    m_crumbRow->setContentsMargins(0, 0, 0, 0);
    m_crumbRow->setSpacing(2);
    top->addWidget(m_breadcrumbs, 1);
    auto *refresh = new QToolButton(remote);
    refresh->setObjectName(QStringLiteral("refreshFolder"));
    refresh->setText(QStringLiteral("Refresh"));
    auto *newFolder = new QToolButton(remote);
    newFolder->setObjectName(QStringLiteral("newFolder"));
    newFolder->setText(QStringLiteral("New Folder"));
    top->addWidget(refresh);
    top->addWidget(newFolder);
    remoteColumn->addLayout(top);
    remoteColumn->addWidget(m_entries, 1);
    remoteColumn->addWidget(m_status);
    if (saving(mode)) {
        auto *nameRow = new QHBoxLayout;
        nameRow->addWidget(new QLabel(QStringLiteral("Name:"), remote));
        nameRow->addWidget(m_name, 1);
        remoteColumn->addLayout(nameRow);
    } else {
        m_name->hide();
    }
    m_pages->addWidget(remote);

    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("cancelButton"));
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(m_choose);
    auto *right = new QVBoxLayout;
    right->addWidget(m_pages, 1);
    right->addLayout(buttons);
    auto *row = new QHBoxLayout(this);
    row->addWidget(m_locations);
    row->addLayout(right, 1);

    connect(browse, &QPushButton::clicked, this, [this] {
        QSettings().setValue(lastLocationKey, QString());
        emit chooseLocal();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_choose, &QPushButton::clicked, this, &CloudBrowser::choose);
    connect(refresh, &QToolButton::clicked, this, [this] { navigate(folder()); });
    connect(newFolder, &QToolButton::clicked, this, &CloudBrowser::makeFolder);
    connect(m_entries, &QListWidget::itemActivated, this, &CloudBrowser::activate);
    connect(m_entries, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && saving(m_mode) && !item->data(Qt::UserRole + 1).toBool())
            m_name->setText(item->data(Qt::UserRole).toString());
    });
    connect(m_name, &QLineEdit::returnPressed, this, &CloudBrowser::choose);
    connect(m_locations, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row == localRow) {
            if (m_listing)
                m_listing->cancel();
            m_folder.reset();
            m_pages->setCurrentIndex(0);
            m_choose->setEnabled(false);
            return;
        }
        if (QListWidgetItem *item = m_locations->item(row))
            showRemote(item->data(Qt::UserRole).toString());
    });
    connect(&m_storage, &CloudStorage::remotesChanged, this, &CloudBrowser::fillLocations);
    fillLocations();
}

void CloudBrowser::fillLocations()
{
    const QString shown = remoteShown();
    const QSignalBlocker quiet(m_locations);
    m_locations->clear();
    m_locations->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_ComputerIcon), QStringLiteral("This Computer")));
    for (const CloudRemote &remote : m_storage.remotes()) {
        auto *item = new QListWidgetItem(CloudBadge::icon(remote.type), remote.name);
        item->setToolTip(CloudProviders::forType(remote.type).name);
        item->setData(Qt::UserRole, remote.name);
        m_locations->addItem(item);
    }
    // The last place used, remembered.
    const QString wanted = shown.isEmpty() ? QSettings().value(lastLocationKey).toString() : shown;
    for (int row = 1; row < m_locations->count(); ++row) {
        if (m_locations->item(row)->data(Qt::UserRole).toString() == wanted) {
            m_locations->setCurrentRow(row);
            if (shown != wanted)
                showRemote(wanted);
            return;
        }
    }
    m_locations->setCurrentRow(localRow);
    m_folder.reset();
    m_pages->setCurrentIndex(0);
    m_choose->setEnabled(false);
}

void CloudBrowser::showRemote(const QString &remote)
{
    for (int row = 1; row < m_locations->count(); ++row) {
        if (m_locations->item(row)->data(Qt::UserRole).toString() == remote) {
            const QSignalBlocker quiet(m_locations);
            m_locations->setCurrentRow(row);
        }
    }
    QSettings().setValue(lastLocationKey, remote);
    m_pages->setCurrentIndex(1);
    navigate(CloudLocation{remote, QSettings().value(folderKey + remote).toString()});
}

void CloudBrowser::navigate(const CloudLocation &target)
{
    if (m_listing)
        m_listing->cancel();
    m_folder = target;
    QSettings().setValue(folderKey + target.remote, target.path);
    fillBreadcrumbs();
    m_entries->clear();
    m_status->setText(QStringLiteral("Loading…"));
    m_choose->setEnabled(saving(m_mode));
    m_listing = m_storage.list(target, [this, target](const QList<CloudEntry> &entries, const QString &error) {
        m_listing = nullptr;
        if (!m_folder || !(*m_folder == target))
            return;
        if (error == QLatin1String("Cancelled."))
            return;
        if (!error.isEmpty()) {
            // A remembered folder that's gone: the top of the remote instead.
            if (!target.path.isEmpty() && error.contains(QLatin1String("not found"), Qt::CaseInsensitive)) {
                navigate(CloudLocation{target.remote, QString()});
                return;
            }
            m_status->setText(QStringLiteral("Couldn't list %1. %2").arg(m_storage.serviceName(target.remote), error));
            return;
        }
        fillEntries(entries);
    });
}

void CloudBrowser::fillBreadcrumbs()
{
    while (QLayoutItem *item = m_crumbRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (!m_folder)
        return;
    QStringList parts{m_folder->remote};
    parts << m_folder->segments();
    for (int index = 0; index < parts.size(); ++index) {
        if (index > 0)
            m_crumbRow->addWidget(new QLabel(QStringLiteral("›"), m_breadcrumbs));
        auto *crumb = new QToolButton(m_breadcrumbs);
        crumb->setAutoRaise(true);
        crumb->setText(parts[index]);
        crumb->setObjectName(QStringLiteral("crumb%1").arg(index));
        const CloudLocation target{m_folder->remote, m_folder->segments().mid(0, index).join(QLatin1Char('/'))};
        connect(crumb, &QToolButton::clicked, this, [this, target] { navigate(target); });
        m_crumbRow->addWidget(crumb);
    }
    m_crumbRow->addStretch(1);
}

bool CloudBrowser::accepts(const QString &name) const
{
    return m_suffixes.isEmpty() || m_suffixes.contains(QFileInfo(name).suffix().toLower());
}

void CloudBrowser::fillEntries(const QList<CloudEntry> &entries)
{
    m_entries->clear();
    int files = 0;
    for (const CloudEntry &entry : entries) {
        if (!entry.isDir && !accepts(entry.name))
            continue;
        auto *item = new QListWidgetItem(style()->standardIcon(entry.isDir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon), entry.name);
        item->setData(Qt::UserRole, entry.name);
        item->setData(Qt::UserRole + 1, entry.isDir);
        if (!entry.isDir) {
            item->setToolTip(QStringLiteral("%1 · %2").arg(QLocale().formattedDataSize(std::max<qint64>(0, entry.size)),
                                                           QLocale().toString(entry.modified.toLocalTime(), QLocale::ShortFormat)));
            files += 1;
        }
        m_entries->addItem(item);
    }
    m_status->setText(m_entries->count() == 0 ? QStringLiteral("Nothing here yet.") : QString());
    m_choose->setEnabled(saving(m_mode) || files > 0 || m_entries->count() > 0);
}

void CloudBrowser::activate(QListWidgetItem *item)
{
    if (!item || !m_folder)
        return;
    if (item->data(Qt::UserRole + 1).toBool()) {
        navigate(m_folder->child(item->data(Qt::UserRole).toString()));
        return;
    }
    if (saving(m_mode))
        m_name->setText(item->data(Qt::UserRole).toString());
    choose();
}

void CloudBrowser::choose()
{
    if (!m_folder)
        return;
    if (!saving(m_mode)) {
        QList<CloudLocation> files;
        for (QListWidgetItem *item : m_entries->selectedItems()) {
            if (!item->data(Qt::UserRole + 1).toBool())
                files << m_folder->child(item->data(Qt::UserRole).toString());
        }
        // A folder alone opens, as in any file dialog.
        if (files.isEmpty()) {
            if (QListWidgetItem *item = m_entries->currentItem(); item && item->data(Qt::UserRole + 1).toBool())
                navigate(m_folder->child(item->data(Qt::UserRole).toString()));
            return;
        }
        emit chosen(files);
        accept();
        return;
    }
    QString name = m_name->text().trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String("..") || name == QLatin1String(".")) {
        m_status->setText(QStringLiteral("Type a name for the file."));
        return;
    }
    if (!m_suffixes.isEmpty() && !accepts(name))
        name += QLatin1Char('.') + m_suffixes.constFirst();
    const CloudLocation file = m_folder->child(name);
    for (int row = 0; row < m_entries->count(); ++row) {
        QListWidgetItem *item = m_entries->item(row);
        if (item->data(Qt::UserRole).toString() == name) {
            if (item->data(Qt::UserRole + 1).toBool()) {
                navigate(file);
                return;
            }
            confirmReplace(file);
            return;
        }
    }
    emit chosenSave(file, CloudStamp{});
    accept();
}

void CloudBrowser::confirmReplace(const CloudLocation &file)
{
    auto *alert = new QMessageBox(this);
    alert->setObjectName(QStringLiteral("replaceAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(QStringLiteral("“%1” already exists. Replace it?").arg(file.fileName()));
    alert->setInformativeText(QStringLiteral("The file on %1 is replaced with this one.").arg(m_storage.serviceName(file.remote)));
    const QPushButton *replace = alert->addButton(QStringLiteral("Replace"), QMessageBox::DestructiveRole);
    alert->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    connect(alert, &QDialog::finished, this, [this, alert, replace, file] {
        if (alert->clickedButton() != replace)
            return;
        m_status->setText(QStringLiteral("Checking…"));
        // What's there now becomes the base, so a change after this still counts as a conflict.
        m_storage.stat(file, [this, browser = QPointer<CloudBrowser>(this), file](const CloudStamp &stamp, const QString &error) {
            if (!browser)
                return;
            if (!error.isEmpty()) {
                m_status->setText(QStringLiteral("Couldn't reach %1. %2").arg(m_storage.serviceName(file.remote), error));
                return;
            }
            emit chosenSave(file, stamp);
            accept();
        });
    });
    alert->open();
}

void CloudBrowser::makeFolder()
{
    if (!m_folder)
        return;
    auto *ask = new QInputDialog(this);
    ask->setObjectName(QStringLiteral("newFolderName"));
    ask->setAttribute(Qt::WA_DeleteOnClose);
    ask->setWindowTitle(QStringLiteral("New Folder"));
    ask->setLabelText(QStringLiteral("Name:"));
    ask->setTextValue(QStringLiteral("New Folder"));
    connect(ask, &QInputDialog::textValueSelected, this, [this](const QString &typed) {
        const QString name = typed.trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String("..") || !m_folder)
            return;
        const CloudLocation made = m_folder->child(name);
        m_status->setText(QStringLiteral("Making “%1”…").arg(name));
        m_storage.makeFolder(made, [this, browser = QPointer<CloudBrowser>(this), made](const QString &error) {
            if (!browser)
                return;
            if (!error.isEmpty()) {
                m_status->setText(QStringLiteral("Couldn't make the folder. %1").arg(error));
                return;
            }
            navigate(made);
        });
    });
    ask->open();
}
