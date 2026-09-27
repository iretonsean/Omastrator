#include "UI/KeyboardShortcuts.h"
#include "Logging.h"
#include <QAbstractButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>
#include <array>
#include <tuple>

namespace {
// Swift's special keys, their Qt keys and their names.
struct Special {
    QString key;
    Qt::Key qt;
};
const std::array specials{
    Special{QStringLiteral("\x7f"), Qt::Key_Backspace}, Special{QStringLiteral("\r"), Qt::Key_Return},
    Special{QStringLiteral("\x1b"), Qt::Key_Escape},    Special{QStringLiteral("\t"), Qt::Key_Tab},
    Special{QStringLiteral(" "), Qt::Key_Space},        Special{QString(QChar(0xf702)), Qt::Key_Left},
    Special{QString(QChar(0xf703)), Qt::Key_Right},     Special{QString(QChar(0xf701)), Qt::Key_Down},
    Special{QString(QChar(0xf700)), Qt::Key_Up},
};

int bits(Qt::KeyboardModifiers modifiers)
{
    return (modifiers.testFlag(Qt::ControlModifier) ? 1 : 0) | (modifiers.testFlag(Qt::AltModifier) ? 2 : 0)
        | (modifiers.testFlag(Qt::MetaModifier) ? 4 : 0) | (modifiers.testFlag(Qt::ShiftModifier) ? 8 : 0);
}

Qt::KeyboardModifiers flags(int bits)
{
    Qt::KeyboardModifiers result;
    result.setFlag(Qt::ControlModifier, bits & 1);
    result.setFlag(Qt::AltModifier, bits & 2);
    result.setFlag(Qt::MetaModifier, bits & 4);
    result.setFlag(Qt::ShiftModifier, bits & 8);
    return result;
}

// A key as Swift's charactersIgnoringModifiers reads it, lowered.
QString keyText(int key)
{
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
        return QStringLiteral("\x7f");
    if (key == Qt::Key_Enter)
        return QStringLiteral("\r");
    if (key == Qt::Key_Backtab)
        return QStringLiteral("\t");
    for (const Special &special : specials) {
        if (special.qt == key)
            return special.key;
    }
    const QString typed = QKeySequence(key).toString(QKeySequence::PortableText).toLower();
    // Shifted brackets and signs count as their own keys.
    static const QHash<QString, QString> unshifted{{QStringLiteral("{"), QStringLiteral("[")}, {QStringLiteral("}"), QStringLiteral("]")},
                                                   {QStringLiteral("+"), QStringLiteral("=")}, {QStringLiteral("_"), QStringLiteral("-")},
                                                   {QStringLiteral("\""), QStringLiteral("'")}, {QStringLiteral("!"), QStringLiteral("1")},
                                                   {QStringLiteral("@"), QStringLiteral("2")}, {QStringLiteral("#"), QStringLiteral("3")},
                                                   {QStringLiteral("&"), QStringLiteral("7")}, {QStringLiteral("*"), QStringLiteral("8")},
                                                   {QStringLiteral(">"), QStringLiteral(".")}, {QStringLiteral("<"), QStringLiteral(",")}};
    return unshifted.value(typed, typed);
}

// Swift's shifted characters for the keys Shift changes.
QString shiftedText(const QString &key, bool shifted)
{
    static const QHash<QString, QString> shifts{{QStringLiteral("["), QStringLiteral("{")}, {QStringLiteral("]"), QStringLiteral("}")},
                                                {QStringLiteral("="), QStringLiteral("+")}, {QStringLiteral("-"), QStringLiteral("_")},
                                                {QStringLiteral("'"), QStringLiteral("\"")}, {QStringLiteral("1"), QStringLiteral("!")},
                                                {QStringLiteral("2"), QStringLiteral("@")}, {QStringLiteral("3"), QStringLiteral("#")},
                                                {QStringLiteral("7"), QStringLiteral("&")}, {QStringLiteral("8"), QStringLiteral("*")},
                                                {QStringLiteral("."), QStringLiteral(">")}, {QStringLiteral(","), QStringLiteral("<")}};
    return shifted ? shifts.value(key, key) : key;
}

Qt::Key qtKey(const QString &key, bool shifted)
{
    for (const Special &special : specials) {
        if (special.key == key)
            return special.qt;
    }
    const QString typed = shiftedText(key, shifted);
    return typed.size() == 1 ? Qt::Key(typed.at(0).toUpper().unicode()) : Qt::Key_unknown;
}

ShortcutDefinition entry(const QString &title, const QString &key, int modifiers, bool menu)
{
    return {title, menu ? QStringLiteral("Menus") : QStringLiteral("Canvas & Layers"), ShortcutChord(key, modifiers)};
}

// Illustrator's tool keys; the other tools have none.
struct ToolKey {
    Tool tool;
    const char *key;
    int modifiers = 0;
};
const std::array<ToolKey, 17> toolKeys{{
    {Tool::select, "v"}, {Tool::directSelect, "a"}, {Tool::pen, "p"}, {Tool::pencil, "n"}, {Tool::text, "t"}, {Tool::line, "\\"},
    {Tool::rectangle, "m"}, {Tool::ellipse, "l"}, {Tool::rotate, "r"}, {Tool::scale, "s"}, {Tool::eyedropper, "i"}, {Tool::hand, "h"},
    {Tool::zoom, "z"}, {Tool::shapeBuilder, "m", 8}, {Tool::gradient, "g"}, {Tool::scissors, "c"}, {Tool::artboard, "o", 8},
}};

QJsonObject encoded(const QHash<QString, ShortcutChord> &values)
{
    QJsonObject object;
    for (auto value = values.cbegin(); value != values.cend(); ++value)
        object.insert(value.key(), QJsonObject{{QStringLiteral("key"), value->key}, {QStringLiteral("modifiers"), value->modifiers}});
    return object;
}
}

