#include "IO/FrameRecorder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>

namespace {
// ffmpeg gets this long after the last picture to finish; a GIF's two passes get it each.
constexpr int finishMs = 120'000;

QString lastLine(const QByteArray &text)
{
    const QStringList lines = QString::fromUtf8(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it)
        if (!it->trimmed().isEmpty())
            return it->trimmed();
    return {};
}
}

FrameRecorder::FrameRecorder(QObject *parent) : QObject(parent) {}

FrameRecorder::~FrameRecorder()
{
    abort();
}

QString FrameRecorder::ffmpeg()
{
    const QString given = qEnvironmentVariable("OMASTRATOR_FFMPEG");
    if (!given.isEmpty())
        return QFileInfo(given).isExecutable() ? given : QString();
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QString FrameRecorder::ffmpegHint()
{
    return QStringLiteral("Recording needs ffmpeg. Install it with sudo pacman -S ffmpeg.");
}

QStringList FrameRecorder::mp4Arguments() const
{
    // The pictures are JPEG, said outright: ffmpeg can't always tell from a pipe. yuv420p needs even sides, so an odd picture
    // loses its last row or column, and JPEG's full colour range becomes the limited range players expect.
    return {QStringLiteral("-y"),        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),  QStringLiteral("error"),
            QStringLiteral("-f"),        QStringLiteral("image2pipe"),   QStringLiteral("-framerate"), QString::number(m_options.fps),
            QStringLiteral("-c:v"),      QStringLiteral("mjpeg"),        QStringLiteral("-i"),         QStringLiteral("-"),
            QStringLiteral("-vf"),       QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2:out_range=tv"),
            QStringLiteral("-c:v"),      QStringLiteral("libx264"),      QStringLiteral("-pix_fmt"),   QStringLiteral("yuv420p"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),   m_options.path};
}

QStringList FrameRecorder::gifArguments(int pass, const QString &folder) const
{
    QStringList arguments{QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                          QStringLiteral("-framerate"), QString::number(m_options.fps), QStringLiteral("-i"), QDir(folder).filePath(QStringLiteral("%05d.jpg"))};
    if (pass == 1)
        return arguments + QStringList{QStringLiteral("-vf"), QStringLiteral("palettegen"), QDir(folder).filePath(QStringLiteral("palette.png"))};
    return arguments + QStringList{QStringLiteral("-i"), QDir(folder).filePath(QStringLiteral("palette.png")), QStringLiteral("-lavfi"), QStringLiteral("paletteuse"),
                                   m_options.path};
}

QString FrameRecorder::start(const Options &options)
{
    if (m_state == State::recording || m_state == State::finishing)
        return QStringLiteral("A recording is already running.");
    m_options = options;
    m_options.fps = std::clamp(options.fps, 1, 120);
    m_frames = 0;
    m_died = false;
    m_stderr.clear();
    m_process.reset();
    m_temporary.reset();
    if (options.path.isEmpty())
        return QStringLiteral("Say where to save the recording.");
    if (options.format == Format::pngFrames) {
        const QFileInfo info(options.path);
        if (info.exists() && (!info.isDir() || !QDir(options.path).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)))
            return QStringLiteral("%1 already exists. Frames go in a new folder.").arg(options.path);
        if (!QDir().mkpath(options.path))
            return QStringLiteral("Couldn't make the folder %1.").arg(options.path);
        m_state = State::recording;
        return {};
    }
    m_program = ffmpeg();
    if (m_program.isEmpty())
        return ffmpegHint();
    if (!QDir().mkpath(QFileInfo(options.path).absolutePath()))
        return QStringLiteral("Couldn't write to %1.").arg(QFileInfo(options.path).absolutePath());
    if (options.format == Format::gif) {
        // The pictures wait in a folder until both passes can read them.
        m_temporary = std::make_unique<QTemporaryDir>(QDir(QFileInfo(options.path).absolutePath()).filePath(QStringLiteral(".omastrator-frames-XXXXXX")));
        if (!m_temporary->isValid())
            return QStringLiteral("Couldn't make a folder for the pictures beside %1.").arg(options.path);
        m_state = State::recording;
        return {};
    }
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_program);
    m_process->setArguments(mp4Arguments());
    m_process->setStandardOutputFile(QProcess::nullDevice());
    connect(m_process.get(), &QProcess::readyReadStandardError, this, [this] { m_stderr += m_process->readAllStandardError(); });
    connect(m_process.get(), &QProcess::finished, this, &FrameRecorder::encodingEnded);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_stderr += m_process->errorString().toUtf8();
            m_died = true;
        }
    });
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        const QString why = m_process->errorString();
        m_process.reset();
        return QStringLiteral("Couldn't start ffmpeg: %1").arg(why);
    }
    m_state = State::recording;
    return {};
}

