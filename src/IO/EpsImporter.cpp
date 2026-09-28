#include "IO/EpsImporter.h"
#include "IO/PdfImporter.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
thread_local QStringList lastWarningList;

// A DOS EPS file wraps its PostScript in a binary header (magic C5D0D3C6,
// then little-endian offset and length of the PostScript section).
QByteArray stripDosEpsHeader(const QByteArray &data)
{
    if (data.size() < 30 || uchar(data[0]) != 0xC5 || uchar(data[1]) != 0xD0 || uchar(data[2]) != 0xD3 || uchar(data[3]) != 0xC6)
        return data;
    const auto readUInt32 = [&](int offset) {
        return quint32(uchar(data[offset])) | (quint32(uchar(data[offset + 1])) << 8) | (quint32(uchar(data[offset + 2])) << 16)
            | (quint32(uchar(data[offset + 3])) << 24);
    };
    const quint32 psStart = readUInt32(4);
    const quint32 psLength = readUInt32(8);
    if (qint64(psStart) + qint64(psLength) > data.size())
        return data;
    return data.mid(int(psStart), int(psLength));
}

QString ghostscriptProgram()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_GS");
    return overridden.isEmpty() ? QStringLiteral("gs") : overridden;
}
}

namespace EpsImporter {

bool canRead(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(64);
    return head.startsWith("%!PS-Adobe")
        || (head.size() >= 4 && uchar(head[0]) == 0xC5 && uchar(head[1]) == 0xD0 && uchar(head[2]) == 0xD3 && uchar(head[3]) == 0xC6);
}

VectorDocument parse(const QByteArray &data, QStringList *warnings)
{
    QStringList localWarnings;
    const QByteArray postscript = stripDosEpsHeader(data);
    if (!postscript.startsWith("%!"))
        throw FileError(QStringLiteral("This is not an EPS or PostScript file."));

    const QString program = ghostscriptProgram();
    const bool overridden = !qEnvironmentVariable("OMASTRATOR_GS").isEmpty();
    if (!overridden && QStandardPaths::findExecutable(program).isEmpty())
        throw FileError(QStringLiteral("Ghostscript isn't installed, so this file can't be converted. Install it with: omarchy pkg add ghostscript"));

    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        throw FileError(QStringLiteral("A temporary folder could not be created to convert this file."));
    const QString inputPath = tempDir.filePath(QStringLiteral("input.eps"));
    const QString outputPath = tempDir.filePath(QStringLiteral("output.pdf"));
    {
        QFile input(inputPath);
        if (!input.open(QIODevice::WriteOnly) || input.write(postscript) != postscript.size())
            throw FileError(QStringLiteral("This file could not be prepared for Ghostscript."));
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{
        QStringLiteral("-q"),
        QStringLiteral("-dNOPAUSE"),
        QStringLiteral("-dBATCH"),
        QStringLiteral("-dSAFER"),
        QStringLiteral("-sDEVICE=pdfwrite"),
        QStringLiteral("-dEPSCrop"),
        QStringLiteral("-sOutputFile=%1").arg(outputPath),
        inputPath,
    };
    process.start(program, args);
    if (!process.waitForStarted(5000))
        throw FileError(QStringLiteral("Ghostscript could not be started: %1").arg(process.errorString()));
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(1000);
        throw FileError(QStringLiteral("Ghostscript did not finish converting this file."));
    }
    const QByteArray gsOutput = process.readAll();
    QFile output(outputPath);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !output.exists() || output.size() == 0) {
        qCWarning(lcIO).noquote() << "ghostscript failed:" << gsOutput;
        throw FileError(QStringLiteral("Ghostscript could not convert this file."));
    }
    if (!output.open(QIODevice::ReadOnly))
        throw FileError(QStringLiteral("The converted PDF could not be read."));
    const QByteArray pdfBytes = output.readAll();

    VectorDocument result = PdfImporter::parse(pdfBytes, &localWarnings);
    lastWarningList = localWarnings;
    if (warnings)
        *warnings = localWarnings;
    return result;
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot read" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    const QByteArray data = file.readAll();
    try {
        return parse(data, warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

QStringList lastWarnings()
{
    return lastWarningList;
}

}
