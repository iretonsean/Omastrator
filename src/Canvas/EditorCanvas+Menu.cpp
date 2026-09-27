#include "Canvas/EditorCanvasState.h"
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>

namespace {
QString titleCase(const QString &text)
{
    QString result = text.toLower();
    bool start = true;
    for (QChar &c : result) {
        if (c.isLetter() && start)
            c = c.toUpper();
        start = !c.isLetterOrNumber() && c != QLatin1Char('\'');
    }
    return result;
}

QString sentenceCase(const QString &text)
{
    QString result = text.toLower();
    bool start = true;
    for (QChar &c : result) {
        if (c.isLetter() && start) {
            c = c.toUpper();
            start = false;
        } else if (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QLatin1Char('\n')) {
            start = true;
        }
    }
    return result;
}
}

void EditorCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_session.hasDocument() || m_paused || m_state->drag) {
        event->ignore();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    event->accept();
    if (m_state->text) {
        QMenu *menu = textEditingMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
        return;
    }
    m_state->finishOpacity();
    if (m_state->pen)
        m_state->finishPen();
    // The menu key asks about the selection where it is; a click asks about what's under the pointer.
    const bool keyboard = event->reason() == QContextMenuEvent::Keyboard;
    const QPointF view = keyboard ? (m_session.hasSelection() ? m_state->toView(m_session.selectionBounds().center()) : QPointF(rect().center()))
                                  : QPointF(event->pos());
    const QPointF document = m_state->toDocument(view);
    QList<QUuid> hits;
    for (const QUuid &id : m_session.document()->hitTestAll(document, m_state->reach(3)))
        hits.append(id);
    if (!keyboard) {
        // As in Illustrator and Figma: an unselected object becomes the selection; inside the selection it stays.
        std::optional<QUuid> target;
        if (!hits.isEmpty())
            target = m_session.tool() == Tool::directSelect ? std::optional(hits.front()) : m_state->selectableTarget(hits.front());
        if (target) {
            if (!m_session.isSelected(*target))
                m_session.select({*target});
        } else if (!m_session.hasSelection() || !m_session.selectionBounds().contains(document)) {
            m_session.deselectAll();
        }
    }
    update();
    emit contextMenuRequested(keyboard ? mapToGlobal(view.toPoint()) : event->globalPos(), hits);
}

std::optional<QUuid> EditorCanvas::isolatedGroup() const
{
    return m_session.isolatedGroup();
}

void EditorCanvas::isolateGroup(const QUuid &group)
{
    const VectorObject *object = m_session.document() ? m_session.document()->find(group) : nullptr;
    if (!object || object->kind != ObjectKind::group)
        return;
    if (m_session.tool() != Tool::select && m_session.tool() != Tool::directSelect)
        m_session.selectTool(Tool::select);
    m_session.isolate(group);
    update();
}

void EditorCanvas::exitIsolation()
{
    m_session.exitIsolation();
    update();
}

QMenu *EditorCanvas::textEditingMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("textEditingMenu"));
    if (!m_state->text)
        return menu;
    // Each entry types into the editor as its key would.
    const auto type = [this](int key, Qt::KeyboardModifiers modifiers, const QString &typed) {
        if (!m_state->text)
            return;
        const QKeyEvent press(QEvent::KeyPress, key, modifiers, typed);
        if (m_state->text->keyPress(press) == InlineTextEditor::Result::edited)
            m_state->applyText();
        m_state->restartCaret();
    };
    const InlineTextEditor &editor = *m_state->text;
    const bool ranged = editor.caret != editor.anchor;
    const auto add = [&](QMenu *into, const QString &name, const QString &label, const QKeySequence &keys, std::function<void()> run) {
        QAction *entry = into->addAction(label, this, std::move(run));
        entry->setObjectName(name);
        entry->setShortcut(keys);
        return entry;
    };
    add(menu, QStringLiteral("textCut"), QStringLiteral("Cut"), QKeySequence::Cut, [type] { type(Qt::Key_X, Qt::ControlModifier, QString()); })->setEnabled(ranged);
    add(menu, QStringLiteral("textCopy"), QStringLiteral("Copy"), QKeySequence::Copy, [type] { type(Qt::Key_C, Qt::ControlModifier, QString()); })->setEnabled(ranged);
    add(menu, QStringLiteral("textPaste"), QStringLiteral("Paste"), QKeySequence::Paste, [type] { type(Qt::Key_V, Qt::ControlModifier, QString()); })
        ->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
    add(menu, QStringLiteral("textSelectAll"), QStringLiteral("Select All"), QKeySequence::SelectAll, [type] { type(Qt::Key_A, Qt::ControlModifier, QString()); });
    menu->addSeparator();
    QMenu *cases = menu->addMenu(QStringLiteral("Change Case"));
    cases->menuAction()->setObjectName(QStringLiteral("textChangeCase"));
    cases->setEnabled(ranged);
    const std::vector<std::pair<QString, std::function<QString(const QString &)>>> changes{
        {QStringLiteral("UPPERCASE"), [](const QString &text) { return text.toUpper(); }},
        {QStringLiteral("lowercase"), [](const QString &text) { return text.toLower(); }},
        {QStringLiteral("Title Case"), titleCase},
        {QStringLiteral("Sentence case"), sentenceCase},
    };
    for (const auto &[label, change] : changes) {
        cases->addAction(label, this, [this, type, change] {
            if (!m_state->text)
                return;
            InlineTextEditor &editor = *m_state->text;
            const int start = std::min(editor.caret, editor.anchor), length = std::abs(editor.caret - editor.anchor);
            const QString changed = change(editor.text().mid(start, length));
            type(Qt::Key_unknown, Qt::NoModifier, changed);
            // The changed run stays selected, as it was.
            if (m_state->text) {
                m_state->text->anchor = start;
                m_state->text->caret = start + int(changed.size());
            }
        });
    }
    QMenu *insert = menu->addMenu(QStringLiteral("Insert"));
    insert->menuAction()->setObjectName(QStringLiteral("textInsert"));
    const std::vector<std::pair<QString, QChar>> characters{
        {QStringLiteral("Em Space"), QChar(0x2003)},      {QStringLiteral("En Space"), QChar(0x2002)}, {QStringLiteral("Thin Space"), QChar(0x2009)},
        {QStringLiteral("Non-Breaking Space"), QChar(0x00a0)}, {QStringLiteral("Em Dash"), QChar(0x2014)}, {QStringLiteral("En Dash"), QChar(0x2013)},
        {QStringLiteral("Ellipsis"), QChar(0x2026)},
    };
    for (const auto &[label, character] : characters)
        insert->addAction(label, this, [type, character] { type(Qt::Key_unknown, Qt::NoModifier, QString(character)); });
    insert->addSeparator();
    insert->addAction(QStringLiteral("Forced Line Break"), this, [type] { type(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r")); });
    return menu;
}
