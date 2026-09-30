#include "Live/LiveSession.h"
#include "Agent/Capture.h"
#include "Live/OverlayScript.h"
#include "Live/Registry.h"
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <algorithm>

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
            {"removeClass", removeClass}, {"addClass", addClass}, {"classesBefore", classesBefore}, {"classesAfter", classesAfter},
            {"path", path}};
}

LiveSession::LiveSession(QObject *parent) : QObject(parent)
{
    connect(&m_browser.cdp(), &CdpConnection::event, this, &LiveSession::onEvent);
    connect(&m_browser, &Browser::exited, this, [this] {
        // The user closed the browser: Live ends, the edits stay for review.
        m_page.reset();
        releaseServer(true);
        if (m_state == State::running || m_state == State::starting)
            setState(State::off, QStringLiteral("The browser closed."));
    });
    connect(&DevServers::shared(), &DevServers::step, this, [this](const QString &folder, const QString &message) {
        if (m_state == State::starting && folder == m_serverFolder)
            setState(State::starting, message);
    });
    connect(&DevServers::shared(), &DevServers::exited, this, [this](const QString &folder) {
        // The lease is dead with the server: a frame goes back to the production page and says why.
        if (m_pool && m_lease && folder == m_serverFolder)
            fail(QStringLiteral("The project's dev server stopped."));
    });
}

void LiveSession::setBrowserLink(BrowserLink *link)
{
    if (m_link)
        m_link->disconnect(this), m_link->cdp().disconnect(this);
    m_link = link;
    if (!link)
        return;
    connect(&link->cdp(), &CdpConnection::event, this, &LiveSession::onEvent);
    connect(link, &BrowserLink::connectedChanged, this, [this] {
        if (m_inTab && m_link && !m_link->isConnected() && (m_state == State::running || m_state == State::starting)) {
            ++m_generation;
            m_page.reset();
            m_inTab = false;
            setState(State::off, QStringLiteral("Chromium closed, or its Omastrator extension stopped."));
        }
    });
}

qint64 LiveSession::browserProcessId() const
{
    if (m_state != State::running)
        return 0;
    if (m_inTab)
        return m_link ? m_link->chromiumPid() : 0;
    return m_browser.processId();
}

LiveSession::~LiveSession()
{
    DevServers::shared().disconnect(this);
    m_browser.disconnect(this);
    m_browser.cdp().disconnect(this);
    if (m_link)
        m_link->disconnect(this), m_link->cdp().disconnect(this);
    stop();
}

LiveSession::Busy::~Busy()
{
    if (--session.m_depth == 0 && session.m_deleteWhenIdle) {
        session.m_deleteWhenIdle = false;
        // From the session's own thread, so the delete waits for the loop it was asked in, not for a nested one.
        session.deleteLater();
    }
}

void LiveSession::deleteWhenIdle()
{
    if (m_depth == 0)
        deleteLater();
    else
        m_deleteWhenIdle = true;
}

QJsonObject LiveSession::call(CdpConnection &connection, const QString &method, const QJsonObject &params, const QString &sessionId,
                              QString *error, int timeoutMs)
{
    Busy busy(*this);
    return connection.callAndWait(method, params, sessionId, error, timeoutMs);
}

QString LiveSession::overlayScript()
{
    return QString::fromUtf8(OmastratorLive::overlay) + QLatin1Char('\n') + QString::fromUtf8(OmastratorLive::motion);
}

QString LiveSession::frameOverlayScript()
{
    return QStringLiteral("window.__omaHost = 'frame';\n") + overlayScript();
}

