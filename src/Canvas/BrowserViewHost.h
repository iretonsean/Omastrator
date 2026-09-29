#pragma once
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <optional>

class QMenu;

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
        // Live runs the page from the project's dev server; `devTip` is its address and command.
        bool dev = false;
        QString devTip;
        // Deploy, for a frame running Live on the user's own site: the button's word, the stage while it runs, or its
        // result for a few seconds. Empty when there is nothing to deploy.
        QString deploy;
        bool deployBusy = false;
        bool deployFailed = false;
        QString deployTip;
    };
    // `deployButton` is the bar's pill (it opens Details after a failure and the site after a deploy); `deploy` and
    // `save` always start one. The rest are the bar menu's, for the frame's project.
    enum class Action { back, forward, reload, reloadIgnoringCache, stop, thisIsMySite, deployButton, deploy, save, reviewChanges, history, stopLive };
    virtual Bar bar(const QUuid &frame) const;
    virtual void act(const QUuid &frame, Action action);

    // A protocol command for the frame's page (the Browse tool's input). False when the page can't take it: not open yet,
    // or paused, in which case a frame that reset paused starts again.
    virtual bool dispatch(const QUuid &frame, const QString &method, const QJsonObject &params);

    // The widths the frame's breakpoint buttons offer, ascending: its own site's media queries, or the defaults.
    virtual QList<int> breakpoints(const QUuid &frame) const;

    // Edit Page (docs/LIVE-IN-FRAME.md, section 3): the page's elements can be picked. Beginning starts Live on the frame
    // if it isn't running; the answer is why it can't, or empty.
    virtual QString beginEditPage(const QUuid &frame);
    // The host's own items for the bar's right-click menu (Live's: keep edits, edit sets, This Is My Site…).
    virtual void extendBarMenu(const QUuid &frame, QMenu *menu);
    virtual void endEditPage(const QUuid &frame);
    // The page's hover and selection boxes in its CSS px (the frame's box top-left is 0,0), each with a "tag  W × H" label.
    struct EditBox {
        QRectF rect;
        QString label;
    };
    struct EditBoxes {
        std::optional<EditBox> hover;
        QList<EditBox> selection;
    };
    virtual EditBoxes editBoxes(const QUuid &frame) const;

    // The element bar (section 3): the selected elements as the page reports them ({selector, tag, classes, text, textOnly,
    // rect, styles}) and the page's tokens ({colors, spacing, ...} of {name, value}), or nothing while none is picked.
    struct ElementState {
        QJsonArray selection;
        QJsonObject tokens;
    };
    virtual ElementState elementState(const QUuid &frame) const;
    // Every selected element takes `value` for each of `properties`. A preview only shows it (a scrub step); the edit
    // that follows records it, snapped to a token where the page has one. The answer is why it can't, or empty.
    virtual QString editElements(const QUuid &frame, const QStringList &properties, const QString &value, bool preview);
    virtual QString editElementText(const QUuid &frame, const QString &selector, const QString &text);
    // Live's own undo (section 4), which Ctrl+Z reaches in Edit Page.
    virtual bool canUndoPageEdit(const QUuid &frame) const;
    virtual bool canRedoPageEdit(const QUuid &frame) const;
    virtual void undoPageEdit(const QUuid &frame);
    virtual void redoPageEdit(const QUuid &frame);

    // The sign-in strip inside the first Browser View, until Sign In… or Not Now answers it.
    virtual bool signInOffered() const;
    virtual void signIn();
    virtual void dismissSignIn();
};

inline BrowserViewHost::Bar BrowserViewHost::bar(const QUuid &) const { return {}; }
inline void BrowserViewHost::act(const QUuid &, Action) {}
inline bool BrowserViewHost::dispatch(const QUuid &, const QString &, const QJsonObject &) { return false; }
inline QList<int> BrowserViewHost::breakpoints(const QUuid &) const { return {390, 768, 1280, 1440}; }
inline bool BrowserViewHost::signInOffered() const { return false; }
inline void BrowserViewHost::signIn() {}
inline void BrowserViewHost::dismissSignIn() {}
inline void BrowserViewHost::extendBarMenu(const QUuid &, QMenu *) {}
inline QString BrowserViewHost::beginEditPage(const QUuid &) { return QStringLiteral("Edit Page needs a live page."); }
inline void BrowserViewHost::endEditPage(const QUuid &) {}
inline BrowserViewHost::EditBoxes BrowserViewHost::editBoxes(const QUuid &) const { return {}; }
inline BrowserViewHost::ElementState BrowserViewHost::elementState(const QUuid &) const { return {}; }
inline QString BrowserViewHost::editElements(const QUuid &, const QStringList &, const QString &, bool)
{
    return QStringLiteral("Edit Page needs a live page.");
}
inline QString BrowserViewHost::editElementText(const QUuid &, const QString &, const QString &)
{
    return QStringLiteral("Edit Page needs a live page.");
}
inline bool BrowserViewHost::canUndoPageEdit(const QUuid &) const { return false; }
inline bool BrowserViewHost::canRedoPageEdit(const QUuid &) const { return false; }
inline void BrowserViewHost::undoPageEdit(const QUuid &) {}
inline void BrowserViewHost::redoPageEdit(const QUuid &) {}
