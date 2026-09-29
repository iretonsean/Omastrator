#pragma once
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QUuid>

// What the canvas asks of whoever streams a Browser View's page (docs/BROWSER-VIEW.md), so the canvas
// needn't link the browser code.
class BrowserViewHost {
public:
    virtual ~BrowserViewHost() = default;
    // The newest picture of the page, or a null image to draw the frame's stored one.
    virtual QImage picture(const QUuid &frame) const = 0;
    // A line drawn over the frame, as "Paused by reset"; empty when the page shows as it is.
    virtual QString message(const QUuid &frame) const = 0;

    // What the address bar shows besides the address.
    struct Bar {
        bool loading = false;
        bool canGoBack = false;
        bool canGoForward = false;
        // The page isn't one of the user's own sites, so edits to it stay on this machine.
        bool notYours = false;
    };
    enum class Action { back, forward, reload, reloadIgnoringCache, stop };
    virtual Bar bar(const QUuid &frame) const;
    virtual void act(const QUuid &frame, Action action);

    // A protocol command for the frame's page (the Browse tool's input). False when the page can't take it: not open yet,
    // or paused, in which case a frame that reset paused starts again.
    virtual bool dispatch(const QUuid &frame, const QString &method, const QJsonObject &params);

    // The sign-in strip inside the first Browser View, until Sign In… or Not Now answers it.
    virtual bool signInOffered() const;
    virtual void signIn();
    virtual void dismissSignIn();
};

inline BrowserViewHost::Bar BrowserViewHost::bar(const QUuid &) const { return {}; }
inline void BrowserViewHost::act(const QUuid &, Action) {}
inline bool BrowserViewHost::dispatch(const QUuid &, const QString &, const QJsonObject &) { return false; }
inline bool BrowserViewHost::signInOffered() const { return false; }
inline void BrowserViewHost::signIn() {}
inline void BrowserViewHost::dismissSignIn() {}