QString LiveSession::start(const Target &target)
{
    if (m_state == State::starting)
        return QStringLiteral("Live is already starting.");
    if (target.pool) {
        if (target.frame.isNull())
            return QStringLiteral("Live in a Browser View needs the frame's id.");
        if (!target.folder.isEmpty() && !QFileInfo(target.folder).isDir())
            return QStringLiteral("%1 isn't a folder.").arg(target.folder);
        stop();
        m_edits.clear();
        forgetSteps();
        m_selection = {};
        m_geometry = {};
        m_pool = target.pool;
        m_frame = target.frame;
        m_targetFolder = target.folder;
        m_targetOrigin = (target.url.scheme() == QLatin1String("http") || target.url.scheme() == QLatin1String("https")) ? EditSets::originOf(target.url) : QString();
        m_project.clear();
        setState(State::starting, QStringLiteral("Waiting for the page…"));
        const int generation = m_generation;
        QTimer::singleShot(0, this, [this, target, generation] {
            if (generation == m_generation)
                runFrame(target);
        });
        return {};
    }
    if (target.tab >= 0) {
        if (!m_link || !m_link->isConnected())
            return QStringLiteral("Omastrator's Chromium extension isn't connected. Run `omastrator setup`, then restart Chromium.");
        if (!target.folder.isEmpty() && !QFileInfo(target.folder).isDir())
            return QStringLiteral("%1 isn't a folder.").arg(target.folder);
        stop();
        m_edits.clear();
        forgetSteps();
        m_selection = {};
        setState(State::starting, QStringLiteral("Joining your tab…"));
        const int generation = m_generation;
        QTimer::singleShot(0, this, [this, target, generation] {
            if (generation == m_generation)
                runInTab(target);
        });
        return {};
    }
    if (target.url.isEmpty() && target.folder.isEmpty() && target.command.trimmed().isEmpty())
        return QStringLiteral("Choose a page, an app or a project folder.");
    if (!target.command.trimmed().isEmpty() && QProcess::splitCommand(target.command).isEmpty())
        return QStringLiteral("That app command can't be read.");
    if (!target.url.isEmpty() && !target.url.isValid())
        return QStringLiteral("That isn't a web address.");
    if (!target.url.isEmpty() && target.url.scheme() != QLatin1String("http") && target.url.scheme() != QLatin1String("https")
        && target.url.scheme() != QLatin1String("file"))
        return QStringLiteral("Live opens http, https and file pages.");
    if (!target.folder.isEmpty() && !QFileInfo(target.folder).isDir())
        return QStringLiteral("%1 isn't a folder.").arg(target.folder);
    if (target.command.trimmed().isEmpty() && Browser::executable().isEmpty())
        return QStringLiteral("Chromium isn't installed. Install it with: sudo pacman -S chromium");
    stop();
    m_edits.clear();
    forgetSteps();
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
    // An Electron app brings its own page; everything after is the same pipeline.
    if (!target.command.trimmed().isEmpty()) {
        m_project = folder.isEmpty() ? QString() : QFileInfo(folder).canonicalFilePath();
        setState(State::starting, QStringLiteral("Starting the app…"));
        QStringList words = QProcess::splitCommand(target.command);
        Browser::Options options;
        options.program = words.takeFirst();
        options.programArguments = words;
        options.profile = target.profile.isEmpty()
                              ? QDir(QFileInfo(Browser::defaultProfile()).absolutePath()).filePath(QStringLiteral("apps/") + QFileInfo(options.program).fileName())
                              : target.profile;
        if (const QString failure = m_browser.start(options); !failure.isEmpty())
            return cancelled() ? void() : fail(failure);
        if (cancelled())
            return;
        QString error;
        m_page = m_browser.attachPage({}, &error);
        if (!m_page)
            return fail(error);
        if (const QString failure = prepare(); !failure.isEmpty())
            return fail(failure);
        // A reload runs the overlay in the app's page.
        if (const QString failure = m_browser.navigate(*m_page, {}); !failure.isEmpty())
            return cancelled() ? void() : fail(failure);
        m_url = QUrl(evaluate(QStringLiteral("location.href")).toString());
        if (m_project.isEmpty() && (m_url.scheme() == QLatin1String("http") || m_url.scheme() == QLatin1String("https")))
            m_project = ProjectRegistry::folderFor(m_url).value_or(QString());
        setState(State::running, isMockup() ? QStringLiteral("Mock-up: changes stay in the app.") : QString());
        pageLoaded();
        return;
    }
    QUrl url = target.url;
    // A registered site runs from its own dev server, at the same path; a page already on localhost is used as it is.
    if (!folder.isEmpty() && (url.isEmpty() || !isLocal(url))) {
        setState(State::starting, QStringLiteral("Starting the project…"));
        if (const QString failure = startServer(folder, generation); !failure.isEmpty())
            return cancelled() ? void() : fail(failure);
        if (cancelled())
            return;
        QUrl served = m_serverUrl;
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
    if (const QString failure = prepare(); !failure.isEmpty())
        return fail(failure);
    if (const QString failure = m_browser.navigate(*m_page, url); !failure.isEmpty())
        return cancelled() ? void() : fail(failure);
    if (cancelled())
        return;
    setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
    pageLoaded();
}

void LiveSession::runInTab(const Target &target)
{
    const int generation = m_generation;
    auto cancelled = [&] { return generation != m_generation; };
    m_inTab = true;
    QString error;
    const QJsonObject attached = call(cdp(), QStringLiteral("Omastrator.attach"), {{"tabId", target.tab}}, {}, &error);
    if (cancelled())
        return;
    if (!error.isEmpty())
        return fail(QStringLiteral("Couldn't join the tab: %1").arg(error));
    m_page = Browser::Page{QStringLiteral("tab-%1").arg(attached["tabId"].toInt()), attached["sessionId"].toString()};
    m_url = QUrl(attached["url"].toString());
    m_title = attached["title"].toString();
    for (const char *domain : {"Page.enable", "Runtime.enable"}) {
        call(cdp(), QLatin1String(domain), {}, m_page->sessionId, &error);
        if (cancelled())
            return;
        if (!error.isEmpty())
            return fail(QStringLiteral("Couldn't join the tab: %1").arg(error));
    }
    if (const QString failure = prepare(); !failure.isEmpty())
        return cancelled() ? void() : fail(failure);
    // The page is already loaded: the overlay goes in now, and prepare() brings it back after each navigation.
    evaluate(overlayScript(), &error);
    if (cancelled())
        return;
    if (!error.isEmpty())
        return fail(QStringLiteral("Couldn't add the overlay to the page: %1").arg(error));
    QString folder = target.folder;
    if (!folder.isEmpty() && (m_url.scheme() == QLatin1String("http") || m_url.scheme() == QLatin1String("https"))) {
        if (const QString failure = ProjectRegistry::remember(m_url, folder); !failure.isEmpty())
            return fail(failure);
    } else if (folder.isEmpty()) {
        folder = ProjectRegistry::folderFor(m_url).value_or(QString());
    }
    m_project = folder.isEmpty() ? QString() : QFileInfo(folder).canonicalFilePath();
    setState(State::running, isMockup() ? QStringLiteral("Not your site: changes stay on this machine.") : QString());
    pageLoaded();
}

void LiveSession::leaveTab()
{
    if (!m_inTab)
        return;
    if (m_page && m_link && m_link->isConnected()) {
        // Short waits: leaving never hangs on a browser that has stopped answering.
        call(cdp(), QStringLiteral("Runtime.evaluate"), {{"expression", "window.__oma && window.__oma.leave()"}}, m_page->sessionId, nullptr, 2000);
        cdp().call(QStringLiteral("Omastrator.detach"), {{"sessionId", m_page->sessionId}}, {});
    }
    m_inTab = false;
    m_title.clear();
}

QString LiveSession::prepare()
{
    QString error;
    CdpConnection &cdp = this->cdp();
    call(cdp, QStringLiteral("Runtime.addBinding"), {{"name", "omastratorSend"}}, m_page->sessionId, &error);
    if (error.isEmpty()) {
        const QJsonObject added = call(cdp, QStringLiteral("Page.addScriptToEvaluateOnNewDocument"),
                                       {{"source", m_pool ? frameOverlayScript() : overlayScript()}}, m_page->sessionId, &error);
        m_scriptId = added["identifier"].toString();
    }
    return error.isEmpty() ? QString() : QStringLiteral("Couldn't prepare the page: %1").arg(error);
}

void LiveSession::fail(const QString &message)
{
    // Read before leaveFrame() lets go of the pool: a frame's thread is the pool's, and it mustn't wait for a server to stop.
    const bool framed = !m_frame.isNull();
    leaveTab();
    leaveFrame();
    if (!framed)
        m_browser.stop();
    releaseServer(!framed);
    m_page.reset();
    setState(State::failed, message);
}

void LiveSession::stop()
{
    ++m_generation;
    m_serving = false;
    leaveTab();
    leaveFrame();
    m_page.reset();
    // The pool's browser isn't ours, and this may be its thread.
    const bool framed = !m_frame.isNull();
    if (!framed)
        m_browser.stop();
    releaseServer(!framed);
    m_frame = {};
    m_pageEditing = false;
    m_tokensScanned = false;
    if (m_state != State::off)
        setState(State::off);
}

QString LiveSession::startServer(const QString &folder, int generation)
{
    QEventLoop loop;
    DevServers::Result got;
    bool finished = false;
    // Released by stop() while it starts: nobody answers, so the wait looks for the new generation.
    QTimer poll;
    poll.setInterval(100);
    connect(&poll, &QTimer::timeout, &loop, [&] {
        if (generation != m_generation)
            loop.quit();
    });
    poll.start();
    m_serverFolder = DevServers::keyFor(folder);
    m_lease = DevServers::shared().acquire(folder, &loop, [&](const DevServers::Result &result) {
        got = result;
        finished = true;
        loop.quit();
    });
    {
        Busy busy(*this);
        loop.exec();
    }
    if (!finished)
        return QStringLiteral("Live was stopped.");
    if (!got.error.isEmpty()) {
        m_lease = 0;
        return got.error;
    }
    m_serverUrl = got.url;
    m_serverCommand = got.command;
    return {};
}

void LiveSession::releaseServer(bool wait)
{
    if (m_lease)
        DevServers::shared().release(m_lease, wait);
    m_lease = 0;
    m_serverUrl.clear();
    m_serverProject.clear();
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
    QJsonObject site;
    if (m_state == State::running && isMockup()) {
        QJsonArray sets;
        for (const EditSets::Set &set : editSets())
            sets.append(set.summary());
        site = {{"origin", origin()}, {"notice", QStringLiteral("Not your site: changes stay on this machine.")}, {"sets", sets}};
    }
    return {{"state", QLatin1String(names[int(m_state)])},
            {"site", site},
            {"url", m_url.toString()},
            {"project", m_project},
            {"mockup", isMockup()},
            {"edits", int(m_edits.size())},
            {"selection", int(m_selection.size())},
            {"message", m_message},
            {"server", m_serverUrl.isEmpty() ? QString() : m_serverCommand.description},
            {"tab", m_inTab},
            {"extension", m_link && m_link->isConnected()}};
}

QJsonValue LiveSession::evaluate(const QString &expression, QString *error)
{
    if (!m_page) {
        if (error)
            *error = QStringLiteral("Live isn't running.");
        return {};
    }
    QString failure;
    const QJsonObject result = call(cdp(), QStringLiteral("Runtime.evaluate"),
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
    if (m_pool)
        evaluate(QStringLiteral("window.__oma && window.__oma.setHost('frame')"));
    const QJsonObject scan = evaluate(QStringLiteral("window.__oma ? window.__oma.scan() : null"), &error).toObject();
    const auto theme = omarchyColors();
    m_tokens = TokenSet::fromScan(scan, theme);
    // An empty answer means the overlay wasn't in the page yet.
    m_tokensScanned = !scan.isEmpty();
    QJsonObject ui;
    for (const auto &[name, color] : theme) {
        if (name == QLatin1String("background") || name == QLatin1String("foreground") || name == QLatin1String("accent"))
            ui[name] = color.name();
    }
    evaluate(QStringLiteral("window.__oma && window.__oma.setTokens(%1, %2)").arg(json(m_tokens.toJson()), json(ui)));
}

void LiveSession::onEvent(const QString &method, const QJsonObject &params, const QString &sessionId)
{
    if (!m_page || sessionId != m_page->sessionId) {
        // Chromium's "is debugging this browser" bar was cancelled, or the tab closed: Live ends there.
        if (m_page && m_inTab && method == QLatin1String("Omastrator.detached") && params["sessionId"].toString() == m_page->sessionId) {
            ++m_generation;
            m_page.reset();
            m_inTab = false;
            m_title.clear();
            setState(State::off, params["reason"].toString() == QLatin1String("canceled_by_user")
                                     ? QStringLiteral("You ended Live from Chromium's bar.")
                                     : QStringLiteral("The tab closed, so Live ended."));
        }
        return;
    }
    if (method == QLatin1String("Runtime.bindingCalled") && params["name"].toString() == QLatin1String("omastratorSend")) {
        handle(QJsonDocument::fromJson(params["payload"].toString().toUtf8()).object());
    } else if (method == QLatin1String("Page.loadEventFired") && (m_state == State::running || (m_pool && m_serving))) {
        // A reload (a dev server's, or the user's) brings a fresh overlay that needs the tokens again. A page the frame
        // goes to while its project's server starts is looked at too: it may be another site's.
        QTimer::singleShot(0, this, &LiveSession::pageLoaded);
    }
}

void LiveSession::handle(const QJsonObject &message)
{
    const QString type = message["type"].toString();
    if (type == QLatin1String("select")) {
        m_selection = message["elements"].toArray();
        emit changed();
    } else if (type == QLatin1String("geometry")) {
        m_geometry = message;
        emit geometryChanged();
    } else if (type == QLatin1String("motion")) {
        m_motion = message;
        emit motionChanged();
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
    } else if (type == QLatin1String("site") && isMockup()) {
        emit siteRequested(message["action"].toString(), message);
    }
}

QString LiveSession::edit(const QString &selector, const QString &property, const QString &value)
{
    const QString failure = applyEdit(selector, property, value);
    // The element bar shows what the page has now; a group of edits refreshes once, at its end.
    if (failure.isEmpty() && m_group == 0) {
        refreshSelection();
        emit changed();
    }
    return failure;
}

QString LiveSession::applyEdit(const QString &selector, const QString &property, const QString &value)
{
    QString error;
    const QJsonObject element = evaluate(QStringLiteral("window.__oma.info(%1)").arg(json(selector)), &error).toObject();
    if (element.isEmpty())
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    if (property == QLatin1String("text")) {
        // Only an element that holds text alone takes new text, as editing it in place does.
        if (!element["textOnly"].toBool())
            return QStringLiteral("%1 holds more than text, so its text can't be replaced.").arg(selector);
        const QJsonObject applied = evaluate(QStringLiteral("window.__oma.applyResolved(%1)")
                                                 .arg(json(QJsonObject{{"selector", selector}, {"property", property}, {"value", value}})),
                                             &error)
                                        .toObject();
        if (applied.isEmpty())
            return error.isEmpty() ? QStringLiteral("The page didn't take the change.") : error;
        record(element, TokenSet::Resolution{property, value, {}, {}, {}}, applied, element["text"].toString());
        return {};
    }
    const QStringList classes = element["classes"].toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Live reports running once the tab is attached, a moment before the page's tokens are scanned. An edit that comes in
    // between (a fast script, a slow machine) scans them now, so it snaps as it would a moment later.
    if (!m_tokensScanned)
        rescanTokens();
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
    edit.path = element["path"].toString(EditSets::pathOf(m_url));
    edit.origin = EditSets::originOf(m_url);
    const bool isText = resolution.property == QLatin1String("text");
    UndoStep step;
    step.selector = selector;
    step.property = edit.property;
    step.was = {{"style", element["inlineStyle"].toString()}, {"cls", element["classes"].toString()},
                {"text", isText ? QJsonValue(textBefore) : QJsonValue()}};
    step.now = {{"style", after["inlineStyle"].toString()}, {"cls", after["classes"].toString()},
                {"text", isText ? QJsonValue(resolution.value) : QJsonValue()}};
    step.group = m_group;
    keep(edit, step);
}

// Puts an edit in the list and its undo step on the stack. A second change to the same thing keeps the first one's "before".
void LiveSession::keep(LiveEdit edit, UndoStep step)
{
    const QString selector = edit.selector;
    m_redo.clear();
    auto remember = [&](const LiveEdit &made) {
        step.made = made;
        m_undo.push_back(step);
        if (m_undo.size() > 200)
            m_undo.erase(m_undo.begin());
    };
    for (LiveEdit &existing : m_edits) {
        if (existing.selector == selector && existing.property == edit.property && existing.origin == edit.origin) {
            edit.before = existing.before;
            edit.classesBefore = existing.classesBefore;
            edit.element = existing.element;
            if (!existing.removeClass.isEmpty() && edit.removeClass == existing.addClass)
                edit.removeClass = existing.removeClass;
            step.replaced = existing;
            existing = edit;
            remember(existing);
            emit editApplied(existing);
            emit changed();
            return;
        }
    }
    m_edits.push_back(edit);
    remember(edit);
    emit editApplied(edit);
    emit changed();
    // Later: a record can arrive inside another DevTools call, which mustn't be nested.
    if (isMockup())
        QTimer::singleShot(0, this, &LiveSession::describeSite);
}

void LiveSession::clearEdits()
{
    m_edits.clear();
    forgetSteps();
    emit changed();
}

void LiveSession::setEdits(std::vector<LiveEdit> edits)
{
    m_edits = std::move(edits);
    forgetSteps();
    emit changed();
}

void LiveSession::forgetSteps()
{
    m_undo.clear();
    m_redo.clear();
    ++m_stepsEpoch;
}

void LiveSession::removeEdits(const std::vector<LiveEdit> &edits)
{
    const auto taken = [&](const LiveEdit &each) { return std::find(edits.begin(), edits.end(), each) != edits.end(); };
    bool touched = std::erase_if(m_edits, taken) > 0;
    // A change queued behind the one that was taken merges into it and is no longer equal to it, so it is kept. What
    // the taken edit wrote is what the page's element was before the kept one, and the next write-back looks for that.
    for (LiveEdit &kept : m_edits) {
        for (const LiveEdit &gone : edits) {
            if (gone.selector != kept.selector || gone.property != kept.property || gone.origin != kept.origin)
                continue;
            if (kept.before != gone.after || kept.classesBefore != gone.classesAfter) {
                kept.before = gone.after;
                kept.classesBefore = gone.classesAfter;
                touched = true;
            }
            // A class swap that chained from the taken one now starts from the class the taken one added.
            if (!gone.addClass.isEmpty() && kept.removeClass == gone.removeClass && kept.addClass != gone.addClass) {
                kept.removeClass = gone.addClass;
                touched = true;
            }
        }
    }
    if (!touched)
        return;
    // The steps are about edits that are now written or sent, as removeEdit's are.
    forgetSteps();
    emit changed();
}

void LiveSession::removeEdit(int index)
{
    if (index >= 0 && index < int(m_edits.size())) {
        m_edits.erase(m_edits.begin() + index);
        forgetSteps();
        emit changed();
    }
}

QString LiveSession::undoEdit()
{
    if (m_undo.empty())
        return QStringLiteral("There's no page edit to undo.");
    const int group = m_undo.back().group;
    QString failure = undoStep();
    while (failure.isEmpty() && group != 0 && !m_undo.empty() && m_undo.back().group == group)
        failure = undoStep();
    refreshSelection();
    emit changed();
    return failure;
}

QString LiveSession::redoEdit()
{
    if (m_redo.empty())
        return QStringLiteral("There's no page edit to redo.");
    const int group = m_redo.back().group;
    QString failure = redoStep();
    while (failure.isEmpty() && group != 0 && !m_redo.empty() && m_redo.back().group == group)
        failure = redoStep();
    refreshSelection();
    emit changed();
    return failure;
}

QString LiveSession::undoStep()
{
    if (m_undo.empty())
        return QStringLiteral("There's no page edit to undo.");
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    // Taken off first: evaluate() waits in an event loop, and a clear posted to this session (a Save that wrote the edits)
    // can run in it and empty both stacks.
    const UndoStep step = m_undo.back();
    m_undo.pop_back();
    const int epoch = m_stepsEpoch;
    QString error;
    const bool put = evaluate(QStringLiteral("window.__oma.restore(%1, %2)").arg(json(step.selector), json(step.was)), &error).toBool();
    if (!error.isEmpty() || !put) {
        // Nothing was undone, so the step is still there to try again.
        if (epoch == m_stepsEpoch)
            m_undo.push_back(step);
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    }
    // The edit was written or sent while the page took the undo: it is no longer this session's to undo or redo.
    if (epoch != m_stepsEpoch) {
        emit changed();
        return {};
    }
    const auto found = std::find_if(m_edits.begin(), m_edits.end(), [&](const LiveEdit &each) {
        return each.selector == step.selector && each.property == step.property && each.origin == step.made.origin;
    });
    if (found != m_edits.end()) {
        if (step.replaced)
            *found = *step.replaced;
        else
            m_edits.erase(found);
    }
    m_redo.push_back(step);
    // A motion edit put back moves the rows, so the timeline reads them again.
    if (m_motionHeld)
        motionRefresh();
    emit changed();
    if (isMockup())
        QTimer::singleShot(0, this, &LiveSession::describeSite);
    return {};
}

QString LiveSession::redoStep()
{
    if (m_redo.empty())
        return QStringLiteral("There's no page edit to redo.");
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    const UndoStep step = m_redo.back();
    m_redo.pop_back();
    const int epoch = m_stepsEpoch;
    QString error;
    const bool put = evaluate(QStringLiteral("window.__oma.restore(%1, %2)").arg(json(step.selector), json(step.now)), &error).toBool();
    if (!error.isEmpty() || !put) {
        if (epoch == m_stepsEpoch)
            m_redo.push_back(step);
        return error.isEmpty() ? QStringLiteral("That element is gone from the page.") : error;
    }
    if (epoch != m_stepsEpoch) {
        emit changed();
        return {};
    }
    const auto found = std::find_if(m_edits.begin(), m_edits.end(), [&](const LiveEdit &each) {
        return each.selector == step.selector && each.property == step.property && each.origin == step.made.origin;
    });
    if (found != m_edits.end())
        *found = step.made;
    else
        m_edits.push_back(step.made);
    m_undo.push_back(step);
    if (m_motionHeld)
        motionRefresh();
    emit changed();
    if (isMockup())
        QTimer::singleShot(0, this, &LiveSession::describeSite);
    return {};
}

void LiveSession::refreshSelection()
{
    if (!m_page || m_selection.isEmpty())
        return;
    // The bar shows the values the page has now, which an edit or an undo has just changed.
    const QJsonValue fresh = evaluate(QStringLiteral("window.__oma ? window.__oma.selection() : null"));
    if (fresh.isArray())
        m_selection = fresh.toArray();
}

QString LiveSession::editSelection(const QStringList &properties, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    evaluate(QStringLiteral("window.__oma && window.__oma.endPreview()"));
    const QJsonArray elements = m_selection;
    if (elements.isEmpty())
        return QStringLiteral("Pick an element first.");
    m_group = ++m_lastGroup;
    QString failure;
    for (const QJsonValue &each : elements) {
        for (const QString &property : properties) {
            const QString error = applyEdit(each.toObject()["selector"].toString(), property, value);
            if (failure.isEmpty())
                failure = error;
        }
    }
    m_group = 0;
    refreshSelection();
    emit changed();
    return failure;
}

QString LiveSession::previewSelection(const QStringList &properties, const QString &value)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    evaluate(QStringLiteral("window.__oma && window.__oma.previewSelected(%1, %2)").arg(json(QJsonArray::fromStringList(properties)), json(value)), &error);
    return error;
}

void LiveSession::setPageEditing(bool on)
{
    m_pageEditing = on;
    if (m_page)
        evaluate(QStringLiteral("window.__oma && window.__oma.enable(%1)").arg(on ? "true" : "false"));
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
    const QJsonObject shot = call(cdp(), QStringLiteral("Page.captureScreenshot"), params, m_page->sessionId, &error);
    if (!error.isEmpty())
        return QStringLiteral("Couldn't take a screenshot of the page: %1").arg(error);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(shot["data"].toString().toLatin1())) <= 0)
        return QStringLiteral("Couldn't write %1.").arg(path);
    return {};
}

