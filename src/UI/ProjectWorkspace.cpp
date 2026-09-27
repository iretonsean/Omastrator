#include "UI/ProjectWorkspace.h"
#include "Logging.h"
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

namespace {
const QString recentKey = QStringLiteral("recentFiles");
constexpr int recentLimit = 10;
}

ProjectTab::ProjectTab(QString name) : defaultName(std::move(name)) {}

QString ProjectTab::nameWithoutSuffix(const QString &path)
{
    // `.omai` alone is all name.
    const QString name = QFileInfo(path).fileName();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 && dot < name.size() - 1 ? name.left(dot) : name;
}

QString ProjectTab::title() const
{
    return path ? nameWithoutSuffix(*path) : defaultName;
}

ProjectWorkspace::ProjectWorkspace()
{
    const auto first = std::make_shared<ProjectTab>(QStringLiteral("Untitled"));
    m_tabs = {first};
    m_selectedID = first->id;
}

std::shared_ptr<ProjectTab> ProjectWorkspace::tab(QUuid id) const
{
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (each->id == id)
            return each;
    }
    return nullptr;
}

ProjectTab &ProjectWorkspace::current() const
{
    const std::shared_ptr<ProjectTab> selected = tab(m_selectedID);
    return selected ? *selected : *m_tabs.front();
}

void ProjectWorkspace::setManaging(bool managing)
{
    m_isManaging = managing;
    emit changed();
}

void ProjectWorkspace::finish(const std::function<void()> &done)
{
    // The caller goes on first.
    if (done)
        QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
}

void ProjectWorkspace::finish(const std::function<void(bool)> &done, bool value)
{
    if (done)
        QMetaObject::invokeMethod(this, [done, value] { done(value); }, Qt::QueuedConnection);
}

ProjectTab &ProjectWorkspace::addTab(bool reuseEmpty, const QString &name)
{
    if (reuseEmpty && m_tabs.size() == 1 && !current().session.hasDocument() && name.isEmpty())
        return current();
    const QString title = name.isEmpty() ? QStringLiteral("Untitled %1").arg(m_nextNumber++) : name;
    adopt(std::make_shared<ProjectTab>(title));
    return current();
}

// A lone empty tab gives way to what arrives.
void ProjectWorkspace::adopt(std::shared_ptr<ProjectTab> tab)
{
    if (m_tabs.size() == 1 && !current().session.hasDocument())
        m_tabs.clear();
    m_tabs.push_back(tab);
    m_selectedID = tab->id;
    emit changed();
}

void ProjectWorkspace::select(QUuid id)
{
    if (id == m_selectedID || m_isManaging || !tab(id))
        return;
    m_selectedID = id;
    emit changed();
}

void ProjectWorkspace::newTab()
{
    if (m_isManaging)
        return;
    // A tab still on its welcome sheet is reused.
    if (!current().session.hasDocument())
        return;
    addTab(false);
}

void ProjectWorkspace::createDocument(QSizeF size)
{
    if (m_isManaging || !(size.width() > 0 && size.height() > 0))
        return;
    ProjectTab &into = current().session.hasDocument() ? addTab(false) : current();
    into.session.createDocument(size);
    emit changed();
}

void ProjectWorkspace::close(QUuid id, std::function<void()> done)
{
    const std::shared_ptr<ProjectTab> closing = tab(id);
    if (m_isManaging || !closing) {
        finish(done);
        return;
    }
    setManaging(true);
    confirmClose(closing, [this, closing, done](bool confirmed) {
        if (confirmed)
            removeTab(closing->id);
        setManaging(false);
        finish(done);
    });
}

void ProjectWorkspace::removeTab(QUuid id)
{
    size_t index = 0;
    while (index < m_tabs.size() && m_tabs[index]->id != id)
        index += 1;
    if (std::erase_if(m_tabs, [&](const std::shared_ptr<ProjectTab> &each) { return each->id == id; }) == 0)
        return;
    if (m_tabs.empty())
        addTab(false);
    else if (m_selectedID == id)
        m_selectedID = m_tabs[std::min(index, m_tabs.size() - 1)]->id;
    emit changed();
}

void ProjectWorkspace::confirmClose(const std::shared_ptr<ProjectTab> &tab, std::function<void(bool)> then)
{
    if (!tab->session.hasDocument() || !tab->session.isModified()) {
        then(true);
        return;
    }
    m_selectedID = tab->id;
    emit changed();
    auto *alert = new QMessageBox(window);
    alert->setObjectName(QStringLiteral("saveChangesAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(QStringLiteral("Save changes to %1?").arg(tab->title()));
    alert->setInformativeText(QStringLiteral("Your changes will be lost if you don’t save them."));
    const QPushButton *save = alert->addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    alert->addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    const QPushButton *discard = alert->addButton(QStringLiteral("Don’t Save"), QMessageBox::DestructiveRole);
    connect(alert, &QDialog::finished, this, [this, alert, save, discard, tab, then] {
        if (alert->clickedButton() == save)
            this->save(tab->id, false, then);
        else
            then(alert->clickedButton() == discard);
    });
    alert->open();
}

void ProjectWorkspace::confirmQuit(std::function<void(bool)> done)
{
    if (m_isManaging) {
        finish(done, false);
        return;
    }
    setManaging(true);
    // The document on screen first, then the rest in order.
    std::vector<std::shared_ptr<ProjectTab>> order{tab(current().id)};
    for (const std::shared_ptr<ProjectTab> &each : m_tabs) {
        if (each->id != current().id)
            order.push_back(each);
    }
    askNext(order, 0, std::move(done));
}

void ProjectWorkspace::askNext(std::vector<std::shared_ptr<ProjectTab>> order, size_t index, std::function<void(bool)> done)
{
    if (index == order.size()) {
        setManaging(false);
        finish(done, true);
        return;
    }
    const std::shared_ptr<ProjectTab> asked = order[index];
    confirmClose(asked, [this, order, index, done](bool confirmed) {
        if (confirmed) {
            askNext(order, index + 1, done);
            return;
        }
        setManaging(false);
        finish(done, false);
    });
}

void ProjectWorkspace::closeWindow(QWidget *closing)
{
    confirmQuit([this, closing = QPointer<QWidget>(closing)](bool confirmed) {
        if (!confirmed)
            return;
        // The window's close asks us; this mark lets it through.
        if (closing) {
            closing->setProperty("closeConfirmed", true);
            closing->close();
        }
    });
}

void ProjectWorkspace::receive(const QStringList &paths)
{
    for (const QString &path : paths)
        openFile(path);
}

void ProjectWorkspace::showError(const QString &title, const QString &message)
{
    qCWarning(lcIO).noquote() << title + QStringLiteral(": ") + message;
    auto *alert = new QMessageBox(window);
    alert->setObjectName(QStringLiteral("fileAlert"));
    alert->setAttribute(Qt::WA_DeleteOnClose);
    alert->setIcon(QMessageBox::Warning);
    alert->setText(title);
    alert->setInformativeText(message);
    alert->addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    alert->open();
}

QStringList ProjectWorkspace::recentFiles()
{
    return QSettings().value(recentKey).toStringList();
}

void ProjectWorkspace::noteRecent(const QString &path)
{
    QStringList recent = recentFiles();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    recent.removeAll(absolute);
    recent.prepend(absolute);
    QSettings().setValue(recentKey, recent.mid(0, recentLimit));
}

void ProjectWorkspace::clearRecent()
{
    QSettings().remove(recentKey);
}
