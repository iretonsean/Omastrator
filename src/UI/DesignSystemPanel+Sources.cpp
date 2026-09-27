#include "Document/EditorSession.h"
#include "Live/Browser.h"
#include "System/Library.h"
#include "System/OmarchyThemes.h"
#include "System/ProjectCode.h"
#include "UI/DesignSystemPanel.h"
#include "UI/SyncConfirmDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QString folderKey = QStringLiteral("designSystem/projectFolder");

QPushButton *button(const QString &text, const QString &name, QWidget *parent)
{
    auto *made = new QPushButton(text, parent);
    made->setObjectName(name);
    return made;
}

// A confirmed plan's outcome, in a line.
QString outcome(const QString &failed, bool confirmed, const QString &done)
{
    if (!failed.isEmpty())
        return failed;
    return confirmed ? done : QStringLiteral("Cancelled. Nothing was changed.");
}
}

QWidget *DesignSystemPanel::buildSources()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);

    auto *code = new QGroupBox(QStringLiteral("Project code"), page);
    auto *codeLayout = new QVBoxLayout(code);
    auto *folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(QSettings().value(folderKey).toString(), code);
    m_folder->setObjectName(QStringLiteral("projectFolder"));
    m_folder->setPlaceholderText(QStringLiteral("Project folder"));
    m_folder->setAccessibleName(QStringLiteral("Project folder"));
    connect(m_folder, &QLineEdit::editingFinished, this, &DesignSystemPanel::refreshSources);
    auto *choose = button(QStringLiteral("Choose…"), QStringLiteral("chooseFolder"), code);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Project Folder"), m_folder->text());
        if (!folder.isEmpty()) {
            m_folder->setText(folder);
            refreshSources();
        }
    });
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(choose);
    codeLayout->addLayout(folderRow);
    m_detected = new QLabel(code);
    m_detected->setWordWrap(true);
    codeLayout->addWidget(m_detected);
    auto *codeButtons = new QHBoxLayout;
    auto *pull = button(QStringLiteral("Pull"), QStringLiteral("pullFromCode"), code);
    auto *push = button(QStringLiteral("Push"), QStringLiteral("pushToCode"), code);
    connect(pull, &QPushButton::clicked, this, [this] { pullFromCode(m_folder->text()); });
    connect(push, &QPushButton::clicked, this, [this] { pushToCode(m_folder->text()); });
    codeButtons->addWidget(pull);
    codeButtons->addWidget(push);
    codeLayout->addLayout(codeButtons);
    layout->addWidget(code);

    auto *library = new QGroupBox(QStringLiteral("Library"), page);
    auto *libraryLayout = new QVBoxLayout(library);
    m_library = new QComboBox(library);
    m_library->setObjectName(QStringLiteral("libraryName"));
    m_library->setEditable(true);
    m_library->setAccessibleName(QStringLiteral("Library"));
    connect(m_library, &QComboBox::currentTextChanged, this, [this] {
        if (!m_rebuilding)
            refreshSources();
    });
    libraryLayout->addWidget(m_library);
    m_libraryComponents = new QTreeWidget(library);
    m_libraryComponents->setHeaderHidden(true);
    m_libraryComponents->setRootIsDecorated(false);
    m_libraryComponents->setMaximumHeight(110);
    connect(m_libraryComponents, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item) { placeFromLibrary(m_library->currentText(), item->text(0)); });
    libraryLayout->addWidget(m_libraryComponents);
    auto *libraryButtons = new QHBoxLayout;
    auto *use = button(QStringLiteral("Use Tokens"), QStringLiteral("useLibrary"), library);
    auto *save = button(QStringLiteral("Save to Library"), QStringLiteral("saveToLibrary"), library);
    connect(use, &QPushButton::clicked, this, [this] { useLibrary(m_library->currentText()); });
    connect(save, &QPushButton::clicked, this, [this] { saveToLibrary(m_library->currentText()); });
    libraryButtons->addWidget(use);
    libraryButtons->addWidget(save);
    libraryLayout->addLayout(libraryButtons);
    layout->addWidget(library);

    auto *site = new QGroupBox(QStringLiteral("Site"), page);
    auto *siteLayout = new QHBoxLayout(site);
    m_site = new QLineEdit(site);
    m_site->setObjectName(QStringLiteral("siteAddress"));
    m_site->setPlaceholderText(QStringLiteral("https://example.com"));
    m_site->setAccessibleName(QStringLiteral("Site address"));
    auto *extract = button(QStringLiteral("Extract"), QStringLiteral("extractSite"), site);
    connect(extract, &QPushButton::clicked, this, [this] { extractSite(QUrl::fromUserInput(m_site->text())); });
    siteLayout->addWidget(m_site, 1);
    siteLayout->addWidget(extract);
    layout->addWidget(site);

    auto *theme = new QGroupBox(QStringLiteral("Omarchy theme"), page);
    auto *themeLayout = new QVBoxLayout(theme);
    m_theme = new QLabel(theme);
    themeLayout->addWidget(m_theme);
    auto *useTheme = button(QStringLiteral("Use Theme Tokens"), QStringLiteral("useOmarchyTheme"), theme);
    connect(useTheme, &QPushButton::clicked, this, [this] { useOmarchyTheme(); });
    themeLayout->addWidget(useTheme);
    auto *saveRow = new QHBoxLayout;
    m_themeName = new QLineEdit(theme);
    m_themeName->setObjectName(QStringLiteral("themeName"));
    m_themeName->setPlaceholderText(QStringLiteral("New theme name"));
    m_themeName->setAccessibleName(QStringLiteral("New theme name"));
    auto *saveTheme = button(QStringLiteral("Save Theme"), QStringLiteral("saveOmarchyTheme"), theme);
    connect(saveTheme, &QPushButton::clicked, this, [this] { saveOmarchyTheme(m_themeName->text(), m_applyTheme->isChecked()); });
    saveRow->addWidget(m_themeName, 1);
    saveRow->addWidget(saveTheme);
    themeLayout->addLayout(saveRow);
    m_applyTheme = new QCheckBox(QStringLiteral("Apply to the desktop"), theme);
    m_applyTheme->setChecked(true);
    themeLayout->addWidget(m_applyTheme);
    layout->addWidget(theme);
    layout->addStretch();
    scroll->setWidget(page);
    refreshSources();
    return scroll;
}

