#pragma once
#include "Document/EditorSession.h"
#include <QDateTime>
#include <QImage>
#include <QString>
#include <utility>
#include <vector>

// The Desk (docs/ANYWHERE.md): one global canvas, $XDG_DATA_HOME/omastrator/desk.omai.
// Whatever is sent from a surface lands as a frame labelled with its source
// and time, laid out after the frames already there; the artboard grows to hold them.
namespace Desk {
QString defaultPath();
// An empty Desk: a wide, pale artboard with one layer.
VectorDocument blank();

struct Frame {
    // "foot", "example.com/pricing": the surface's label.
    QString source;
    QDateTime time = QDateTime::currentDateTime();
    // What was under the art, if captured; drawn at `size`.
    QImage screenshot;
    // The art, in the surface's own coordinates (OverlayStore::extract).
    VectorDocument art;
    // The frame's size in points; empty: the screenshot's, else the art's.
    QSizeF size;
};
// "foot · 14:05"
QString label(const QString &source, const QDateTime &time);
// Adds `frame` as one undo step, "Send to Desk". Returns the frame group's id, or null with `error` set.
QUuid addFrame(EditorSession &desk, const Frame &frame, QString *error = nullptr);
// The frames on the Desk: each group's id and label, in the order they were added.
std::vector<std::pair<QUuid, QString>> frames(const VectorDocument &desk);
}