ShortcutChord::ShortcutChord(const QKeyEvent &event) : key(keyText(event.key())), modifiers(bits(event.modifiers())) {}

ShortcutChord::ShortcutChord(QKeyCombination combination)
    : key(keyText(combination.key())), modifiers(bits(combination.keyboardModifiers()))
{
}

QKeyCombination ShortcutChord::combination() const
{
    return QKeyCombination(flags(modifiers), qtKey(key, modifiers & 8));
}

QString ShortcutChord::label() const
{
    return QKeySequence(combination()).toString(QKeySequence::NativeText);
}

std::unique_ptr<QKeyEvent> ShortcutChord::event(const QKeyEvent &like) const
{
    const Qt::Key typed = qtKey(key, modifiers & 8);
    const QString text = typed == Qt::Key_Space || key.size() != 1 || key.at(0).unicode() >= 0xf700 || key.at(0).unicode() < 0x20
        ? QString()
        : shiftedText(key, modifiers & 8);
    return std::make_unique<QKeyEvent>(like.type(), typed, flags(modifiers), text, like.isAutoRepeat(), like.count());
}

const std::vector<ShortcutDefinition> &ShortcutDefinition::all()
{
    static const std::vector<ShortcutDefinition> definitions = [] {
        // The menus' keys, as Illustrator's with Ctrl for Cmd.
        std::vector<ShortcutDefinition> result{
            entry("New", "n", 1, true), entry("Open", "o", 1, true), entry("Close", "w", 1, true), entry("Save", "s", 1, true),
            entry("Save As", "s", 9, true), entry("Place", "p", 9, true), entry("Share", "s", 11, true), entry("Export PNG", "e", 3, true), entry("Quit", "q", 1, true),
            entry("Undo", "z", 1, true), entry("Redo", "z", 9, true), entry("Cut", "x", 1, true), entry("Copy", "c", 1, true),
            entry("Paste", "v", 1, true), entry("Paste in Place", "v", 9, true), entry("Duplicate", "d", 3, true),
            entry("Paste in Front", "f", 1, true), entry("Paste in Back", "b", 1, true), entry("Transform Again", "d", 1, true),
            entry("Copy Properties", "c", 3, true), entry("Paste Properties", "v", 3, true),
            entry("Reselect", "6", 1, true), entry("Next Object Above", "]", 3, true), entry("Next Object Below", "[", 3, true),
            entry("Zoom to Selection", "0", 3, true),
            entry("Select All", "a", 1, true), entry("Deselect", "a", 9, true), entry("Move", "m", 9, true),
            entry("Bring to Front", "]", 9, true), entry("Bring Forward", "]", 1, true), entry("Send Backward", "[", 1, true),
            entry("Send to Back", "[", 9, true), entry("Group", "g", 1, true), entry("Ungroup", "g", 9, true),
            entry("Lock Selection", "2", 1, true), entry("Unlock All", "2", 3, true), entry("Hide Selection", "3", 1, true),
            entry("Show All", "3", 3, true), entry("Make Clipping Mask", "7", 1, true), entry("Release Clipping Mask", "7", 3, true),
            entry("Make Compound Path", "8", 1, true), entry("Release Compound Path", "8", 11, true),
            entry("Create Outlines", "o", 9, true), entry("Zoom In", "=", 1, true), entry("Zoom Out", "-", 1, true),
            entry("Fit Artboard in Window", "0", 1, true), entry("Actual Size", "1", 1, true), entry("Outline", "y", 1, true),
            entry("Show Grid", "'", 1, true), entry("Snap to Grid", "'", 9, true), entry("Command Palette", "k", 1, true),
            entry("Rulers", "r", 1, true), entry("Hide Guides", ";", 1, true), entry("Lock Guides", ";", 3, true),
            entry("Make Guides", "5", 1, true), entry("Release Guides", "5", 3, true), entry("Join", "j", 1, true), entry("Average", "j", 3, true),
            entry("Make Component", "k", 3, true), entry("Detach Instance", "b", 3, true)};
        // Illustrator's type keys: they work on selected type and while typing, and rest otherwise.
        const QString left(QChar(0xf702)), right(QChar(0xf703)), up(QChar(0xf700)), down(QChar(0xf701));
        for (const auto &[title, key, modifiers] : std::vector<std::tuple<const char *, QString, int>>{
                 {"Increase Font Size", QStringLiteral("."), 9}, {"Decrease Font Size", QStringLiteral(","), 9},
                 {"Tighten Tracking", left, 2}, {"Loosen Tracking", right, 2}, {"Tighten Tracking ×5", left, 3}, {"Loosen Tracking ×5", right, 3},
                 {"Decrease Leading", up, 2}, {"Increase Leading", down, 2}, {"Raise Baseline", up, 10}, {"Lower Baseline", down, 10},
                 {"Reset Tracking", QStringLiteral("q"), 3}})
            result.push_back({QString::fromUtf8(title), QStringLiteral("Type"), ShortcutChord(key, modifiers)});
        for (const auto &[tool, key, modifiers] : toolKeys)
            result.push_back(entry(::title(tool) + QStringLiteral(" tool"), QString::fromLatin1(key), modifiers, false));
        const std::vector<std::pair<const char *, QString>> keys{
            {"Swap fill and stroke", "x"}, {"Default fill and stroke", "d"}, {"Temporary Hand tool (hold)", " "},
            {"Apply / finish current operation", "\r"}, {"Cancel current operation", "\x1b"}};
        for (const auto &[title, key] : keys)
            result.push_back(entry(title, key, 0, false));
        for (const auto &[direction, key] : std::vector<std::pair<QString, QString>>{
                 {"Left", QString(QChar(0xf702))}, {"Right", QString(QChar(0xf703))}, {"Up", QString(QChar(0xf700))}, {"Down", QString(QChar(0xf701))}}) {
            // The step is the keyboard increment in Preferences.
            result.push_back(entry(QStringLiteral("Nudge %1").arg(direction), key, 0, false));
            result.push_back(entry(QStringLiteral("Nudge %1 ×10").arg(direction), key, 8, false));
        }
        return result;
    }();
    return definitions;
}