void DesignSystemPanel::refreshSources()
{
    const QString folder = m_folder->text().trimmed();
    if (folder.isEmpty()) {
        m_detected->setText(QStringLiteral("Choose the folder a site's code lives in."));
    } else {
        QSettings().setValue(folderKey, folder);
        QStringList labels;
        for (const ProjectCode::Source &source : ProjectCode::detect(folder))
            labels.append(source.label());
        m_detected->setText(labels.isEmpty() ? QStringLiteral("No token files yet. Push writes a tokens.json.") : labels.join(QLatin1Char('\n')));
    }
    const bool was = m_rebuilding;
    m_rebuilding = true;
    const QString chosen = m_library->currentText().isEmpty() ? QStringLiteral("Personal") : m_library->currentText();
    m_library->clear();
    QStringList names = Library::names();
    if (!names.contains(chosen))
        names.prepend(chosen);
    m_library->addItems(names);
    m_library->setCurrentText(chosen);
    m_rebuilding = was;
    m_libraryComponents->clear();
    for (const QString &set : Library::load(chosen).sets())
        new QTreeWidgetItem(m_libraryComponents, {set});
    const QString current = OmarchyThemes::currentName();
    m_theme->setText(current.isEmpty() ? QStringLiteral("No Omarchy theme found.") : QStringLiteral("Current theme: %1").arg(current));
}

QString DesignSystemPanel::pullFromCode(const QString &folder)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    QPointer<EditorSession> target = s;
    const SyncPlan plan = ProjectCode::pullPlan(folder, documentName(), [target](const ProjectCode::Pulled &pulled) {
        if (!target)
            return QStringLiteral("The document was closed.");
        target->mergeTokens(pulled.tokens, QStringLiteral("Pull Tokens from Code"), pulled.modes);
        return QString();
    });
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    return say(outcome(failed, confirmed, QStringLiteral("Tokens pulled from %1.").arg(folder)));
}

QString DesignSystemPanel::pushToCode(const QString &folder)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    const SyncPlan plan = ProjectCode::pushPlan(folder, s->document()->tokens, s->document()->tokenModes);
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    refreshSources();
    return say(outcome(failed, confirmed, plan.git && plan.git->commit ? QStringLiteral("Tokens written and committed on %1.").arg(plan.git->branch)
                                                                       : QStringLiteral("Tokens written.")));
}

QString DesignSystemPanel::saveToLibrary(const QString &name)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    const SyncPlan plan = Library::pushPlan(name.trimmed().isEmpty() ? QStringLiteral("Personal") : name.trimmed(), *s->document());
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    refreshSources();
    return say(outcome(failed, confirmed, QStringLiteral("Saved to the library.")));
}

