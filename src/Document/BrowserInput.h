#pragma once
#include <QJsonObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <Qt>
#include <optional>

// What the Browse tool sends a page (docs/BROWSER-VIEW.md, section 5), as Input.dispatch* parameters. Pure: the canvas
// decides where and when, this decides what.
namespace BrowserInput {

// A document point in the page's CSS pixels: the page fills `box` (the frame, or its preview box) one to one.
QPointF cssPoint(QPointF documentPoint, const QRectF &box);

// Input.dispatchMouseEvent's modifiers: Alt 1, Ctrl 2, Meta 4, Shift 8.
int cdpModifiers(Qt::KeyboardModifiers modifiers);

// `type` is mousePressed, mouseReleased or mouseMoved. `button` is left, middle or right, or none.
// `buttons` is the mask of buttons held (left 1, right 2, middle 4).
QJsonObject mouseParams(const QString &type, QPointF css, Qt::MouseButton button, Qt::MouseButtons buttons, int clickCount,
                        Qt::KeyboardModifiers modifiers);

// Qt's angle delta is 1/8 degree, and Chromium reads one unit as one pixel; a pixel delta, when there is one, wins.
// Down and right are positive on the page and negative in Qt.
QJsonObject wheelParams(QPointF css, QPoint angleDelta, QPoint pixelDelta, Qt::KeyboardModifiers modifiers);

struct Key {
    // The DOM's key and code, and the Windows virtual key code Chromium wants.
    QString key;
    QString code;
    int windowsVirtualKeyCode = 0;
};

// The key a Qt key event stands for, given its text; none for a key the page has no name for.
std::optional<Key> keyFor(int qtKey, const QString &text, Qt::KeyboardModifiers modifiers);

// Esc leaves Browse and Ctrl+K opens the palette; the page never sees either.
bool reserved(int qtKey, Qt::KeyboardModifiers modifiers);

// Input.dispatchKeyEvent's parameters for a press or a release, or none for an unknown key. A printable key with no
// Ctrl, Alt or Meta is a keyDown that carries its text; the rest are rawKeyDown, with the editing command a Ctrl shortcut
// stands for (headless Chromium doesn't map Ctrl+A to select all on its own).
std::optional<QJsonObject> keyParams(bool down, int qtKey, const QString &text, Qt::KeyboardModifiers modifiers, bool autoRepeat);

}