std::optional<Tool> ShortcutDefinition::tool(const ShortcutChord &chord)
{
    for (const auto &[tool, key, modifiers] : toolKeys) {
        if (chord == ShortcutChord(QString::fromLatin1(key), modifiers))
            return tool;
    }
    return std::nullopt;
}

ShortcutSettings &ShortcutSettings::shared()
{
    static ShortcutSettings settings;
    return settings;
}

ShortcutSettings::ShortcutSettings()
{
    reload();
}

void ShortcutSettings::reload()
{
    m_overrides.clear();
    const QByteArray data = QSettings().value(QLatin1String(storageKey)).toByteArray();
    if (!data.isEmpty()) {
        QHash<QString, ShortcutChord> saved;
        const QJsonObject object = QJsonDocument::fromJson(data).object();
        for (auto value = object.constBegin(); value != object.constEnd(); ++value) {
            const QJsonObject chord = value->toObject();
            // Nudges were named for fixed steps before the increment became a preference.
            QString id = value.key();
            if (id.startsWith(QLatin1String("Canvas & Layers:Nudge ")))
                id.replace(QLatin1String(" 10 pt"), QStringLiteral(" ×10")).remove(QLatin1String(" 1 pt"));
            saved.insert(id,ShortcutChord(chord.value(QLatin1String("key")).toString(), chord.value(QLatin1String("modifiers")).toInt(-1)));
        }
        // Swift keeps stored overrides only while they hold together.
        if (const std::optional<QString> wrong = problem(saved))
            qCWarning(lcApp).noquote() << "stored keyboard shortcuts ignored:" << *wrong;
        else
            m_overrides = saved;
    }
    emit changed();
}

