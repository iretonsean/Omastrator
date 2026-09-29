#include "Document/BrowserInput.h"
#include <QJsonArray>
#include <QList>
#include <utility>

namespace BrowserInput {

namespace {
struct Named {
    int qtKey;
    const char *key;
    const char *code;
    int vk;
};

const Named namedKeys[] = {
    {Qt::Key_Return, "Enter", "Enter", 13},
    {Qt::Key_Enter, "Enter", "NumpadEnter", 13},
    {Qt::Key_Backspace, "Backspace", "Backspace", 8},
    {Qt::Key_Tab, "Tab", "Tab", 9},
    {Qt::Key_Backtab, "Tab", "Tab", 9},
    {Qt::Key_Escape, "Escape", "Escape", 27},
    {Qt::Key_Delete, "Delete", "Delete", 46},
    {Qt::Key_Insert, "Insert", "Insert", 45},
    {Qt::Key_Left, "ArrowLeft", "ArrowLeft", 37},
    {Qt::Key_Up, "ArrowUp", "ArrowUp", 38},
    {Qt::Key_Right, "ArrowRight", "ArrowRight", 39},
    {Qt::Key_Down, "ArrowDown", "ArrowDown", 40},
    {Qt::Key_Home, "Home", "Home", 36},
    {Qt::Key_End, "End", "End", 35},
    {Qt::Key_PageUp, "PageUp", "PageUp", 33},
    {Qt::Key_PageDown, "PageDown", "PageDown", 34},
    {Qt::Key_Space, " ", "Space", 32},
    {Qt::Key_Shift, "Shift", "ShiftLeft", 16},
    {Qt::Key_Control, "Control", "ControlLeft", 17},
    {Qt::Key_Alt, "Alt", "AltLeft", 18},
    {Qt::Key_Meta, "Meta", "MetaLeft", 91},
    {Qt::Key_CapsLock, "CapsLock", "CapsLock", 20},
};

// Punctuation as the US layout puts it: the key both characters share, then the code and virtual key.
struct Punct {
    int base;
    int shifted;
    const char *code;
    int vk;
};

const Punct punctuation[] = {
    {Qt::Key_Semicolon, Qt::Key_Colon, "Semicolon", 186},
    {Qt::Key_Equal, Qt::Key_Plus, "Equal", 187},
    {Qt::Key_Comma, Qt::Key_Less, "Comma", 188},
    {Qt::Key_Minus, Qt::Key_Underscore, "Minus", 189},
    {Qt::Key_Period, Qt::Key_Greater, "Period", 190},
    {Qt::Key_Slash, Qt::Key_Question, "Slash", 191},
    {Qt::Key_QuoteLeft, Qt::Key_AsciiTilde, "Backquote", 192},
    {Qt::Key_BracketLeft, Qt::Key_BraceLeft, "BracketLeft", 219},
    {Qt::Key_Backslash, Qt::Key_Bar, "Backslash", 220},
    {Qt::Key_BracketRight, Qt::Key_BraceRight, "BracketRight", 221},
    {Qt::Key_Apostrophe, Qt::Key_QuoteDbl, "Quote", 222},
};

// Shift+digit, on the US layout: the digit that made it.
const std::pair<int, int> shiftedDigits[] = {
    {Qt::Key_Exclam, 1},     {Qt::Key_At, 2},        {Qt::Key_NumberSign, 3}, {Qt::Key_Dollar, 4},     {Qt::Key_Percent, 5},
    {Qt::Key_AsciiCircum, 6}, {Qt::Key_Ampersand, 7}, {Qt::Key_Asterisk, 8},   {Qt::Key_ParenLeft, 9}, {Qt::Key_ParenRight, 0},
};

Key digit(int digitValue, const QString &text)
{
    return {text.size() == 1 ? text : QString::number(digitValue), QStringLiteral("Digit%1").arg(digitValue), 48 + digitValue};
}

bool printable(const QString &text)
{
    return text.size() == 1 && text.at(0).isPrint();
}

// The command a Ctrl (or Cmd) shortcut asks the page's editor for.
QString commandFor(int qtKey, Qt::KeyboardModifiers modifiers)
{
    if (!(modifiers & (Qt::ControlModifier | Qt::MetaModifier)) || (modifiers & Qt::AltModifier))
        return {};
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    switch (qtKey) {
    case Qt::Key_A:
        return QStringLiteral("selectAll");
    case Qt::Key_C:
        return QStringLiteral("copy");
    case Qt::Key_X:
        return QStringLiteral("cut");
    case Qt::Key_V:
        return QStringLiteral("paste");
    case Qt::Key_Z:
        return shift ? QStringLiteral("redo") : QStringLiteral("undo");
    case Qt::Key_Y:
        return QStringLiteral("redo");
    default:
        return {};
    }
}
}

QPointF cssPoint(QPointF documentPoint, const QRectF &box)
{
    return documentPoint - box.topLeft();
}

int cdpModifiers(Qt::KeyboardModifiers modifiers)
{
    return (modifiers.testFlag(Qt::AltModifier) ? 1 : 0) | (modifiers.testFlag(Qt::ControlModifier) ? 2 : 0)
        | (modifiers.testFlag(Qt::MetaModifier) ? 4 : 0) | (modifiers.testFlag(Qt::ShiftModifier) ? 8 : 0);
}

QJsonObject mouseParams(const QString &type, QPointF css, Qt::MouseButton button, Qt::MouseButtons buttons, int clickCount,
                        Qt::KeyboardModifiers modifiers)
{
    QString name = QStringLiteral("none");
    if (button == Qt::LeftButton)
        name = QStringLiteral("left");
    else if (button == Qt::MiddleButton)
        name = QStringLiteral("middle");
    else if (button == Qt::RightButton)
        name = QStringLiteral("right");
    const int mask = (buttons.testFlag(Qt::LeftButton) ? 1 : 0) | (buttons.testFlag(Qt::RightButton) ? 2 : 0)
        | (buttons.testFlag(Qt::MiddleButton) ? 4 : 0);
    return {{QStringLiteral("type"), type},
            {QStringLiteral("x"), css.x()},
            {QStringLiteral("y"), css.y()},
            {QStringLiteral("button"), name},
            {QStringLiteral("buttons"), mask},
            {QStringLiteral("clickCount"), clickCount},
            {QStringLiteral("modifiers"), cdpModifiers(modifiers)}};
}

QJsonObject wheelParams(QPointF css, QPoint angleDelta, QPoint pixelDelta, Qt::KeyboardModifiers modifiers)
{
    const QPoint delta = pixelDelta.isNull() ? angleDelta : pixelDelta;
    return {{QStringLiteral("type"), QStringLiteral("mouseWheel")},
            {QStringLiteral("x"), css.x()},
            {QStringLiteral("y"), css.y()},
            {QStringLiteral("deltaX"), double(-delta.x())},
            {QStringLiteral("deltaY"), double(-delta.y())},
            {QStringLiteral("modifiers"), cdpModifiers(modifiers)}};
}

std::optional<Key> keyFor(int qtKey, const QString &text, Qt::KeyboardModifiers modifiers)
{
    for (const Named &named : namedKeys) {
        if (named.qtKey == qtKey)
            return Key{QString::fromLatin1(named.key), QString::fromLatin1(named.code), named.vk};
    }
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        const QChar letter(qtKey);
        // With Ctrl held the event's text is a control character, so the key is worked out from the letter.
        const QString shown = printable(text) ? text : (shift ? QString(letter) : QString(letter.toLower()));
        return Key{shown, QStringLiteral("Key%1").arg(letter), qtKey};
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return digit(qtKey - Qt::Key_0, printable(text) ? text : QString());
    for (const auto &[shifted, digitValue] : shiftedDigits) {
        if (qtKey == shifted)
            return digit(digitValue, printable(text) ? text : QString());
    }
    for (const Punct &punct : punctuation) {
        if (qtKey == punct.base || qtKey == punct.shifted) {
            const QChar plain = QChar(qtKey);
            const QString shown = printable(text) ? text : QString(plain);
            return Key{shown, QString::fromLatin1(punct.code), punct.vk};
        }
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        const int number = qtKey - Qt::Key_F1 + 1;
        return Key{QStringLiteral("F%1").arg(number), QStringLiteral("F%1").arg(number), 111 + number};
    }
    // A letter another layout makes directly (é, ü, ñ, Cyrillic, Greek) has no DOM code here, but its text is the key.
    if (printable(text) && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        return Key{text, QString(), 0};
    return std::nullopt;
}

bool reserved(int qtKey, Qt::KeyboardModifiers modifiers)
{
    if (qtKey == Qt::Key_Escape)
        return true;
    return qtKey == Qt::Key_K && modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::AltModifier);
}

std::optional<QJsonObject> keyParams(bool down, int qtKey, const QString &text, Qt::KeyboardModifiers modifiers, bool autoRepeat)
{
    const std::optional<Key> key = keyFor(qtKey, text, modifiers);
    if (!key)
        return std::nullopt;
    const bool typed = printable(text) || qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter;
    const bool shortcut = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    QJsonObject params{{QStringLiteral("key"), key->key},
                       {QStringLiteral("code"), key->code},
                       {QStringLiteral("windowsVirtualKeyCode"), key->windowsVirtualKeyCode},
                       {QStringLiteral("nativeVirtualKeyCode"), key->windowsVirtualKeyCode},
                       {QStringLiteral("modifiers"), cdpModifiers(modifiers)}};
    if (!down) {
        params[QStringLiteral("type")] = QStringLiteral("keyUp");
        return params;
    }
    if (typed && !shortcut) {
        const QString sent = (qtKey == Qt::Key_Return || qtKey == Qt::Key_Enter) ? QStringLiteral("\r") : text;
        params[QStringLiteral("type")] = QStringLiteral("keyDown");
        params[QStringLiteral("text")] = sent;
        params[QStringLiteral("unmodifiedText")] = sent;
    } else {
        params[QStringLiteral("type")] = QStringLiteral("rawKeyDown");
        if (const QString command = commandFor(qtKey, modifiers); !command.isEmpty())
            params[QStringLiteral("commands")] = QJsonArray{command};
    }
    if (autoRepeat)
        params[QStringLiteral("autoRepeat")] = true;
    return params;
}

}
