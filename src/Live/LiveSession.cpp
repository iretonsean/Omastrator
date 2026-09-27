#include "Live/LiveSession.h"
#include "Agent/Capture.h"
#include "Live/OverlayScript.h"
#include "Live/Registry.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

namespace {
QString json(const QJsonValue &value)
{
    return QString::fromUtf8(value.isObject() ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
                                              : QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).mid(1).chopped(1));
}

bool isLocal(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("[::1]") || host == QLatin1String("::1");
}

// The theme the overlay's bar wears, and the colours tokens fall back to.
std::vector<std::pair<QString, QColor>> omarchyColors()
{
    QFile file(QDir(Capture::themeDirectory()).filePath(QStringLiteral("colors.toml")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return Capture::themeColors(QString::fromUtf8(file.readAll()));
}
}

QJsonObject LiveEdit::toJson() const
{
    return {{"selector", selector}, {"property", property}, {"before", before}, {"after", after}, {"token", token},
            {"removeClass", removeClass}, {"addClass", addClass}, {"classesBefore", classesBefore}, {"classesAfter", classesAfter}};
}

LiveSession::LiveSession(QObject *parent) : QObject(parent)
{
    connect(&m_browser.cdp(), &CdpConnection::event, this, &LiveSession::onEvent);
    connect(&m_browser, &Browser::exited, this, [this] {
        // The user closed the browser: Live ends, the edits stay for review.
        m_page.reset();
        m_devServer.stop();
        if (m_state == State::running || m_state == State::starting)
            setState(State::off, QStringLiteral("The browser closed."));
    });
}

LiveSession::~LiveSession()
{
    m_browser.disconnect(this);
    m_browser.cdp().disconnect(this);
    stop();
}

QString LiveSession::overlayScript()
{
    return QString::fromUtf8(OmastratorLive::overlay);
}

QString LiveSession::start(const Target &target)
{
    if (m_state == State::starting)
        return QStringLiteral("Live is already starting.");
    if (target.url.isEmpty() && target.folder.isEmpty())
        return QStringLiteral("Choose a page or a project folder.");
    if (!target.url.isEmpty() && !target.url.isValid())
        return QStringLiteral("That isn't a web address.");
    if (!target.url.isEmpty() && target.url.scheme() != QLatin1String("http") && target.url.scheme() != QLatin1String("https")
        && target.url.scheme() != QLatin1String("file"))
        return QStringLiteral("Live opens http, https and file pages.");
    if (!target.folder.isEmpty() && !QFileInfo(target.folder).isDir())
        return QStringLiteral("%1 isn't a folder.").arg(target.folder);
    if (Browser::executable().isEmpty())
        return QStringLiteral("Chromium isn't installed. Install it with: sudo pacman -S chromium");
    stop();
    m_edits.clear();
    m_selection = {};
    setState(State::starting, QStringLiteral("Starting…"));
    const int generation = m_generation;
    QTimer::singleShot(0, this, [this, target, generation] {
        if (generation == m_generation)
            run(target);
    });
    return {};
}

void LiveSession::run(Target target)
{
    const int generation = m_generation;
    auto cancelled = [&] { return generation != m_generation; };
    // Which folder, if any, this page's code is in.
    QString folder = target.folder;
    if (!target.url.isEmpty() && !folder.isEmpty()) {
        if (const QString failure = ProjectRegistry::remember(target.url, folder); !failure.isEmpty())
            return fail(failure);
    } else if (!target.url.isEmpty()) {
        folder = ProjectRegistry::folderFor(target.url).value_or(QString());
    }
    QUrl url = target.url;
    // A registered site runs from its own dev server, at the same path; a page already on localhost is used as it is.
    if (!folder.isEmpty() && (url.isEmpty() || !isLocal(url))) {
        setState(State::starting, QStringLiteral("Starting the project…"));
        if (const QString failure = m_devServer.start(folder); !failure.isEmpty())
            return cancelled() ? void() : fail(failure);
        if (cancelled())
            return;
        QUrl served = m_devServer.url();
        if (!url.isEmpty()) {
            served.setPath(url.path());
            served.setQuery(url.query());
        }
        url = served;
    }
    m_project = folder.isEmpty() ? QString() : QFileInfo(folder).canonicalFilePath();
    m_url = url;
    setState(State::starting, QStringLiteral("Opening the browser…"));
    Browser::Options options;
    options.headless = target.headless;
    options.profile = target.profile;
    if (target.app)
        options.app = url;
    if (const QString failure = m_browser.start(options); !failure.isEmpty())
        return cancelled() ? void() : fail(failure);
    if (cancelled())
        return;
    QString error;
    m_page = m_browser.attachPage({}, &error);
    if (!m_page)
        return fail(error);
    CdpConnection &cdp = m_browser.cdp();
    cdp.callAndWait(QStringLiteral("Runtime.addBinding"), {{"name", "omastratorSend"}}, m_page->sessionId, &error);
    cdp.callAndWait(QStringLiteral("Page.addScriptToEvaluateOnNewDocument"), {{"source", overlayScript()}}, m_page->sessionId, &error);
    if (!error.isEmpty())
        return fail(QStringLiteral("Couldn't prepare the page: %1").arg(error));
    // An app window already shows the page; a tab loads it now.
    if (target.app)
        cdp.callAndWait(QStringLiteral("Page.reload"), {}, m_page->sessionId, &error);
    if (const QString failure = m_browser.navigate(*m_page, url); !failure.isEmpty())
        return cancelled() ? void() : fail(failure);
    if (cancelled())
        return;
    rescanTokens();
    setState(State::running, isMockup() ? QStringLiteral("Mock-up: changes stay in the browser.") : QString());
}

void LiveSession::fail(const QString &message)
{
    m_browser.stop();
    m_devServer.stop();
    m_page.reset();
    setState(State::failed, message);
}

void LiveSession::stop()
{
    ++m_generation;
    m_page.reset();
    m_browser.stop();
    m_devServer.stop();
    if (m_state != State::off)
        setState(State::off);
}

void LiveSession::setState(State state, const QString &message)
{
    m_state = state;
    m_message = message;
    emit changed();
}

QJsonObject LiveSession::status() const
{
    static const char *names[] = {"off", "starting", "running", "failed"};
    return {{"state", QLatin1String(names[int(m_state)])},
            {"url", m_url.toString()},
            {"project", m_project},
            {"mockup", isMockup()},
            {"edits", int(m_edits.size())},
            {"selection", int(m_selection.size())},
            {"message", m_message},
            {"server", m_devServer.url().isEmpty() ? QString() : m_devServer.command().description}};
}

QJsonValue LiveSession::evaluate(const QString &expression, QString *error)
{
    if (!m_page) {
        if (error)
            *error = QStringLiteral("Live isn't running.");
        return {};
    }
    QString failure;
    const QJsonObject result = m_browser.cdp().callAndWait(QStringLiteral("Runtime.evaluate"),
                                                           {{"expression", expression}, {"returnByValue", true}, {"awaitPromise", true}},
                                                           m_page->sessionId, &failure);
    if (failure.isEmpty() && result.contains("exceptionDetails"))
        failure = result["exceptionDetails"].toObject()["exception"].toObject()["description"].toString();
    if (error)
        *error = failure;
    return failure.isEmpty() ? result["result"].toObject()["value"] : QJsonValue();
}

void LiveSession::rescanTokens()
{
    QString error;
    const QJsonObject scan = evaluate(QStringLiteral("window.__oma ? window.__oma.scan() : null"), &error).toObject();
    const auto theme = omarchyColors();
    m_tokens = TokenSet::fromScan(scan, theme);
    QJsonObject ui;
    for (const auto &[name, color] : theme) {
        if (name == QLatin1String("background") || name == QLatin1String("foreground") || name == QLatin1String("accent"))
            ui[name] = color.name();
    }
    evaluate(QStringLiteral("window.__oma && window.__oma.setTokens(%1, %2)").arg(json(m_tokens.toJson()), json(ui)));
}

void LiveSession::onEvent(const QString &method, const QJsonObject &params, const QString &sessionId)
{
    if (!m_page || sessionId != m_page->sessionId)
        return;
    if (method == QLatin1String("Runtime.bindingCalled") && params["name"].toString() == QLatin1String("omastratorSend")) {
        handle(QJsonDocument::fromJson(params["payload"].toString().toUtf8()).object());
    } else if (method == QLatin1String("Page.loadEventFired") && m_state == State::running) {
        // A reload (a dev server's, or the user's) brings a fresh overlay that needs the tokens again.
        QTimer::singleShot(0, this, &LiveSession::rescanTokens);
    }
}

void LiveSession::handle(const QJsonObject &message)
{
    const QString type = message["type"].toString();
    if (type == QLatin1String("select")) {
        m_selection = message["elements"].toArray();
        emit changed();
    } else if (type == QLatin1String("edit")) {
        const QJsonObject element = message["element"].toObject();
        const QString property = message["property"].toString();
        if (property == QLatin1String("text")) {
            TokenSet::Resolution resolution{property, message["value"].toString(), {}, {}, {}};
            record(element, resolution, {{"after", element}}, message["before"].toString());
            return;
        }
        edit(message["selector"].toString(), property, message["value"].toString());
    } else if (type == QLatin1String("ask")) {
        emit askRequested(message["prompt"].toString(), message["elements"].toArray());
    }
}

QString LiveSession::edit(const QString &selector, const QString &property, const QString &value)
{
    QString error;
    const QJsonObject element = evaluate(QStringLiteral("window.__oma.info(%1)").arg(json(selector)), &error).toObject();
    if (element.isEmpty())
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    const QStringList classes = element["classes"].toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    TokenSet::Resolution resolution = m_tokens.resolve(property, value, classes);
    QJsonObject request{{"selector", selector}, {"property", resolution.property}, {"value", resolution.value},
                        {"removeClass", resolution.removeClass}, {"addClass", resolution.addClass}};
    const QJsonObject applied = evaluate(QStringLiteral("window.__oma.applyResolved(%1)").arg(json(request)), &error).toObject();
    if (applied.isEmpty())
        return error.isEmpty() ? QStringLiteral("The page didn't take the change.") : error;
    record(applied["before"].toObject(), resolution, applied, QString());
    return {};
}

void LiveSession::record(const QJsonObject &element, const TokenSet::Resolution &resolution, const QJsonObject &applied, const QString &textBefore)
{
    const QString selector = element["selector"].toString();
    const QJsonObject after = applied["after"].toObject();
    LiveEdit edit;
    edit.selector = selector;
    edit.property = resolution.property;
    edit.before = resolution.property == QLatin1String("text") ? textBefore : element["styles"].toObject()[resolution.property].toString();
    edit.after = resolution.property == QLatin1String("text") ? resolution.value : resolution.value;
    edit.token = resolution.token;
    edit.removeClass = resolution.removeClass;
    edit.addClass = resolution.addClass;
    edit.classesBefore = element["classes"].toString();
    edit.classesAfter = after["classes"].toString();
    edit.element = element;
    // A second change to the same thing keeps the first one's "before".
    for (LiveEdit &existing : m_edits) {
        if (existing.selector == selector && existing.property == edit.property) {
            edit.before = existing.before;
            edit.classesBefore = existing.classesBefore;
            edit.element = existing.element;
            if (!existing.removeClass.isEmpty() && edit.removeClass == existing.addClass)
                edit.removeClass = existing.removeClass;
            existing = edit;
            emit editApplied(existing);
            emit changed();
            return;
        }
    }
    m_edits.push_back(edit);
    emit editApplied(edit);
    emit changed();
}

void LiveSession::clearEdits()
{
    m_edits.clear();
    emit changed();
}

void LiveSession::setEdits(std::vector<LiveEdit> edits)
{
    m_edits = std::move(edits);
    emit changed();
}

void LiveSession::removeEdit(int index)
{
    if (index >= 0 && index < int(m_edits.size())) {
        m_edits.erase(m_edits.begin() + index);
        emit changed();
    }
}

void LiveSession::notice(const QString &text)
{
    evaluate(QStringLiteral("window.__oma && window.__oma.notice(%1)").arg(json(text)));
}

QString LiveSession::screenshot(const QString &path, const QString &selector)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QJsonObject params{{"format", "png"}};
    if (!selector.isEmpty()) {
        QString error;
        const QJsonObject rect = evaluate(QStringLiteral("(() => { const e = document.querySelector(%1); if (!e) return null; "
                                                         "const r = e.getBoundingClientRect(); return {x: r.x + scrollX, y: r.y + scrollY, "
                                                         "width: r.width, height: r.height}; })()")
                                              .arg(json(selector)),
                                          &error)
                                     .toObject();
        if (rect.isEmpty())
            return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
        constexpr double room = 24;
        params["clip"] = QJsonObject{{"x", std::max(0.0, rect["x"].toDouble() - room)}, {"y", std::max(0.0, rect["y"].toDouble() - room)},
                                     {"width", rect["width"].toDouble() + 2 * room}, {"height", rect["height"].toDouble() + 2 * room}, {"scale", 1}};
        params["captureBeyondViewport"] = true;
    }
    QString error;
    const QJsonObject shot = m_browser.cdp().callAndWait(QStringLiteral("Page.captureScreenshot"), params, m_page->sessionId, &error);
    if (!error.isEmpty())
        return QStringLiteral("Couldn't take a screenshot of the page: %1").arg(error);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(shot["data"].toString().toLatin1())) <= 0)
        return QStringLiteral("Couldn't write %1.").arg(path);
    return {};
}
