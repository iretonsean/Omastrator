#include "UI/FrameToolHeader.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include "UI/FramePresets.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>

namespace {
QString serverWord(BrowserViews::Server server)
{
    switch (server) {
    case BrowserViews::Server::none: return QStringLiteral("No server");
    case BrowserViews::Server::starting: return QStringLiteral("Starting…");
    case BrowserViews::Server::running: return QStringLiteral("Running");
    case BrowserViews::Server::paused: return QStringLiteral("Frozen");
    case BrowserViews::Server::failed: return QStringLiteral("Failed");
    }
    return {};
}
}

FrameToolHeader::FrameToolHeader(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Frame"), parent), m_session(session), m_size(new QComboBox(this)),
      m_clip(new QCheckBox(QStringLiteral("Clip content"), this)), m_autoLayout(new QPushButton(this)),
      m_hint(new QLabel(QStringLiteral("Drag to draw · Inside a frame it nests · Shift squares"), this)),
      m_browserView(new QCheckBox(QStringLiteral("Browser View"), this)), m_live(new QWidget(this))
{
    m_size->setObjectName(QStringLiteral("frameSize"));
    m_size->setToolTip(QStringLiteral("Drop a frame of this size in the middle of the view"));
    m_size->setFont(ToolHeaderStyle::controlFont());
    m_size->addItem(QStringLiteral("Drop a size…"));
    // Group names are headings, not choices.
    auto *model = qobject_cast<QStandardItemModel *>(m_size->model());
    QString group;
    for (const FramePresets::Preset &preset : FramePresets::builtIn()) {
        if (group != QString::fromUtf8(preset.group)) {
            group = QString::fromUtf8(preset.group);
            m_size->addItem(group);
            if (model)
                model->item(m_size->count() - 1)->setEnabled(false);
        }
        m_size->addItem(QStringLiteral("   %1  %2 × %3").arg(QString::fromUtf8(preset.name)).arg(preset.size.width()).arg(preset.size.height()),
                        QVariant::fromValue(preset.size));
        m_size->setItemData(m_size->count() - 1, QString::fromUtf8(preset.name), Qt::UserRole + 1);
    }
    // After the title, before the stretch that ends the row, as every tool bar has them.
    int at = 1;
    row->insertWidget(at++, m_size);
    m_clip->setObjectName(QStringLiteral("frameClip"));
    m_clip->setFont(ToolHeaderStyle::controlFont());
    m_clip->setToolTip(QStringLiteral("Show the selected frames' children only inside their box"));
    row->insertWidget(at++, m_clip);
    m_autoLayout->setObjectName(QStringLiteral("frameAutoLayout"));
    m_autoLayout->setFont(ToolHeaderStyle::controlFont());
    row->insertWidget(at++, m_autoLayout);
    m_browserView->setObjectName(QStringLiteral("frameBrowserView"));
    m_browserView->setFont(ToolHeaderStyle::controlFont());
    m_browserView->setToolTip(QStringLiteral("Show the frame's web page, run from its project's dev server. Off freezes the server"));
    row->insertWidget(at++, m_browserView);
    // The Live group: the same actions as Object ▸ Browser View, for the frame whose switch is on.
    m_live->setObjectName(QStringLiteral("frameLive"));
    auto *live = new QHBoxLayout(m_live);
    live->setContentsMargins(0, 0, 0, 0);
    live->setSpacing(4);
    m_server = new QLabel(m_live);
    m_server->setObjectName(QStringLiteral("frameServer"));
    m_server->setFont(ToolHeaderStyle::controlFont());
    m_server->setForegroundRole(QPalette::PlaceholderText);
    live->addWidget(m_server);
    m_editPage = liveButton(QStringLiteral("frameEditPage"), QStringLiteral("Edit Page"), QStringLiteral("Pick the page's elements and change them (double-click the frame)"));
    m_editPage->setCheckable(true);
    live->addWidget(m_editPage);
    connect(m_editPage, &QPushButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        if (m_canvas->editPageFrame())
            m_canvas->leaveEditPage();
        else if (const auto frame = browserFrame())
            m_canvas->enterEditPage(*frame);
        synchronize();
    });
    // The host's own actions, as the bar menu and Object ▸ Browser View run them.
    const struct {
        const char *name;
        const char *text;
        const char *tip;
        BrowserViewHost::Action action;
    } actions[] = {{"frameReload", "Reload", "Reload the page", BrowserViewHost::Action::reload},
                   {"frameChanges", "Changes", "Review the changes Live made to the project", BrowserViewHost::Action::reviewChanges},
                   {"frameHistory", "History", "The project's saved versions", BrowserViewHost::Action::history},
                   {"frameDeploy", "Deploy", "Save the changes and deploy the site", BrowserViewHost::Action::deploy},
                   {"frameStopLive", "Stop Live", "End Live on this frame; its dev server stops", BrowserViewHost::Action::stopLive}};
    for (const auto &each : actions) {
        QPushButton *button = liveButton(QString::fromLatin1(each.name), QString::fromUtf8(each.text), QString::fromUtf8(each.tip));
        live->addWidget(button);
        if (each.action == BrowserViewHost::Action::stopLive)
            m_stopLive = button;
        connect(button, &QPushButton::clicked, this, [this, action = each.action] {
            if (const auto frame = browserFrame())
                BrowserViews::of(m_session)->act(*frame, action);
            synchronize();
        });
    }
    row->insertWidget(at++, m_live);
    m_hint->setFont(ToolHeaderStyle::controlFont());
    m_hint->setForegroundRole(QPalette::PlaceholderText);
    row->insertWidget(at++, m_hint);
    connect(m_size, &QComboBox::activated, this, [this](int index) {
        const QSizeF size = m_size->itemData(index).toSizeF();
        m_size->setCurrentIndex(0);
        if (!m_session.hasDocument() || size.isEmpty())
            return;
        m_session.addFrame(m_session.framePlacement(size), m_size->itemData(index, Qt::UserRole + 1).toString());
        m_session.selectTool(Tool::select);
    });
    connect(m_clip, &QCheckBox::toggled, this, [this](bool clips) { m_session.setClipsContent(clips); });
    connect(m_autoLayout, &QPushButton::clicked, this, [this] {
        if (m_session.canRemoveAutoLayout())
            m_session.removeAutoLayout();
        else
            m_session.addAutoLayout();
    });
    connect(m_browserView, &QCheckBox::clicked, this, [this] { flip(); });
    connect(&m_session, &EditorSession::changed, this, &FrameToolHeader::synchronize);
    BrowserViews *views = BrowserViews::of(m_session);
    connect(views, &BrowserViews::browserViewChanged, this, &FrameToolHeader::synchronize);
    connect(views, &BrowserViews::frameChanged, this, &FrameToolHeader::synchronize);
    synchronize();
}

