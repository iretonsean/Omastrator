#include "UI/TextStylesPanel.h"
#include "Document/EditorSession.h"
#include "UI/ToolHeaderStyle.h"
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int idRole = Qt::UserRole;
constexpr int nameRole = Qt::UserRole + 1;
}

TextStylesPanel::TextStylesPanel(std::function<EditorSession *()> session, QWidget *parent)
    : QWidget(parent), m_sessionOf(std::move(session)), m_list(new QListWidget(this)), m_add(new QToolButton(this)), m_delete(new QToolButton(this))
{
    setObjectName(QStringLiteral("typeStyles"));
    setMinimumSize(220, 260);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(10, 10, 10, 10);
    column->setSpacing(6);
    m_list->setObjectName(QStringLiteral("typeStylesList"));
    m_list->setAccessibleName(QStringLiteral("Type styles"));
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setFont(ToolHeaderStyle::controlFont());
    column->addWidget(m_list, 1);
    connect(m_list, &QListWidget::itemClicked, this, &TextStylesPanel::applyItem);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &TextStylesPanel::showMenu);
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_rebuilding || !m_session)
            return;
        const QString name = item->text().trimmed();
        const QUuid id = QUuid::fromString(item->data(idRole).toString());
        // Later: renaming rebuilds the list, and the view is still using this item.
        if (name != item->data(nameRole).toString())
            QTimer::singleShot(0, this, [this, id, name] {
                if (m_session)
                    m_session->renameTextStyle(id, name);
            });
    });

    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    m_add->setObjectName(QStringLiteral("typeStylesNew"));
    m_add->setText(QStringLiteral("+"));
    m_add->setToolTip(QStringLiteral("New style from the selection"));
    m_add->setAccessibleName(QStringLiteral("New style"));
    m_add->setPopupMode(QToolButton::InstantPopup);
    auto *made = new QMenu(m_add);
    made->addAction(QStringLiteral("New Paragraph Style"), this, [this] {
        if (m_session)
            m_session->newTextStyle(TextStyleKind::paragraph);
    })->setObjectName(QStringLiteral("typeStylesNewParagraph"));
    made->addAction(QStringLiteral("New Character Style"), this, [this] {
        if (m_session)
            m_session->newTextStyle(TextStyleKind::character);
    })->setObjectName(QStringLiteral("typeStylesNewCharacter"));
    m_add->setMenu(made);
    m_delete->setObjectName(QStringLiteral("typeStylesDelete"));
    m_delete->setText(QStringLiteral("Delete"));
    m_delete->setToolTip(QStringLiteral("Delete the style; text that used it keeps its look"));
    connect(m_delete, &QToolButton::clicked, this, [this] {
        QListWidgetItem *item = m_list->currentItem();
        if (m_session && item && item->data(idRole).isValid())
            m_session->deleteTextStyle(QUuid::fromString(item->data(idRole).toString()));
    });
    row->addWidget(m_add);
    row->addStretch(1);
    row->addWidget(m_delete);
    column->addLayout(row);
    follow();
}

void TextStylesPanel::follow()
{
    EditorSession *session = m_sessionOf ? m_sessionOf() : nullptr;
    if (session != m_session) {
        disconnect(m_watch);
        m_session = session;
        if (m_session)
            m_watch = connect(m_session, &EditorSession::changed, this, &TextStylesPanel::rebuild);
    }
    rebuild();
}

void TextStylesPanel::rebuild()
{
    m_rebuilding = true;
    const QString current = m_list->currentItem() ? m_list->currentItem()->data(idRole).toString() : QString();
    m_list->clear();
    const bool texts = m_session && !m_session->selectedTexts().empty();
    for (const TextStyleKind kind : {TextStyleKind::paragraph, TextStyleKind::character}) {
        auto *heading = new QListWidgetItem(kind == TextStyleKind::paragraph ? QStringLiteral("Paragraph Styles") : QStringLiteral("Character Styles"), m_list);
        heading->setFlags(Qt::NoItemFlags);
        QFont bold = m_list->font();
        bold.setWeight(QFont::DemiBold);
        heading->setFont(bold);
        bool overridden = false;
        const std::optional<QUuid> shown = texts ? m_session->shownTextStyle(kind, &overridden) : std::nullopt;
        for (const TextStyle &style : m_session && m_session->document() ? m_session->document()->textStyles : std::vector<TextStyle>{}) {
            if (style.kind != kind)
                continue;
            const bool used = shown == style.id;
            auto *item = new QListWidgetItem(style.name + (used && overridden ? QStringLiteral("+") : QString()), m_list);
            item->setData(idRole, style.id.toString());
            item->setData(nameRole, style.name + (used && overridden ? QStringLiteral("+") : QString()));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            item->setToolTip(used ? QStringLiteral("In use by the selection") : QStringLiteral("Click to apply to the selection"));
            if (used) {
                QFont marked = m_list->font();
                marked.setWeight(QFont::DemiBold);
                item->setFont(marked);
            }
            if (style.id.toString() == current)
                m_list->setCurrentItem(item);
        }
    }
    m_add->setEnabled(texts);
    m_delete->setEnabled(m_list->currentItem() != nullptr);
    m_rebuilding = false;
}

void TextStylesPanel::applyItem(QListWidgetItem *item)
{
    if (!m_session || !item || !item->data(idRole).isValid())
        return;
    m_delete->setEnabled(true);
    // Later, for the same reason as renaming.
    const QUuid id = QUuid::fromString(item->data(idRole).toString());
    QTimer::singleShot(0, this, [this, id] {
        if (m_session)
            m_session->applyTextStyle(id);
    });
}

void TextStylesPanel::showMenu(const QPoint &at)
{
    QListWidgetItem *item = m_list->itemAt(at);
    if (!m_session || !item || !item->data(idRole).isValid())
        return;
    const QUuid id = QUuid::fromString(item->data(idRole).toString());
    const bool texts = !m_session->selectedTexts().empty();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Apply"), this, [this, id] { m_session->applyTextStyle(id); })->setEnabled(texts);
    menu.addAction(QStringLiteral("Redefine from Selection"), this, [this, id] { m_session->redefineTextStyle(id); })->setEnabled(texts);
    menu.addAction(QStringLiteral("Rename"), this, [this, item] { m_list->editItem(item); });
    menu.addAction(QStringLiteral("Delete"), this, [this, id] { m_session->deleteTextStyle(id); });
    menu.exec(m_list->viewport()->mapToGlobal(at));
}
