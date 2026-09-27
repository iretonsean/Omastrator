#include "Anywhere/Lift.h"
#include "Live/LiveSession.h"
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

// A lift in the background: the page's answer and its pictures come over the
// DevTools Protocol one reply at a time; an app's tree and screenshot are read
// on a worker thread. Cancel drops whatever arrives after.

std::unique_ptr<LiftJob> LiftJob::web(LiveSession &live, bool element, const QRectF &rect, const QString &label, const Lift::Limits &limits)
{
    std::unique_ptr<LiftJob> job(new LiftJob);
    job->m_kind = Kind::web;
    job->m_live = &live;
    job->m_element = element;
    job->m_rect = rect;
    job->m_label = label;
    job->m_limits = limits;
    return job;
}

std::unique_ptr<LiftJob> LiftJob::screen(DesktopSource &source, const Hyprland::Window &window, const QRect &region, const QString &label,
                                         const Lift::Limits &limits)
{
    std::unique_ptr<LiftJob> job(new LiftJob);
    job->m_kind = Kind::screen;
    job->m_source = &source;
    job->m_window = window;
    job->m_region = region;
    job->m_label = label;
    job->m_limits = limits;
    return job;
}

std::unique_ptr<LiftJob> LiftJob::trace(DesktopSource &source, const QRect &rect, QPointF origin, const QString &label)
{
    std::unique_ptr<LiftJob> job(new LiftJob);
    job->m_kind = Kind::trace;
    job->m_source = &source;
    job->m_region = rect;
    job->m_origin = origin;
    job->m_label = label;
    return job;
}

LiftJob::~LiftJob()
{
    m_cancelled = true;
    disconnect(&m_watcher, nullptr, this, nullptr);
    // A worker still reading holds only copies; its answer is dropped.
    m_watcher.waitForFinished();
}

QJsonObject LiftJob::status() const
{
    QJsonObject status{{"label", m_label}, {"stage", m_stage}, {"running", m_running}};
    if (m_total > 0) {
        status["done"] = m_done;
        status["total"] = m_total;
    }
    return status;
}

void LiftJob::setStage(const QString &stage, int done, int total)
{
    m_stage = stage;
    m_done = done;
    m_total = total;
    emit progressed();
}

void LiftJob::finish(const QString &error)
{
    if (!m_running)
        return;
    m_running = false;
    m_error = error;
    if (!error.isEmpty())
        m_result.reset();
    emit finished();
}

void LiftJob::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    finish(QStringLiteral("Cancelled."));
}

void LiftJob::start()
{
    if (m_running)
        return;
    m_running = true;
    m_cancelled = false;
    if (m_kind == Kind::web) {
        if (!m_live || m_live->state() != LiveSession::State::running || m_live->pageSession().isEmpty())
            return QTimer::singleShot(0, this, [this] { finish(QStringLiteral("The page isn't open in Omastrator's browser any more.")); });
        setStage(QStringLiteral("Reading the page…"));
        QPointer<LiftJob> self(this);
        m_live->browser().cdp().call(QStringLiteral("Runtime.evaluate"),
                                     {{"expression", Lift::webScript(m_element, m_rect, m_limits)}, {"returnByValue", true}},
                                     m_live->pageSession(), [self](const QJsonObject &result, const QString &error) {
                                         if (!self || !self->m_running)
                                             return;
                                         if (!error.isEmpty())
                                             return self->finish(QStringLiteral("The page couldn't be read: %1").arg(error));
                                         if (result.contains("exceptionDetails"))
                                             return self->finish(QStringLiteral("The page couldn't be read: %1")
                                                                     .arg(result["exceptionDetails"].toObject()["exception"].toObject()["description"].toString()));
                                         self->webAnswered(result["result"].toObject()["value"].toObject());
                                     });
        return;
    }
    setStage(m_kind == Kind::screen ? QStringLiteral("Reading the app…") : QStringLiteral("Capturing the screen…"));
    DesktopSource *source = m_source;
    const Hyprland::Window window = m_window;
    const QRect grabRect = m_kind == Kind::screen ? window.rect : m_region;
    const bool readTree = m_kind == Kind::screen;
    const int maxNodes = m_limits.maxNodes;
    connect(&m_watcher, &QFutureWatcher<ScreenRead>::finished, this, &LiftJob::screenRead, Qt::UniqueConnection);
    m_watcher.setFuture(QtConcurrent::run([source, window, grabRect, readTree, maxNodes] {
        ScreenRead read;
        if (readTree)
            read.tree = source->accessibleTree(window, maxNodes, &read.treeError);
        read.shot = source->grab(grabRect, &read.shotError);
        return read;
    }));
}

void LiftJob::webAnswered(const QJsonObject &answer)
{
    if (answer.contains("error") || answer.isEmpty())
        return finish(answer["error"].toString(QStringLiteral("The page gave no answer.")));
    m_answer = answer;
    m_pendingImages = Lift::imageAddresses(answer, m_limits);
    m_pendingShots = Lift::shotBoxes(answer);
    m_total = int(m_pendingImages.size() + m_pendingShots.size());
    if (m_total == 0)
        return buildWeb();
    setStage(QStringLiteral("Fetching pictures…"), 0, m_total);
    QPointer<LiftJob> self(this);
    m_live->browser().cdp().call(QStringLiteral("Page.getFrameTree"), {}, m_live->pageSession(), [self](const QJsonObject &result, const QString &) {
        if (!self || !self->m_running)
            return;
        self->m_frameId = result["frameTree"].toObject()["frame"].toObject()["id"].toString();
        self->fetchNext();
    });
}