ShortcutChord ShortcutSettings::chord(const ShortcutDefinition &definition) const
{
    return m_overrides.value(definition.id(), definition.original);
}

QKeySequence ShortcutSettings::menu(const QKeySequence &original) const
{
    // An empty sequence reads as no key, which nothing matches.
    const ShortcutChord typed(original[0]);
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.isMenu() && definition.original == typed)
            return QKeySequence(chord(definition).combination());
    }
    return original;
}

ShortcutChord ShortcutSettings::native(const ShortcutChord &original) const
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (!definition.isMenu() && definition.original == original)
            return chord(definition);
    }
    return original;
}

bool ShortcutSettings::save(const QHash<QString, ShortcutChord> &values)
{
    if (problem(values))
        return false;
    m_overrides = values;
    QSettings().setValue(QLatin1String(storageKey), QJsonDocument(encoded(values)).toJson(QJsonDocument::Compact));
    emit changed();
    return true;
}

std::optional<QString> ShortcutSettings::problem(const QHash<QString, ShortcutChord> &values)
{
    QHash<QString, QString> assigned;
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        const ShortcutChord chord = values.value(definition.id(), definition.original);
        if (chord.key.size() != 1 || chord.modifiers < 0 || chord.modifiers > 15)
            return QStringLiteral("Choose a single key with optional modifiers.");
        if (definition.group == QLatin1String("Text Editing") && !(chord.modifiers & 7))
            return QStringLiteral("Text-editing shortcuts need Ctrl, Alt or Meta so they do not replace normal typing.");
        const QString name = QString::number(chord.modifiers) + QLatin1Char(':') + chord.key;
        if (assigned.contains(name))
            return QStringLiteral("%1 is assigned to both %2 and %3.").arg(chord.label(), assigned.value(name), definition.title);
        assigned.insert(name, definition.title);
    }
    return std::nullopt;
}

