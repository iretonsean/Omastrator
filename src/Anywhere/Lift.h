#pragma once
#include "Anywhere/DesktopSource.h"
#include "Anywhere/Inspect.h"
#include "Document/VectorDocument.h"
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class LiveSession;

// Lift into vectors (docs/ANYWHERE.md): a surface's UI becomes editable shapes
// and text, in the surface's own coordinates, so it lands exactly over the
// original. Pages are read from the DOM; other apps from the accessibility
// tree over a screenshot, or traced when they have no tree.
namespace Lift {
struct Limits {
    int maxElements = 1500;
    int maxCharacters = 40'000;
    int maxImages = 80;
    int maxNodes = 800;
};

// What a lift made: one layer holding one group, the lifted thing.
struct Result {
    VectorDocument art;
    QUuid root;
    QString name;
    // "dom", "accessibility" or "trace".
    QString method;
    // What was left out, one plain sentence each.
    QStringList notes;
    int objects = 0;
};

// The page's side: `element` lifts the element whose box is `rect`; otherwise everything in `rect`
// (the whole viewport when empty). Rectangles are viewport coordinates.
QString webScript(bool element, const QRectF &rect, const Limits &limits = {});

// Pixels the DOM answer needs, fetched by the caller: images by address, screenshots by node index.
struct Resources {
    QHash<QString, QImage> images;
    QHash<QString, QByteArray> svgs;
    QHash<int, QImage> shots;
};
QStringList imageAddresses(const QJsonObject &answer, const Limits &limits = {});
// Page-coordinate boxes to screenshot (canvas, video, frames, and images that couldn't be fetched).
std::vector<std::pair<int, QRectF>> shotBoxes(const QJsonObject &answer);
// The page's answer as vectors in page coordinates. `progress` gets (done, total) and returns false to stop.
std::optional<Result> fromDom(const QJsonObject &answer, const Resources &resources, QString *error,
                              const std::function<bool(int, int)> &progress = {});

// A Python program that prints an app's accessibility tree as JSON: `pid maxNodes`, in window coordinates.
QString accessibleTreeScript();
// $OMASTRATOR_ATSPI_TREE, else python3 running accessibleTreeScript(); nullopt with `error` when there's no tree.
std::optional<QJsonObject> accessibleTree(qint64 pid, int maxNodes, QString *error = nullptr);
// The tree over the window's screenshot: rectangles in each widget's colour, its text, and a crop where it isn't flat.
// `region` (window coordinates) limits it; empty is the whole window.
std::optional<Result> fromAccessible(const QJsonObject &tree, const QImage &screenshot, const QSize &windowSize, const QRect &region,
                                     const QString &label, QString *error, const std::function<bool(int, int)> &progress = {});
// No tree: the screenshot traced to filled paths, placed over `region` (surface coordinates).
std::optional<Result> fromTrace(const QImage &screenshot, const QRectF &region, const QString &label, QString *error);
}

// One lift in the background, with progress, cancellable. Nothing lands until it finishes.
class LiftJob : public QObject {
    Q_OBJECT
public:
    // A page in Omastrator's browser: the element whose viewport box is `rect`, or the region `rect`.
    static std::unique_ptr<LiftJob> web(LiveSession &live, bool element, const QRectF &rect, const QString &label, const Lift::Limits &limits = {});
    // Another app's window: its accessibility tree, else a trace. `region` is in window coordinates; empty is all of it.
    static std::unique_ptr<LiftJob> screen(DesktopSource &source, const Hyprland::Window &window, const QRect &region, const QString &label,
                                           const Lift::Limits &limits = {});
    // A region of the screen with nothing to read: traced. `rect` is on screen; `origin` is the surface's.
    static std::unique_ptr<LiftJob> trace(DesktopSource &source, const QRect &rect, QPointF origin, const QString &label);
    ~LiftJob() override;

    void start();
    void cancel();
    bool isRunning() const { return m_running; }
    bool wasCancelled() const { return m_cancelled; }
    QString stage() const { return m_stage; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    QString label() const { return m_label; }
    const std::optional<Lift::Result> &result() const { return m_result; }
    QString error() const { return m_error; }
    QJsonObject status() const;

signals:
    void progressed();
    // Finished, failed or cancelled: result() or error() says which.
    void finished();

private:
    LiftJob() = default;
    void setStage(const QString &stage, int done = 0, int total = 0);
    void finish(const QString &error = QString());
    void webAnswered(const QJsonObject &answer);
    void fetchNext();
    void buildWeb();
    void screenRead();

    enum class Kind { web, screen, trace };
    Kind m_kind = Kind::web;
    QPointer<LiveSession> m_live;
    DesktopSource *m_source = nullptr;
    Hyprland::Window m_window;
    QRect m_region;
    QPointF m_origin;
    bool m_element = false;
    QRectF m_rect;
    QString m_label;
    Lift::Limits m_limits;
    bool m_running = false;
    bool m_cancelled = false;
    QString m_stage;
    int m_done = 0;
    int m_total = 0;
    QString m_error;
    std::optional<Lift::Result> m_result;
    QJsonObject m_answer;
    Lift::Resources m_resources;
    QStringList m_pendingImages;
    std::vector<std::pair<int, QRectF>> m_pendingShots;
    QString m_frameId;
    struct ScreenRead {
        std::optional<QJsonObject> tree;
        QString treeError;
        QImage shot;
        QString shotError;
    };
    QFutureWatcher<ScreenRead> m_watcher;
};
