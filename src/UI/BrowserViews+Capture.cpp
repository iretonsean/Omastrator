#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>
#include <algorithm>

// The frame's page as a picture on demand, for Record (docs/MOTION.md, section 8): the screencast sends a picture only when the
// page paints, so a step that paints what the last one did would wait for ever; a screenshot always answers.

void BrowserViews::capture(const QUuid &frame, int longSide, CaptureDone done)
{
    const auto found = m_entries.constFind(frame);
    if (found == m_entries.constEnd() || found->state != State::live || found->applied.css.isEmpty()) {
        // A tab that is opening again, or a frame that reconcile has just paused, is back within moments; a frame that stays away isn't showing.
        static constexpr int retryMs = 100;
        static constexpr int retries = 30;
        // Each timer keeps the retry alive until it runs; the retry holds itself only weakly, so it doesn't outlive them in a cycle.
        const auto attempt = std::make_shared<std::function<void(int)>>();
        const std::weak_ptr<std::function<void(int)>> weak = attempt;
        *attempt = [this, frame, longSide, done, weak](int left) {
            const auto again = m_entries.constFind(frame);
            if (again != m_entries.constEnd() && again->state == State::live && !again->applied.css.isEmpty()) {
                capture(frame, longSide, done);
                return;
            }
            if (left <= 0) {
                done({}, {}, QStringLiteral("The page isn't showing. Bring the frame on screen and try again."));
                return;
            }
            if (const auto self = weak.lock())
                QTimer::singleShot(retryMs, this, [self, left] { (*self)(left - 1); });
        };
        QTimer::singleShot(0, this, [attempt] { (*attempt)(retries); });
        return;
    }
    const QSize css = found->applied.css;
    // On screen the frame is this wide, in device pixels; the picture is that, or the cap. The tab already draws at its own density,
    // and a screenshot's scale multiplies it.
    const double density = std::max(1, found->applied.scale);
    double pixels = css.width() * density;
    if (m_canvas && m_session.hasDocument() && m_session.document()->find(frame))
        pixels = m_canvas->documentToView().mapRect(m_session.document()->bounds(frame)).width() * m_canvas->devicePixelRatioF();
    const double wanted = std::min(pixels / css.width(), double(std::max(1, longSide)) / std::max(css.width(), css.height()));
    const double scale = std::clamp(wanted / density, 0.05, 8.0);
    const QUuid key = found->key;
    const QPointer<BrowserViews> guard(this);
    const auto finish = [guard, done](const QByteArray &jpeg, const QSizeF &size, const QString &error) {
        // The pool answers on its own thread.
        QMetaObject::invokeMethod(qApp, [guard, done, jpeg, size, error] {
            if (guard)
                done(jpeg, size, error);
        }, Qt::QueuedConnection);
    };
    // Two animation frames put a seek on the screen; the timer answers for a page that isn't drawing.
    const QString settle = QStringLiteral(
        "new Promise(done => { const at = () => [window.scrollX, window.scrollY];"
        " const timer = setTimeout(() => done(at()), 250);"
        " requestAnimationFrame(() => requestAnimationFrame(() => { clearTimeout(timer); done(at()); })); })");
    BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"), {{"expression", settle}, {"awaitPromise", true}, {"returnByValue", true}},
                               [key, css, scale, finish](const QJsonObject &result, const QString &error) {
                                   if (!error.isEmpty()) {
                                       finish({}, {}, error);
                                       return;
                                   }
                                   const QJsonArray scroll = result["result"].toObject()["value"].toArray();
                                   const QJsonObject clip{{"x", scroll.at(0).toDouble()}, {"y", scroll.at(1).toDouble()}, {"width", css.width()},
                                                          {"height", css.height()}, {"scale", scale}};
                                   BrowserViews::pool()->call(key, QStringLiteral("Page.captureScreenshot"), {{"format", "jpeg"}, {"quality", 90}, {"clip", clip}},
                                                              [css, finish](const QJsonObject &shot, const QString &why) {
                                                                  const QByteArray bytes = QByteArray::fromBase64(shot["data"].toString().toLatin1());
                                                                  if (!why.isEmpty() || bytes.isEmpty())
                                                                      finish({}, {}, why.isEmpty() ? QStringLiteral("The page gave no picture.") : why);
                                                                  else
                                                                      finish(bytes, QSizeF(css), {});
                                                              });
                               });
}