QString FrameRecorder::failure() const
{
    QString line = lastLine(m_stderr);
    if (line.isEmpty() && m_process)
        line = QStringLiteral("ffmpeg stopped (exit code %1).").arg(m_process->exitCode());
    return QStringLiteral("Couldn't record: %1").arg(line.isEmpty() ? QStringLiteral("ffmpeg stopped.") : line);
}

QString FrameRecorder::add(const QByteArray &jpeg)
{
    if (m_state != State::recording)
        return QStringLiteral("No recording is running.");
    if (jpeg.isEmpty())
        return {};
    if (m_options.format == Format::pngFrames) {
        const QImage picture = QImage::fromData(jpeg, "JPEG");
        const QString name = QDir(m_options.path).filePath(QStringLiteral("frame-%1.png").arg(m_frames + 1, 4, 10, QLatin1Char('0')));
        if (picture.isNull() || !picture.save(name, "PNG"))
            return QStringLiteral("Couldn't write %1.").arg(name);
        ++m_frames;
        return {};
    }
    if (m_options.format == Format::gif) {
        QFile file(QDir(m_temporary->path()).filePath(QStringLiteral("%1.jpg").arg(m_frames + 1, 5, 10, QLatin1Char('0'))));
        if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size())
            return QStringLiteral("Couldn't write a picture beside %1: %2").arg(m_options.path, file.errorString());
        ++m_frames;
        return {};
    }
    if (m_died || m_process->state() != QProcess::Running) {
        m_stderr += m_process->readAllStandardError();
        const QString why = failure();
        removeOutput();
        m_state = State::done;
        return why;
    }
    m_process->write(jpeg);
    ++m_frames;
    return {};
}

qint64 FrameRecorder::pending() const
{
    return m_process ? m_process->bytesToWrite() : 0;
}

void FrameRecorder::removeOutput()
{
    if (m_options.format == Format::pngFrames)
        QDir(m_options.path).removeRecursively();
    else
        QFile::remove(m_options.path);
}

void FrameRecorder::end(const QString &error)
{
    if (m_deadline) {
        m_deadline->stop();
        m_deadline->deleteLater();
        m_deadline = nullptr;
    }
    if (!error.isEmpty())
        removeOutput();
    m_temporary.reset();
    m_state = State::done;
    emit finished(error);
}

void FrameRecorder::finish()
{
    if (m_state != State::recording)
        return;
    if (m_frames == 0) {
        // Nothing to keep: the pictures never came.
        abort();
        m_state = State::done;
        emit finished(QStringLiteral("Couldn't record: no picture was taken."));
        return;
    }
    m_state = State::finishing;
    if (m_options.format == Format::pngFrames) {
        end({});
        return;
    }
    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        if (m_process)
            m_process->kill();
    });
    m_deadline->start(finishMs);
    if (m_options.format == Format::gif) {
        startPass(1);
        return;
    }
    // ffmpeg ended already (it died, or it is quick): the exit status is read through the same slot.
    if (m_process->state() == QProcess::NotRunning) {
        encodingEnded(m_process->exitCode(), m_process->exitStatus());
        return;
    }
    m_process->closeWriteChannel();
}

void FrameRecorder::startPass(int pass)
{
    m_stderr.clear();
    m_process = std::make_unique<QProcess>();
    m_process->setProgram(m_program);
    m_process->setArguments(gifArguments(pass, m_temporary->path()));
    m_process->setStandardOutputFile(QProcess::nullDevice());
    connect(m_process.get(), &QProcess::readyReadStandardError, this, [this] { m_stderr += m_process->readAllStandardError(); });
    connect(m_process.get(), &QProcess::finished, this, [this, pass](int code, QProcess::ExitStatus status) {
        m_stderr += m_process->readAllStandardError();
        if (status != QProcess::NormalExit || code != 0) {
            end(failure());
            return;
        }
        if (pass == 1)
            QTimer::singleShot(0, this, [this] { startPass(2); });
        else
            end({});
    });
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_stderr += m_process->errorString().toUtf8();
            end(failure());
        }
    });
    m_process->start();
}

void FrameRecorder::encodingEnded(int exitCode, QProcess::ExitStatus status)
{
    m_stderr += m_process->readAllStandardError();
    if (m_state == State::finishing) {
        end(status == QProcess::NormalExit && exitCode == 0 ? QString() : failure());
        return;
    }
    // While pictures were still coming: add() reports it, and finish() finds the state.
    m_died = true;
}

void FrameRecorder::abort()
{
    if (m_state != State::recording && m_state != State::finishing)
        return;
    if (m_deadline) {
        m_deadline->stop();
        m_deadline->deleteLater();
        m_deadline = nullptr;
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process.reset();
    }
    removeOutput();
    m_temporary.reset();
    m_state = State::done;
}
