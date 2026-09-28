#pragma once
#include "UI/DeviceSend.h"
#include "UI/Share.h"
#include "UI/ShareJob.h"
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QUuid>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class AgentBridge;
class ProjectTab;
class ProjectWorkspace;
class QWidget;

// Share with client (docs/SHARE.md): one press shares the front artboard, or the
// selection, and puts the link on the clipboard. It picks the destination itself
// (the remembered one, else a cloud service that makes links, else GitHub), and
// keeps each document's Shared list outside the document.
class ShareController : public QObject {
    Q_OBJECT
public:
    ShareController(ProjectWorkspace &workspace, AgentBridge *agent, QWidget &window);

    // A choice made in the popover; set fields are remembered for the document.
    struct Options {
        std::optional<Share::Format> format;
        QString destination;
    };
    struct Destination {
        QString id;
        // "Google Drive", "GitHub", "Live preview deploy".
        QString label;
    };

    // The front document's key in the Shared store: its cloud location, its path, or its tab while untitled.
    static QString documentKey(const ProjectTab &tab);
    QString documentKey() const;
    bool hasDocument() const;
    // Share takes the selection when there is one.
    bool sharesSelection() const;
    // "the selection (2 objects)" or "the artboard".
    QString scopeText() const;
    Share::DocumentShares shares() const;
    // Where it can go now, best first.
    std::vector<Destination> destinations() const;
    // Where one press would go: the remembered choice if it's still there, else the first; empty with nowhere to go.
    QString chosenDestination() const;
    QString destinationLabel(const QString &id) const;
    Share::Format chosenFormat() const;
    // The Live project Share can deploy a preview of, else empty.
    QString liveProject() const;
    // Live is running on a project: Share goes to the site unless the document chose otherwise.
    bool liveRunning() const;

    // Keeps the set fields as the document's choice.
    void remember(const Options &options);
    // One press. Returns why it couldn't start, or empty; the result arrives as the notice.
    QString share(const Options &options = {});
    // Live: the latest deploy's URL when it's of the current commit and nothing is unsaved (unless `fresh`),
    // else a preview deploy.
    QString shareLive(bool fresh = false);
    // The first upload to GitHub asks once; share() waits for this answer.
    bool askingGitHub() const { return m_pending.has_value(); }
    void answerGitHub(bool share);
    // Deletes the shared file, gist or release, then drops it from the list.
    QString unshare(const QString &recordId);
    // Paste client feedback…: Edit with Instruction on what was shared, previewed to keep or discard.
    QString pasteFeedback(const QString &recordId, const QString &feedback);
    // The front document's shares and the Live project's, newest first.
    std::vector<Share::Record> sharedList() const;
    QString copyLink(const QString &link);
    QString openLink(const QString &link);
    QString connectCloud();
    QString connectGitHub();

    // Send to a device: AirDrops the selection (else the artboard) to a nearby Apple device.
    DeviceSend &device() { return m_device; }
    // The device and format last used, remembered across sessions; the device is matched by address, else name.
    std::optional<DeviceSend::Device> lastDevice() const;
    Share::Format deviceFormat() const;
    void setDeviceFormat(Share::Format format);
    // Returns why it couldn't start, or empty; the result arrives as the notice.
    QString sendToDevice(const DeviceSend::Device &device, Share::Format format);

    ShareJob &job() { return m_job; }
    bool running() const { return m_job.running() || m_device.sending(); }
    // Stops whichever is running.
    void cancel();

    // The toast: progress, the link, a failure, or what to connect.
    struct Notice {
        enum class Kind { none, progress, shared, failed, connect };
        Kind kind = Kind::none;
        QString text;
        QString detail;
        QString link;
        // A preview deploy's log, for Details.
        QString log;
    };
    const Notice &notice() const { return m_notice; }
    void dismissNotice();
    // A failure from outside a job, such as Share with nothing open.
    void setFailure(const QString &text);

signals:
    void changed();
    // Ask before the first upload to GitHub.
    void githubQuestion();

private:
    QString start(const Options &options);
    // Renders the selection (else the artboard) into a fresh temporary folder as `baseName.<format>`; returns why not.
    QString render(Share::Format format, const QString &baseName, QString *file);
    void connectDevice();
    void finished(bool ok);
    void setNotice(Notice::Kind kind, const QString &text, const QString &detail = QString(), const QString &link = QString());
    void noticeShared(const Share::Record &record);
    void addRecord(const QString &key, const Share::Record &record);
    void followRenames();
    QString liveKey() const;

    ProjectWorkspace &m_workspace;
    const QPointer<AgentBridge> m_agent;
    QWidget &m_window;
    ShareJob m_job;
    DeviceSend m_device;
    // What the running job is: a share into `m_jobKey`, or the Unshare of `m_unsharing`.
    QString m_jobKey;
    QString m_unsharing;
    QString m_jobDetail;
    QString m_jobDeviceName;
    std::unique_ptr<QTemporaryDir> m_folder;
    std::optional<Options> m_pending;
    bool m_listedRemotes = false;
    Notice m_notice;
    int m_noticeNumber = 0;
    std::map<QUuid, QString> m_keys;
};
