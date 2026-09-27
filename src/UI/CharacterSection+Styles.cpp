#include "UI/CharacterSection.h"
#include "UI/ToolHeaderStyle.h"
#include <QAction>
#include <QActionGroup>

namespace {
// "Heading", with "+" when what's shown differs from it.
QString styleLabel(const EditorSession &session, TextStyleKind kind)
{
    bool overridden = false;
    const std::optional<QUuid> id = session.shownTextStyle(kind, &overridden);
    const TextStyle *style = id ? session.textStyle(*id) : nullptr;
    return style ? style->name + (overridden ? QStringLiteral("+") : QString()) : QString();
}
}

void CharacterSection::buildStyles()
{
    m_styles = new QToolButton(this);
    m_styles->setObjectName(QStringLiteral("characterStyles"));
    m_styles->setAutoRaise(true);
    m_styles->setPopupMode(QToolButton::InstantPopup);
    m_styles->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_styles->setFont(ToolHeaderStyle::controlFont());
    m_styles->setMaximumWidth(150);
    m_styles->setToolTip(QStringLiteral("Text style: apply, make or redefine character and paragraph styles"));
    m_styles->setAccessibleName(QStringLiteral("Text style"));
    QMenu *menu = stylesMenu();
    m_styles->setMenu(menu);
    trailing->addWidget(m_styles);
}

// Filled as it opens: the document's styles, then what can be done with the one shown.
QMenu *CharacterSection::stylesMenu()
{
    auto *menu = new QMenu(m_styles);
    menu->setObjectName(QStringLiteral("characterStylesMenu"));
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        const bool texts = !m_session.selectedTexts().empty();
        std::optional<QUuid> shown[2];
        bool overridden[2] = {false, false};
        for (const TextStyleKind kind : {TextStyleKind::paragraph, TextStyleKind::character})
            shown[int(kind)] = m_session.shownTextStyle(kind, &overridden[int(kind)]);
        for (const TextStyleKind kind : {TextStyleKind::paragraph, TextStyleKind::character}) {
            menu->addSection(kind == TextStyleKind::paragraph ? QStringLiteral("Paragraph Styles") : QStringLiteral("Character Styles"));
            bool any = false;
            for (const TextStyle &style : m_session.document() ? m_session.document()->textStyles : std::vector<TextStyle>{}) {
                if (style.kind != kind)
                    continue;
                any = true;
                const bool current = shown[int(kind)] == style.id;
                QAction *entry = menu->addAction(style.name + (current && overridden[int(kind)] ? QStringLiteral("+") : QString()));
                entry->setObjectName(QStringLiteral("textStyle:") + style.name);
                entry->setCheckable(true);
                entry->setChecked(current);
                entry->setEnabled(texts);
                const QUuid id = style.id;
                connect(entry, &QAction::triggered, this, [this, id] { m_session.applyTextStyle(id); });
            }
            if (!any)
                menu->addAction(QStringLiteral("None yet"))->setEnabled(false);
        }
        menu->addSeparator();
        const auto add = [&](const QString &name, const QString &text, bool enabled, const std::function<void()> &run) {
            QAction *entry = menu->addAction(text);
            entry->setObjectName(name);
            entry->setEnabled(enabled);
            connect(entry, &QAction::triggered, this, run);
        };
        add(QStringLiteral("newParagraphStyle"), QStringLiteral("New Paragraph Style"), texts,
            [this] { m_session.newTextStyle(TextStyleKind::paragraph); });
        add(QStringLiteral("newCharacterStyle"), QStringLiteral("New Character Style"), texts,
            [this] { m_session.newTextStyle(TextStyleKind::character); });
        // The overridden style, the character one first as it's nearer; else whichever is shown.
        const TextStyle *style = nullptr;
        for (const bool needsOverride : {true, false}) {
            for (const TextStyleKind kind : {TextStyleKind::character, TextStyleKind::paragraph}) {
                if (!style && shown[int(kind)] && (overridden[int(kind)] || !needsOverride))
                    style = m_session.textStyle(*shown[int(kind)]);
            }
        }
        const bool changed = style && overridden[int(style->kind)];
        const QUuid id = style ? style->id : QUuid();
        const TextStyleKind kind = style ? style->kind : TextStyleKind::character;
        add(QStringLiteral("redefineStyle"), style ? QStringLiteral("Redefine “%1”").arg(style->name) : QStringLiteral("Redefine Style"), changed,
            [this, id] { m_session.redefineTextStyle(id); });
        add(QStringLiteral("clearOverrides"), QStringLiteral("Clear Overrides"), changed, [this, kind] { m_session.clearTextOverrides(kind); });
        add(QStringLiteral("detachStyle"), QStringLiteral("Detach Style"), style != nullptr, [this, kind] { m_session.clearTextStyle(kind); });
        menu->addSeparator();
        add(QStringLiteral("openTypeStyles"), QStringLiteral("Type Styles…"), true, [this] {
            if (QAction *action = window()->findChild<QAction *>(QStringLiteral("showTypeStyles")); action && action->isEnabled())
                action->trigger();
        });
    });
    return menu;
}

void CharacterSection::synchronizeStyles()
{
    QStringList names;
    for (const TextStyleKind kind : {TextStyleKind::paragraph, TextStyleKind::character}) {
        const QString name = styleLabel(m_session, kind);
        if (!name.isEmpty())
            names << name;
    }
    m_styles->setText(names.isEmpty() ? QStringLiteral("No style") : names.join(QStringLiteral(" · ")));
    m_styles->setForegroundRole(names.isEmpty() ? QPalette::PlaceholderText : QPalette::ButtonText);
}
