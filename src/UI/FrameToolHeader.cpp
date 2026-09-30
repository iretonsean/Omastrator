#include "UI/FrameToolHeader.h"
#include "Document/EditorSession.h"
#include "UI/FramePresets.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>

FrameToolHeader::FrameToolHeader(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QStringLiteral("Frame"), parent), m_session(session), m_size(new QComboBox(this)),
      m_clip(new QCheckBox(QStringLiteral("Clip content"), this)), m_autoLayout(new QPushButton(this))
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
    row->addWidget(m_size);
    m_clip->setObjectName(QStringLiteral("frameClip"));
    m_clip->setFont(ToolHeaderStyle::controlFont());
    m_clip->setToolTip(QStringLiteral("Show the selected frames' children only inside their box"));
    row->addWidget(m_clip);
    m_autoLayout->setObjectName(QStringLiteral("frameAutoLayout"));
    m_autoLayout->setFont(ToolHeaderStyle::controlFont());
    row->addWidget(m_autoLayout);
    auto *hint = new QLabel(QStringLiteral("Drag to draw · Inside a frame it nests · Shift squares"), this);
    hint->setFont(ToolHeaderStyle::controlFont());
    hint->setForegroundRole(QPalette::PlaceholderText);
    row->addWidget(hint);
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
    connect(&m_session, &EditorSession::changed, this, &FrameToolHeader::synchronize);
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
}