void FrameToolHeader::setCanvas(EditorCanvas *canvas)
{
    if (m_canvas)
        disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (m_canvas)
        connect(m_canvas, &EditorCanvas::editPageChanged, this, &FrameToolHeader::synchronize);
    synchronize();
}

QPushButton *FrameToolHeader::liveButton(const QString &name, const QString &text, const QString &tip)
{
    auto *button = new QPushButton(text, m_live);
    button->setObjectName(name);
    button->setFont(ToolHeaderStyle::controlFont());
    button->setToolTip(tip);
    return button;
}

std::optional<QUuid> FrameToolHeader::browserFrame() const
{
    if (!m_session.hasDocument())
        return std::nullopt;
    if (const std::vector<QUuid> frames = m_session.selectedFrames(); frames.size() == 1)
        return frames.front();
    // Edit Page deselects the document, so its frame stands in for the selected one.
    if (m_canvas)
        return m_canvas->editPageFrame();
    return std::nullopt;
}

void FrameToolHeader::flip()
{
    const auto frame = browserFrame();
    if (!frame)
        return synchronize();
    // The canvas's flip is the pill's: it leaves Edit Page and asks a new view for its address.
    if (m_canvas)
        m_canvas->flipBrowserView(*frame);
    else
        BrowserViews::of(m_session)->setBrowserViewOn(*frame, !m_session.browserViewOn(*frame));
    synchronize();
}

void FrameToolHeader::synchronize()
{
    const bool frames = !m_session.selectedFrames().empty();
    m_size->setEnabled(m_session.hasDocument());
    m_clip->setEnabled(frames);
    const QSignalBlocker quiet(m_clip);
    m_clip->setChecked(frames && m_session.selectedFramesClip());
    m_autoLayout->setEnabled(frames || m_session.canRemoveAutoLayout());
    m_autoLayout->setText(m_session.canRemoveAutoLayout() ? QStringLiteral("Remove Auto Layout") : QStringLiteral("Add Auto Layout"));
    m_autoLayout->setToolTip(QStringLiteral("Shift+A"));

    const auto frame = browserFrame();
    const bool unlocked = frame && !m_session.isDocumentLocked() && !m_session.document()->isEffectivelyLocked(*frame);
    const bool on = frame && m_session.browserViewOn(*frame);
    m_browserView->setEnabled(unlocked);
    {
        const QSignalBlocker quietSwitch(m_browserView);
        m_browserView->setChecked(on);
    }
    m_live->setVisible(on);
    // One row: the help gives way to the Live controls.
    m_hint->setVisible(!on);
    if (!on)
        return;
    const VectorObject *object = m_session.document()->find(*frame);
    const bool noPage = !object || !object->browser || object->browser->url.isEmpty();
    const BrowserViews *views = BrowserViews::of(m_session);
    const BrowserViews::Server server = views->server(*frame);
    m_server->setText(noPage ? QStringLiteral("Type a URL") : serverWord(server));
    m_editPage->setVisible(m_canvas != nullptr);
    m_editPage->setEnabled(!noPage);
    const QSignalBlocker quietEdit(m_editPage);
    m_editPage->setChecked(m_canvas && m_canvas->editPageFrame() == frame);
    m_stopLive->setEnabled(server != BrowserViews::Server::none);
}
