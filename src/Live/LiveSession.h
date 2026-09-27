#pragma once
#include "Live/Browser.h"
#include "Live/DevServer.h"
#include "Live/Tokens.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <optional>
#include <vector>

// One change made on the page, as write-back (Phase 6) needs it: where, what
// it was, what it became, and the token and class swap it snapped to.
struct LiveEdit {
    QString selector;
    // A CSS property, or "text".
    QString property;
    QString before;
    QString after;
    QString token;
    QString removeClass;
    QString addClass;
    QString classesBefore;
    QString classesAfter;
    // The element as it was before its first edit.
    QJsonObject element;
    QJsonObject toJson() const;
};

// Live mode (docs/OS-SUITE.md): a page in Omastrator's own Chromium with the
// overlay injected. Edits apply to the page at once, snapped to the page's
// tokens, and are kept for write-back. Pages without a registered project
// folder are mock-ups: they change in the browser only.
class LiveSession : public QObject {
    Q_OBJECT
public:
    enum class State { off, starting, running, failed };
    struct Target {
        // A page to open; with `folder`, the user has confirmed it is that site's code.
        QUrl url;
        QString folder;
        bool headless = false;
        // Electron-style: the page opens as an app window (Phase 8).
        bool app = false;
        QString profile;
    };

    explicit LiveSession(QObject *parent = nullptr);
    ~LiveSession() override;

    // Checks the target, then starts in the background. Returns why it can't start, or empty.
    QString start(const Target &target);
    void stop();

    State state() const { return m_state; }
    QString message() const { return m_message; }
    QUrl url() const { return m_url; }
    QString project() const { return m_project; }
    bool isMockup() const { return m_project.isEmpty(); }
    const std::vector<LiveEdit> &edits() const { return m_edits; }
    const QJsonArray &selection() const { return m_selection; }
    const TokenSet &tokens() const { return m_tokens; }
    const DevServer &devServer() const { return m_devServer; }
    Browser &browser() { return m_browser; }
    // The page's DevTools session, for input the tests and native apps send.
    QString pageSession() const { return m_page ? m_page->sessionId : QString(); }
    // For the status stream's "live" key.
    QJsonObject status() const;

    // Runs `expression` in the page and returns its value.
    QJsonValue evaluate(const QString &expression, QString *error = nullptr);
    // The bar's path for one edit: snap, apply, record. Returns why it failed, or empty.
    QString edit(const QString &selector, const QString &property, const QString &value);
    // Forgets the recorded edits, or keeps only `edits` (the ones write-back left for the agent).
    void clearEdits();
    void setEdits(std::vector<LiveEdit> edits);
    void removeEdit(int index);
    // Shows a line in the overlay's bar.
    void notice(const QString &text);
    // A PNG of the page, or of one element with some room around it. Returns why it failed, or empty.
    QString screenshot(const QString &path, const QString &selector = QString());

    // The overlay's source.
    static QString overlayScript();

signals:
    void changed();
    void editApplied(const LiveEdit &edit);
    // "Ask AI…" in the bar: the prompt and the selected elements.
    void askRequested(const QString &prompt, const QJsonArray &elements);

private:
    void run(Target target);
    void fail(const QString &message);
    void setState(State state, const QString &message = QString());
    void onEvent(const QString &method, const QJsonObject &params, const QString &sessionId);
    void handle(const QJsonObject &message);
    void rescanTokens();
    void record(const QJsonObject &element, const TokenSet::Resolution &resolution, const QJsonObject &applied, const QString &textBefore);

    Browser m_browser;
    DevServer m_devServer;
    std::optional<Browser::Page> m_page;
    State m_state = State::off;
    QString m_message;
    QUrl m_url;
    QString m_project;
    TokenSet m_tokens;
    QJsonArray m_selection;
    std::vector<LiveEdit> m_edits;
    // Bumped by stop(), so a start still running notices it was cancelled.
    int m_generation = 0;
};
