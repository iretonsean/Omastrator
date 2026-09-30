#pragma once
#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <memory>

class QTemporaryDir;
class QTimer;

// Turns a run of JPEG pictures into a file (docs/MOTION.md, section 8): an MP4 or a GIF through ffmpeg, or a numbered PNG
// sequence through Qt when there is no ffmpeg. The pictures go to ffmpeg as they arrive, so the encode keeps pace with the
// recording. Nothing is written to `path` before start(), and a failure removes what was written.
class FrameRecorder : public QObject {
    Q_OBJECT
public:
    enum class Format { mp4, gif, pngFrames };
    struct Options {
        // The file; for pngFrames, a new folder.
        QString path;
        Format format = Format::mp4;
        int fps = 30;
    };

    explicit FrameRecorder(QObject *parent = nullptr);
    // A recording still running is aborted, so no half file stays.
    ~FrameRecorder() override;

    // $OMASTRATOR_FFMPEG, else `ffmpeg` on PATH; empty when there is none.
    static QString ffmpeg();
    // What the menu says without ffmpeg.
    static QString ffmpegHint();

    // Returns why it can't start, or empty.
    QString start(const Options &options);
    // One picture, as JPEG bytes. Returns why the recording has ended (ffmpeg stopped, a file can't be written), or empty.
    QString add(const QByteArray &jpeg);
    int frames() const { return m_frames; }
    // Bytes ffmpeg hasn't taken yet: a caller that runs faster than the encoder waits for this to fall.
    qint64 pending() const;
    bool active() const { return m_state == State::recording || m_state == State::finishing; }
    // Ends the file where it is; finished() says how it went. With no picture taken, nothing is kept.
    void finish();
    // Ends it and removes what was written. No signal.
    void abort();

signals:
    // Empty when the file is complete, else "Couldn't record: <ffmpeg's last line>", with the partial file removed.
    void finished(const QString &error);

private:
    enum class State { idle, recording, finishing, done };
    QString failure() const;
    void removeOutput();
    void encodingEnded(int exitCode, QProcess::ExitStatus status);
    void startPass(int pass);
    void end(const QString &error);
    QStringList mp4Arguments() const;
    QStringList gifArguments(int pass, const QString &folder) const;

    Options m_options;
    State m_state = State::idle;
    int m_frames = 0;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QTemporaryDir> m_temporary;
    QString m_program;
    QByteArray m_stderr;
    // ffmpeg ended while pictures were still coming.
    bool m_died = false;
    QTimer *m_deadline = nullptr;
};