void LiveSession::pageLoaded()
{
    if (!m_page)
        return;
    const QUrl now(evaluate(QStringLiteral("location.href")).toString());
    if (m_inTab)
        m_title = evaluate(QStringLiteral("document.title")).toString();
    if (now.isValid() && !now.isEmpty() && now != m_url) {
        m_url = now;
        emit changed();
    }
    if (m_pool) {
        frameProject();
        // The production page is left as it is; the dev server's page takes the edits.
        if (m_serving)
            return;
    }
    m_original = false;
    // A page that began loading before the session attached never ran the script that comes with each new document.
    if (m_pool && !evaluate(QStringLiteral("!!window.__oma")).toBool())
        evaluate(frameOverlayScript());
    rescanTokens();
    // A frame's page is reloaded and replaced under its session, so its edits go back on every load.
    if (isMockup() || m_pool)
        evaluate(QStringLiteral("window.__oma && window.__oma.applyEdits(%1)").arg(json(EditSets::toJson(editsShown()))));
    if (m_pool)
        evaluate(QStringLiteral("window.__oma && window.__oma.enable(%1)").arg(m_pageEditing ? "true" : "false"));
    // A new page starts with its own states, and the timeline still holds: the new page's motion is held too. A load
    // event for the page that is already held (its overlay says so) changes nothing.
    if (m_pool && m_motionHeld && !evaluate(QStringLiteral("!!(window.__oma && window.__oma.motion && window.__oma.motion.isHeld())")).toBool()) {
        m_forced.clear();
        m_forcedNodes.clear();
        m_agentsOn = false;
        motionHold();
    }
    describeSite();
}

