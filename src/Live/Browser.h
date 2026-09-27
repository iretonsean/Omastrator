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
        // Opens this URL as an app window (Electron-style) instead of a normal window.
        QUrl app;
        QStringList extraArguments;
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
    CdpConnection &cdp() { return m_cdp; }
    // The first page tab, attached, with Page and Runtime enabled; `url` loaded if given.
    std::optional<Page> attachPage(const QUrl &url, QString *error);
    // Loads `url` in the page and waits for its load event.
    QString navigate(const Page &page, const QUrl &url, int timeoutMs = 30'000);

signals:
    void exited();

private:
    QProcess m_process;
    CdpConnection m_cdp;
    QString m_profile;
};
