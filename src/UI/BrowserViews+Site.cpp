#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/EditSets.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>

// The bar's menu for a site that isn't yours (docs/LIVE-IN-FRAME.md, section 2): its edits are kept as named sets on
// this machine, and This Is My Site… turns the frame into one of the user's own.

namespace {
BrowserViews::FolderChooser &chooser()
{
    static BrowserViews::FolderChooser responder;
    return responder;
}

QString chooseFolder(QWidget *parent, const QUrl &url)
{
    if (chooser())
        return chooser()(url);
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("This Is My Site"));
    auto *form = new QFormLayout(&dialog);
    auto *label = new QLabel(QStringLiteral("Where is the code for %1?").arg(url.host()), &dialog);
    label->setWordWrap(true);
    form->addRow(label);
    auto *folder = new QComboBox(&dialog);
    folder->setObjectName(QStringLiteral("siteFolder"));
    for (const auto &suggestion : ProjectRegistry::suggest(url)) {
        folder->addItem(suggestion.folder, suggestion.folder);
        folder->setItemData(folder->count() - 1, suggestion.reason, Qt::ToolTipRole);
    }
    auto *choose = new QPushButton(QStringLiteral("Choose a Folder…"), &dialog);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&dialog, folder] {
        const QString picked = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("The site's code"), QDir::homePath());
        if (picked.isEmpty())
            return;
        folder->insertItem(0, picked, picked);
        folder->setCurrentIndex(0);
    });
    form->addRow(QStringLiteral("Its code:"), folder);
    form->addRow(QString(), choose);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Use This Folder"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    return dialog.exec() == QDialog::Accepted ? folder->currentData().toString() : QString();
}
}

void BrowserViews::setFolderChooser(FolderChooser chooser)
{
    ::chooser() = std::move(chooser);
}

void BrowserViews::extendBarMenu(const QUuid &frame, QMenu *menu)
{
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser || object->browser->url.isEmpty() || owned(frame))
        return;
    menu->addSeparator();
    addSiteActions(frame, menu);
}

void BrowserViews::addSiteActions(const QUuid &frame, QMenu *menu)
{
    LiveFrames *live = LiveFrames::of(m_session);
    const bool running = live->active(frame) && live->snapshot(frame).state == LiveSession::State::running;
    const LiveFrames::Snapshot snapshot = live->snapshot(frame);
    const QUrl page = snapshot.url.isEmpty() ? m_session.document()->find(frame)->browser->url : snapshot.url;
    const QString origin = EditSets::originOf(page);
    const auto report = [this](const QString &error) {
        if (!error.isEmpty())
            emit notice(error);
    };

    QAction *keep = menu->addAction(QStringLiteral("Keep Edits…"));
    keep->setEnabled(running && !snapshot.edits.empty());
    connect(keep, &QAction::triggered, menu, [this, live, frame, origin, report] {
        bool ok = false;
        const QString name = QInputDialog::getText(nullptr, QStringLiteral("Keep Edits"), QStringLiteral("Name for this set of edits:"), QLineEdit::Normal,
                                                   EditSets::suggestedName(origin), &ok);
        if (ok)
            live->run(frame, [name](LiveSession &session) { return session.keepEdits(name); }, report);
    });

    QMenu *sets = menu->addMenu(QStringLiteral("Edit Sets"));
    const std::vector<EditSets::Set> kept = EditSets::read(origin);
    sets->setEnabled(running && !kept.empty());
    for (const EditSets::Set &set : kept) {
        QAction *toggle = sets->addAction(set.name);
        toggle->setCheckable(true);
        toggle->setChecked(set.enabled);
        connect(toggle, &QAction::toggled, menu, [live, frame, name = set.name, report](bool on) {
            live->run(frame, [name, on](LiveSession &session) { return session.setEditSetEnabled(name, on); }, report);
        });
    }

    QAction *original = menu->addAction(QStringLiteral("Show Original"));
    original->setCheckable(true);
    original->setChecked(snapshot.original);
    original->setEnabled(running);
    connect(original, &QAction::triggered, menu, [live, frame, report](bool on) {
        live->run(frame, [on](LiveSession &session) { return session.showOriginal(on); }, report);
    });

    QAction *exportCss = menu->addAction(QStringLiteral("Export CSS…"));
    exportCss->setEnabled(running);
    connect(exportCss, &QAction::triggered, menu, [this, live, frame, page, report] {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString host = page.host().isEmpty() ? QStringLiteral("page") : page.host();
        const QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Export CSS"),
                                                          QDir(folder.isEmpty() ? QDir::homePath() : folder).filePath(host + QStringLiteral(".user.css")),
                                                          QStringLiteral("Userstyle (*.user.css);;CSS (*.css)"));
        if (path.isEmpty())
            return;
        live->run(frame, [path, host](LiveSession &session) {
            const std::vector<EditSets::Edit> edits = session.editsShown();
            if (edits.empty())
                return QStringLiteral("There are no edits on this page to export.");
            QSaveFile file(path);
            const QByteArray css = EditSets::css(session.origin(), QStringLiteral("Edits on %1").arg(host), edits, path.endsWith(QLatin1String(".user.css"))).toUtf8();
            if (!file.open(QIODevice::WriteOnly) || file.write(css) != css.size() || !file.commit())
                return QStringLiteral("Couldn't write %1.").arg(path);
            return QString();
        }, report);
    });

    menu->addSeparator();
    QAction *mine = menu->addAction(QStringLiteral("This Is My Site…"));
    connect(mine, &QAction::triggered, menu, [this, frame] { chooseMySite(frame); });
}

void BrowserViews::chooseMySite(const QUuid &frame)
{
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser || object->browser->url.isEmpty())
        return;
    const QUrl url = object->browser->url;
    const QString folder = chooseFolder(m_canvas ? m_canvas->window() : nullptr, url);
    if (folder.isEmpty())
        return;
    if (!QFileInfo(folder).isDir()) {
        emit notice(QStringLiteral("%1 isn't a folder.").arg(folder));
        return;
    }
    if (const QString failure = ProjectRegistry::remember(url, folder); !failure.isEmpty()) {
        emit notice(failure);
        return;
    }
    // The tag reads the registry again; Live picks the project up on the page's next load.
    if (const auto found = m_entries.find(frame); found != m_entries.end())
        found->ownedAt = -100'000;
    emit notice(QStringLiteral("%1 is your site now. From the page's next load it runs from %2.").arg(url.host(), QDir::toNativeSeparators(folder)));
    emit frameChanged(frame);
}
