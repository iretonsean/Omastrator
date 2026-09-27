#pragma once
#include "Live/Cdp.h"
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <optional>

// Chromium in Omastrator's own profile with DevTools on a random localhost
// port, read back from DevToolsActivePort. The user's own profile is never
// used. $OMASTRATOR_CHROMIUM names the browser; tests run it headless.
class Browser : public QObject {
    Q_OBJECT
public:
    struct Options {
        bool headless = false;
        // Default: defaultProfile().
        QString profile;
        // Opens this URL as an app window (an Omarchy web app) instead of a normal window.
        QUrl app;
        QStringList extraArguments;
        // An Electron app to run instead of Chromium, with its own arguments; it gets
        // the same debugging port and dedicated profile.
        QString program;
        QStringList programArguments;
    };
    struct Page {
        QString targetId;
        QString sessionId;
    };

    explicit Browser(QObject *parent = nullptr);
    ~Browser() override;

    // chromium, else Chrome; empty when neither is installed.
    static QString executable();
    // $XDG_DATA_HOME/omastrator/browser.
    static QString defaultProfile();

    // Starts the browser and connects to it. Returns why it failed, or empty.
    QString start(const Options &options);
    void stop();
    bool isRunning() const;
    // The browser's own process, which owns its windows: design mode knows its pages by it.
    qint64 processId() const { return m_process.processId(); }
    CdpConnection &cdp() { return m_cdp; }
    // The first page tab, attached, with Page and Runtime enabled; `url` loaded if given.
    std::optional<Page> attachPage(const QUrl &url, QString *error);
    // Loads `url` in the page and waits for its load event; an empty `url` reloads what is there.
    QString navigate(const Page &page, const QUrl &url, int timeoutMs = 30'000);

signals:
    void exited();

private:
    // Once connected, the program's output is read and dropped.
    bool m_draining = false;
    QProcess m_process;
    CdpConnection m_cdp;
    QString m_profile;
};