void LiveSession::describeSite()
{
    if (!m_page)
        return;
    if (!isMockup()) {
        evaluate(QStringLiteral("window.__oma && window.__oma.setSite(null)"));
        return;
    }
    QJsonArray sets;
    for (const EditSets::Set &set : editSets())
        sets.append(set.summary());
    const int pending = int(std::count_if(m_edits.begin(), m_edits.end(), [&](const LiveEdit &edit) { return editIsHere(edit); }));
    const QJsonObject site{{"origin", origin()}, {"sets", sets}, {"pending", pending}, {"suggested", EditSets::suggestedName(origin())}};
    evaluate(QStringLiteral("window.__oma && window.__oma.setSite(%1)").arg(json(site)));
}

bool LiveSession::editIsHere(const LiveEdit &edit) const
{
    return !isMockup() || edit.origin.isEmpty() || edit.origin == origin();
}

QString LiveSession::origin() const
{
    return m_url.isEmpty() ? QString() : EditSets::originOf(m_url);
}

std::vector<EditSets::Set> LiveSession::editSets() const
{
    return origin().isEmpty() ? std::vector<EditSets::Set>{} : EditSets::read(origin());
}

std::vector<EditSets::Edit> LiveSession::editsShown(const QString &name) const
{
    if (!name.isEmpty()) {
        for (const EditSets::Set &set : editSets())
            if (set.name == name)
                return set.edits;
        return {};
    }
    const QString path = EditSets::pathOf(m_url);
    // The sets belong to a site that isn't the user's.
    std::vector<EditSets::Edit> edits = isMockup() ? EditSets::active(origin(), path) : std::vector<EditSets::Edit>{};
    for (const LiveEdit &edit : m_edits)
        if ((edit.path.isEmpty() || edit.path == path) && editIsHere(edit))
            edits.push_back(EditSets::Edit::fromLive(edit));
    return edits;
}