void LiftJob::fetchNext()
{
    if (!m_running)
        return;
    if (!m_live || m_live->pageSession().isEmpty())
        return finish(QStringLiteral("The page closed while it was being lifted."));
    QPointer<LiftJob> self(this);
    if (!m_pendingImages.isEmpty()) {
        const QString url = m_pendingImages.takeFirst();
        setStage(m_stage, m_done + 1, m_total);
        // Data addresses carry their own bytes.
        if (url.startsWith(QLatin1String("data:"))) {
            const QString header = url.section(QLatin1Char(','), 0, 0);
            const QByteArray payload = url.section(QLatin1Char(','), 1).toUtf8();
            const QByteArray bytes = header.endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(payload) : QByteArray::fromPercentEncoding(payload);
            if (header.contains(QLatin1String("svg")))
                m_resources.svgs.insert(url, bytes);
            else
                m_resources.images.insert(url, QImage::fromData(bytes));
            return QTimer::singleShot(0, this, &LiftJob::fetchNext);
        }
        m_live->browser().cdp().call(QStringLiteral("Page.getResourceContent"), {{"frameId", m_frameId}, {"url", url}}, m_live->pageSession(),
                                     [self, url](const QJsonObject &result, const QString &error) {
                                         if (!self || !self->m_running)
                                             return;
                                         if (error.isEmpty()) {
                                             const QByteArray bytes = result["base64Encoded"].toBool()
                                                                          ? QByteArray::fromBase64(result["content"].toString().toLatin1())
                                                                          : result["content"].toString().toUtf8();
                                             if (bytes.trimmed().startsWith('<') || QUrl(url).path().endsWith(QLatin1String(".svg")))
                                                 self->m_resources.svgs.insert(url, bytes);
                                             else if (const QImage image = QImage::fromData(bytes); !image.isNull())
                                                 self->m_resources.images.insert(url, image);
                                         }
                                         if (!self->m_resources.images.contains(url) && !self->m_resources.svgs.contains(url)) {
                                             // Not in the page's cache: a screenshot of where it shows stands in.
                                             const QJsonArray nodes = self->m_answer["nodes"].toArray();
                                             for (int index = 0; index < nodes.size(); ++index) {
                                                 const QJsonObject img = nodes.at(index).toObject()["img"].toObject();
                                                 if (img["url"].toString() == url) {
                                                     const QJsonArray r = nodes.at(index).toObject()["r"].toArray();
                                                     self->m_pendingShots.emplace_back(index, QRectF(r.at(0).toDouble(), r.at(1).toDouble(),
                                                                                                     r.at(2).toDouble(), r.at(3).toDouble()));
                                                     ++self->m_total;
                                                 }
                                             }
                                         }
                                         self->fetchNext();
                                     });
        return;
    }
    if (!m_pendingShots.empty()) {
        const auto [index, rect] = m_pendingShots.front();
        m_pendingShots.erase(m_pendingShots.begin());
        setStage(m_stage, m_done + 1, m_total);
        const double scale = std::clamp(m_answer["dpr"].toDouble(1), 1.0, 3.0);
        const QJsonObject clip{{"x", rect.x()}, {"y", rect.y()}, {"width", std::max(1.0, rect.width())}, {"height", std::max(1.0, rect.height())},
                               {"scale", scale}};
        m_live->browser().cdp().call(QStringLiteral("Page.captureScreenshot"), {{"format", "png"}, {"clip", clip}, {"captureBeyondViewport", false}},
                                     m_live->pageSession(), [self, index](const QJsonObject &result, const QString &error) {
                                         if (!self || !self->m_running)
                                             return;
                                         if (error.isEmpty())
                                             self->m_resources.shots.insert(index, QImage::fromData(QByteArray::fromBase64(result["data"].toString().toLatin1()), "PNG"));
                                         self->fetchNext();
                                     });
        return;
    }
    buildWeb();
}

void LiftJob::buildWeb()
{
    const int count = int(m_answer["nodes"].toArray().size());
    setStage(QStringLiteral("Building shapes…"), 0, count);
    QString error;
    auto made = Lift::fromDom(m_answer, m_resources, &error, [this](int done, int total) {
        m_done = done;
        m_total = total;
        return !m_cancelled;
    });
    if (!m_running)
        return;
    if (!made)
        return finish(error);
    m_result = std::move(made);
    finish();
}

void LiftJob::screenRead()
{
    if (!m_running)
        return;
    const ScreenRead read = m_watcher.result();
    QString error;
    const QString label = m_label;
    if (m_kind == Kind::screen) {
        const QRect local = m_region.isEmpty() ? QRect(QPoint(0, 0), m_window.rect.size()) : m_region;
        if (read.tree) {
            setStage(QStringLiteral("Building shapes…"));
            m_result = Lift::fromAccessible(*read.tree, read.shot, m_window.rect.size(), m_region, label, &error, [this](int done, int total) {
                m_done = done;
                m_total = total;
                return !m_cancelled;
            });
            if (!m_running)
                return;
            if (m_result)
                return finish();
        }
        // No tree (or nothing in it): trace the pixels instead.
        setStage(QStringLiteral("Tracing…"));
        const double scale = read.shot.isNull() ? 1 : double(read.shot.width()) / std::max(1, m_window.rect.width());
        const QImage part = read.shot.copy(QRectF(QPointF(local.topLeft()) * scale, QSizeF(local.size()) * scale).toAlignedRect());
        m_result = Lift::fromTrace(part, local, label, &error);
        if (m_result && !read.treeError.isEmpty())
            m_result->notes.prepend(read.treeError);
    } else {
        setStage(QStringLiteral("Tracing…"));
        m_result = Lift::fromTrace(read.shot, QRectF(m_region).translated(-m_origin), label, &error);
    }
    if (!m_result)
        return finish(error.isEmpty() ? read.shotError : error);
    finish();
}
