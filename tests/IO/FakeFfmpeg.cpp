// A stand-in for ffmpeg, run through OMASTRATOR_FFMPEG. It records its arguments, counts the JPEG pictures it is given
// (on stdin for `-i -`, else the numbered files a `%05d.jpg` input names), and writes its output file.
//   FAKE_FFMPEG_OUT   a folder: `calls` gets one block per run (its arguments, one per line), `frames-N` the count for run N
//   FAKE_FFMPEG_MODE  fail-at-start (an error, and it exits without reading anything), fail-at-end (it reads everything, then
//                     fails), fail-pass2 (the GIF's palette pass works and the second pass fails)
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <cstdio>

namespace {
void append(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write(bytes);
}

int fail(const char *line)
{
    fprintf(stderr, "%s\n", line);
    return 1;
}

// Every FF D8 FF is a picture's start.
int pictures(const QByteArray &bytes)
{
    int count = 0;
    for (qsizetype at = bytes.indexOf("\xff\xd8\xff"); at >= 0; at = bytes.indexOf("\xff\xd8\xff", at + 3))
        ++count;
    return count;
}
}

int main(int argc, char **argv)
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const QString out = qEnvironmentVariable("FAKE_FFMPEG_OUT");
    const QString mode = qEnvironmentVariable("FAKE_FFMPEG_MODE");
    int run = 1;
    while (QFileInfo::exists(QDir(out).filePath(QStringLiteral("frames-%1").arg(run))) || QFileInfo::exists(QDir(out).filePath(QStringLiteral("started-%1").arg(run))))
        ++run;
    append(QDir(out).filePath(QStringLiteral("started-%1").arg(run)), "x");
    append(QDir(out).filePath(QStringLiteral("calls")), (arguments.join(QLatin1Char('\n')) + QStringLiteral("\n---\n")).toUtf8());
    const QString target = arguments.isEmpty() ? QString() : arguments.last();
    // ffmpeg opens its output at once.
    append(target, "FAKE\n");
    if (mode == QLatin1String("fail-at-start"))
        return fail("Unknown encoder 'libx264'");

    int count = 0;
    const int pipe = arguments.indexOf(QStringLiteral("-i"));
    if (pipe >= 0 && arguments.value(pipe + 1) == QLatin1String("-")) {
        QByteArray all;
        char buffer[65536];
        for (size_t got; (got = fread(buffer, 1, sizeof buffer, stdin)) > 0;)
            all.append(buffer, qsizetype(got));
        count = pictures(all);
    } else {
        // The first numbered input names the folder.
        for (const QString &word : arguments) {
            if (!word.endsWith(QLatin1String("%05d.jpg")))
                continue;
            const QString folder = QFileInfo(word).absolutePath();
            while (QFileInfo::exists(QDir(folder).filePath(QStringLiteral("%1.jpg").arg(count + 1, 5, 10, QLatin1Char('0')))))
                ++count;
            break;
        }
    }
    append(QDir(out).filePath(QStringLiteral("frames-%1").arg(run)), QByteArray::number(count));
    if (mode == QLatin1String("fail-at-end"))
        return fail("Error while encoding: Invalid argument");
    if (mode == QLatin1String("fail-pass2") && arguments.contains(QStringLiteral("paletteuse")))
        return fail("Error initializing filter 'paletteuse'");
    QFile::remove(target);
    append(target, "FAKE frames=" + QByteArray::number(count) + "\n");
    return 0;
}