QString LiveSession::keepEdits(const QString &name, QString *kept)
{
    if (!isMockup())
        return QStringLiteral("This is your site: its edits go to the code with Deploy.");
    // A frame that browsed on may hold edits made on other sites; only this one's are kept here.
    std::vector<EditSets::Edit> edits;
    for (const LiveEdit &edit : m_edits)
        if (editIsHere(edit))
            edits.push_back(EditSets::Edit::fromLive(edit));
    if (edits.empty())
        return QStringLiteral("There are no edits on this page to keep.");
    const QString chosen = name.trimmed().isEmpty() ? EditSets::suggestedName(origin()) : name.trimmed();
    if (const QString failure = EditSets::keep(origin(), chosen, edits); !failure.isEmpty())
        return failure;
    if (kept)
        *kept = chosen;
    // They're in the set now, and stay on the page.
    std::erase_if(m_edits, [&](const LiveEdit &edit) { return editIsHere(edit); });
    forgetSteps();
    emit changed();
    describeSite();
    notice(QStringLiteral("Kept as “%1”. It comes back every time you open this site in Omastrator.").arg(chosen));
    return {};
}

QString LiveSession::setEditSetEnabled(const QString &name, bool enabled)
{
    if (const QString failure = EditSets::setEnabled(origin(), name, enabled); !failure.isEmpty())
        return failure;
    const QString failure = showOriginal(false);
    emit changed();
    return failure;
}

QString LiveSession::removeEditSet(const QString &name)
{
    if (const QString failure = EditSets::remove(origin(), name); !failure.isEmpty())
        return failure;
    const QString failure = showOriginal(false);
    emit changed();
    return failure;
}

QString LiveSession::showOriginal(bool original)
{
    if (!m_page)
        return QStringLiteral("Live isn't running.");
    QString error;
    evaluate(QStringLiteral("window.__oma.revertAll()"), &error);
    if (error.isEmpty() && !original)
        evaluate(QStringLiteral("window.__oma.applyEdits(%1)").arg(json(EditSets::toJson(editsShown()))), &error);
    m_original = original && error.isEmpty();
    describeSite();
    emit changed();
    return error;
}