QString DesignSystemPanel::useLibrary(const QString &name)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    QPointer<EditorSession> target = s;
    const SyncPlan plan = Library::pullPlan(name, documentName(), [target](const Library::Contents &library) {
        if (!target)
            return QStringLiteral("The document was closed.");
        target->mergeTokens(library.tokens, QStringLiteral("Use Library Tokens"), library.modes);
        return QString();
    });
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    return say(outcome(failed, confirmed, QStringLiteral("Library tokens added.")));
}

QString DesignSystemPanel::placeFromLibrary(const QString &name, const QString &set)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    const Library::Contents library = Library::load(name);
    const std::vector<VectorObject> objects = library.set(set);
    if (objects.empty())
        return say(QStringLiteral("The library “%1” has no component “%2”.").arg(name, set));
    // Reading the library into the document writes nothing, so it needs no confirmation; its tokens come along.
    s->beginEdit(QStringLiteral("Place Component"));
    s->mergeTokens(library.tokens, QStringLiteral("Place Component"), library.modes);
    s->placeFromLibrary(objects, set, {});
    s->endEdit();
    return say(QString());
}

QString DesignSystemPanel::extractSite(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return say(QStringLiteral("Type the address of a site."));
    if (Browser::executable().isEmpty())
        return say(QStringLiteral("Extracting needs Chromium or Chrome."));
    say(QStringLiteral("Reading %1…").arg(url.toString()));
    QTemporaryDir profile;
    Browser browser;
    Browser::Options options;
    options.headless = true;
    options.profile = profile.path();
    if (const QString failed = browser.start(options); !failed.isEmpty())
        return say(failed);
    QString error;
    const auto page = browser.attachPage(url, &error);
    if (!page)
        return say(error.isEmpty() ? QStringLiteral("Couldn't open %1.").arg(url.toString()) : error);
    const QJsonObject scan = SiteExtract::scan(browser, page->sessionId, &error);
    browser.stop();
    if (!error.isEmpty())
        return say(error);
    return useProposal(SiteExtract::propose(scan, url.toString()));
}

QString DesignSystemPanel::useProposal(const SiteExtract::Proposal &proposal)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    QPointer<EditorSession> target = s;
    const SyncPlan plan = SiteExtract::pullPlan(proposal, documentName(), [target](const SiteExtract::Proposal &taken) {
        if (!target)
            return QStringLiteral("The document was closed.");
        target->beginEdit(QStringLiteral("Use Design System from Site"));
        target->mergeTokens(taken.tokens, QStringLiteral("Use Design System from Site"));
        target->placeFromLibrary(taken.objects, QString(), {});
        target->endEdit();
        return QString();
    });
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    return say(outcome(failed, confirmed, QStringLiteral("%1 tokens and %2 components taken from the site.").arg(proposal.tokens.size()).arg(proposal.components().size())));
}

QString DesignSystemPanel::useOmarchyTheme()
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    const QString name = OmarchyThemes::currentName();
    const QString directory = QFileInfo(OmarchyThemes::currentDirectory()).isDir() ? OmarchyThemes::currentDirectory() : OmarchyThemes::directoryOf(name);
    QPointer<EditorSession> target = s;
    const SyncPlan plan = OmarchyThemes::pullPlan(directory, name, documentName(), [target](const OmarchyThemes::Theme &theme) {
        if (!target)
            return QStringLiteral("The document was closed.");
        target->mergeTokens(theme.tokens, QStringLiteral("Use Omarchy Theme"));
        return QString();
    });
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    if (confirmed && failed.isEmpty() && m_themeName->text().isEmpty())
        m_themeName->setText(name + QStringLiteral(" Remix"));
    return say(outcome(failed, confirmed, QStringLiteral("The theme's colours are tokens now. Edit them, then Save Theme.")));
}

QString DesignSystemPanel::saveOmarchyTheme(const QString &name, bool apply)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return say(QStringLiteral("Open a document first."));
    const QString current = OmarchyThemes::currentName();
    // A theme of that name already there is changed in place; otherwise the new one starts from the current theme.
    const QString existing = QDir::homePath() + QStringLiteral("/.config/omarchy/themes/") + OmarchyThemes::slug(name);
    const QString source = QFileInfo(existing).isDir() ? existing : OmarchyThemes::directoryOf(current);
    const SyncPlan plan = OmarchyThemes::savePlan(source, name, s->document()->tokens, apply);
    bool confirmed = false;
    const QString failed = SyncConfirmDialog::run(plan, this, &confirmed);
    refreshSources();
    return say(outcome(failed, confirmed, apply ? QStringLiteral("Theme saved and applied.") : QStringLiteral("Theme saved.")));
}