std::unique_ptr<QKeyEvent> ShortcutSettings::canvasEvent(const QKeyEvent &event) const
{
    std::unique_ptr<QKeyEvent> same(event.clone());
    if (m_overrides.isEmpty())
        return same;
    const ShortcutChord input(event);
    const std::vector<ShortcutDefinition> &all = ShortcutDefinition::all();
    for (const ShortcutDefinition &definition : all) {
        if (definition.group == QLatin1String("Canvas & Layers") && chord(definition) == input)
            return definition.original == input ? std::move(same) : definition.original.event(event);
    }
    for (const ShortcutDefinition &definition : all) {
        if (definition.group != QLatin1String("Text Editing") && definition.original == input && chord(definition) != input)
            return nullptr;
    }
    // Tool letters also take Shift, unless Shift has its own.
    if (input.modifiers == 8) {
        const ShortcutChord plain(input.key);
        // Shift-M is Shape Builder's own, so a remapped M doesn't carry Shift along.
        auto shiftTaken = [](const QString &key) { return ShortcutDefinition::tool(ShortcutChord(key, 8)).has_value(); };
        for (const ShortcutDefinition &definition : all) {
            if (!definition.isMenu() && definition.original.modifiers == 0 && chord(definition) == plain && !shiftTaken(definition.original.key))
                return ShortcutChord(definition.original.key, 8).event(event);
        }
        for (const ShortcutDefinition &definition : all) {
            if (!definition.isMenu() && definition.original == plain && chord(definition) != plain)
                return nullptr;
        }
    }
    return same;
}

std::unique_ptr<QKeyEvent> ShortcutSettings::textEvent(const QKeyEvent &event) const
{
    std::unique_ptr<QKeyEvent> same(event.clone());
    if (m_overrides.isEmpty())
        return same;
    std::vector<ShortcutDefinition> definitions;
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.group == QLatin1String("Text Editing") || definition.original == ShortcutChord(QStringLiteral("\x1b")))
            definitions.push_back(definition);
    }
    const ShortcutChord input(event);
    for (const ShortcutDefinition &definition : definitions) {
        if (chord(definition) == input)
            return definition.original == input ? std::move(same) : definition.original.event(event);
    }
    for (const ShortcutDefinition &definition : definitions) {
        if (definition.original == input && chord(definition) != input)
            return nullptr;
    }
    return same;
}

NativeShortcut::NativeShortcut(const ShortcutChord &original, QWidget &scope, QAbstractButton &target)
    : QObject(&scope), m_original(original), m_scope(scope), m_target(&target), m_shortcut(new QShortcut(&scope))
{
    m_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    // A disabled button ignores click() itself.
    connect(m_shortcut, &QShortcut::activated, this, [this] {
        if (m_target && m_target->isVisible())
            m_target->click();
    });
    scope.installEventFilter(this);
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &NativeShortcut::synchronize);
    synchronize();
}

void NativeShortcut::bind(QWidget &scope, QAbstractButton *apply, QAbstractButton *cancel)
{
    if (apply)
        new NativeShortcut(ShortcutChord(QStringLiteral("\r")), scope, *apply);
    if (cancel)
        new NativeShortcut(ShortcutChord(QStringLiteral("\x1b")), scope, *cancel);
}

void NativeShortcut::synchronize()
{
    const ShortcutChord chord = ShortcutSettings::shared().native(m_original);
    m_shortcut->setKey(QKeySequence(chord.combination()));
    // Its own plain Return or Escape already reaches the button.
    const bool plain = !m_original.modifiers && (m_original.key == QLatin1String("\r") || m_original.key == QLatin1String("\x1b"));
    m_shortcut->setEnabled(!(plain && chord == m_original));
}

bool NativeShortcut::eventFilter(QObject *watched, QEvent *event)
{
    // The original key, moved elsewhere, no longer acts.
    if (watched == &m_scope && event->type() == QEvent::KeyPress) {
        const ShortcutChord typed(*static_cast<QKeyEvent *>(event));
        if (typed == m_original && ShortcutSettings::shared().native(m_original) != m_original)
            return true;
    }
    return QObject::eventFilter(watched, event);
}
