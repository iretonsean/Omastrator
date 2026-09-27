#pragma once
#include "Anywhere/Inspect.h"
#include "Document/EditorSession.h"
#include <QObject>
#include <QPointF>
#include <map>
#include <optional>
#include <vector>

class QTimer;

// The art drawn on top of surfaces (docs/ANYWHERE.md): one document, kept by
// the background Omastrator in $XDG_DATA_HOME/omastrator/overlays.omai, with
// one layer per surface named by its key. Art is stored relative to the
// surface's origin (the window's corner, or the page's, scrolled), so it
// moves with it. Every change is one named undo step of its session.
class OverlayStore : public QObject {
    Q_OBJECT
public:
    explicit OverlayStore(QObject *parent = nullptr);

    static QString defaultPath();
    // Reads the file, or starts empty when there is none. Returns why it failed, or empty.
    QString load(const QString &path = defaultPath());
    QString path() const { return m_path; }
    EditorSession &session() { return m_session; }
    const EditorSession &session() const { return m_session; }

    // What the overlay's tools draw: `points` are on screen; `text` is for text and notes.
    struct Stroke {
        QString tool;
        std::vector<QPointF> points;
        QString text;
    };
    // Draws on `surface`'s layer, made on first use, as one undo step ("Draw Rectangle on foot").
    // Returns the new object's id, or null with `error` set.
    QUuid draw(const Surface &surface, const Stroke &stroke, QString *error);

    // The surfaces with art, by key.
    QStringList surfaces() const;
    std::optional<QUuid> layer(const QString &key) const;
    // The surface's layer, made (one undo step) if it has none yet.
    QUuid ensureLayer(const Surface &surface);
    // The objects directly on a surface's layer, bottom to top.
    std::vector<QUuid> art(const QString &key) const;
    // Selects a surface's art (all of it, or `ids`), so the bar's actions and Ask act on it.
    bool selectArt(const QString &key, const std::vector<QUuid> &ids = {});
    // The surface whose art is selected, if any.
    QString selectedSurface() const;
    // Lifted art (in the surface's own coordinates) on the surface's layer, as one undo step named `step`;
    // returns the placed copy of `root`, selected, or null with `error` set.
    QUuid place(const Surface &surface, const VectorDocument &art, const QUuid &root, const QString &step, QString *error);
    // Takes a surface's art away in one undo step.
    void clear(const QString &key);

    // A surface's art (and an agent's proposal on it) as a transparent PNG at `scale`, with where it sits
    // relative to the surface's origin. Rendered again only after it changes.
    struct Picture {
        QString path;
        QRectF bounds;
        int version = 0;
    };
    std::optional<Picture> picture(const QString &key, double scale = 1);
    // The surface's art as a document of its own, placed at its origin: what the Desk and a new file get.
    VectorDocument extract(const QString &key, const std::vector<QUuid> &ids = {}) const;

    // Saves now; changes also save themselves shortly after. Returns why it failed, or empty.
    QString save();

    // The accent the overlay draws in: the Omarchy theme's, else a warm orange.
    static QColor ink();

signals:
    // The art changed: redraw the overlays.
    void changed();

private:

    EditorSession m_session;
    QString m_path;
    QTimer *m_saveTimer = nullptr;
    struct Rendered {
        std::vector<VectorObject> objects;
        double scale = 0;
        Picture picture;
    };
    std::map<QString, Rendered> m_rendered;
    int m_version = 0;
};
