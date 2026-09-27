#pragma once
#include "Live/StaticServer.h"
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

// How to run a project for Live mode: omastrator.json's override, else the
// package.json dev (or start) script with the lockfile's package manager,
// else the folder served as a static site.
struct DevCommand {
    enum class Kind { override, script, staticSite };
    Kind kind = Kind::staticSite;
    QString program;
    QStringList arguments;
    // "pnpm run dev", for the island and the log.
    QString description;
    // omastrator.json may say where the server listens; otherwise its output does.
    QUrl url;

    // Returns nullopt with `why` when the folder has nothing to run.
    static std::optional<DevCommand> detect(const QString &folder, QString *why);
};

class DevServer : public QObject {
    Q_OBJECT
public:
    explicit DevServer(QObject *parent = nullptr);
    ~DevServer() override;

    // Starts the project and waits until it answers. Returns why it couldn't, or empty.
    QString start(const QString &folder, int timeoutMs = 120'000);
    void stop();
    QUrl url() const { return m_url; }
    const DevCommand &command() const { return m_command; }
    QString output() const { return QString::fromUtf8(m_output); }
    // The first local URL a dev server printed, without colour codes.
    static QUrl urlIn(const QString &output);
    // True once something answers HTTP at `url`.
    static bool answers(const QUrl &url, int timeoutMs);

signals:
    void exited();

private:
    QProcess m_process;
    StaticServer m_static;
    DevCommand m_command;
    QUrl m_url;
    QByteArray m_output;
};
