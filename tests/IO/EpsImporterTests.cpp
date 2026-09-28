#include "IO/EpsImporter.h"
#include "PdfFixtures.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
// A fake `gs` that ignores its input and copies a canned PDF to whatever
// -sOutputFile= names, the way OMASTRATOR_RCLONE's fakes stand in for a real
// external program in tests.
QString writeFakeGhostscript(QTemporaryDir &dir, const QByteArray &cannedPdf)
{
    const QString cannedPath = dir.filePath(QStringLiteral("canned.pdf"));
    QFile canned(cannedPath);
    const bool cannedOpened = canned.open(QIODevice::WriteOnly);
    Q_ASSERT(cannedOpened);
    canned.write(cannedPdf);
    canned.close();

    const QString scriptPath = dir.filePath(QStringLiteral("fake-gs"));
    QFile script(scriptPath);
    const bool scriptOpened = script.open(QIODevice::WriteOnly);
    Q_ASSERT(scriptOpened);
    script.write("#!/bin/sh\nfor a in \"$@\"; do case \"$a\" in -sOutputFile=*) out=\"${a#-sOutputFile=}\";; esac; done\ncp \""
                 + cannedPath.toUtf8() + "\" \"$out\"\n");
    script.close();
    QFile::setPermissions(scriptPath,
                           QFile::permissions(scriptPath) | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup);
    return scriptPath;
}

QByteArray onePagePdf()
{
    PdfFixtureBuilder pdf;
    const int contentObj = pdf.addStream("", "0 1 0 rg 0 0 10 10 re f");
    const int pageObj = pdf.nextNumber();
    const int pagesObj = pageObj + 1;
    const int catalogObj = pagesObj + 1;
    pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 20 20] /Contents %2 0 R /Resources << >>")
                    .replace("%1", QByteArray::number(pagesObj))
                    .replace("%2", QByteArray::number(contentObj)));
    pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
    pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
    return pdf.build(catalogObj);
}
}

class EpsImporterTests : public QObject {
    Q_OBJECT

private slots:
    void convertsThroughAFakeGhostscript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = writeFakeGhostscript(dir, onePagePdf());
        qputenv("OMASTRATOR_GS", script.toUtf8());

        const VectorDocument document = EpsImporter::parse("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 20 20\nshowpage\n");
        qunsetenv("OMASTRATOR_GS");

        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().rect.size(), QSizeF(20, 20));
    }

    void dosEpsBinaryHeaderIsStrippedBeforeConversion()
    {
        const QByteArray ps = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 5 5\nshowpage\n";
        QByteArray header(30, char(0));
        header[0] = char(0xC5);
        header[1] = char(0xD0);
        header[2] = char(0xD3);
        header[3] = char(0xC6);
        const auto writeLE = [&](int offset, quint32 value) {
            header[offset] = char(value & 0xff);
            header[offset + 1] = char((value >> 8) & 0xff);
            header[offset + 2] = char((value >> 16) & 0xff);
            header[offset + 3] = char((value >> 24) & 0xff);
        };
        writeLE(4, 30);
        writeLE(8, quint32(ps.size()));
        const QByteArray dosEps = header + ps;

        QTemporaryDir dir;
        const QString script = writeFakeGhostscript(dir, onePagePdf());
        qputenv("OMASTRATOR_GS", script.toUtf8());
        const VectorDocument document = EpsImporter::parse(dosEps);
        qunsetenv("OMASTRATOR_GS");

        QCOMPARE(document.artboards.size(), size_t(1));
    }

    void missingGhostscriptIsAFileError()
    {
        qputenv("OMASTRATOR_GS", "/nonexistent/gs-binary-that-does-not-exist");
        QVERIFY_THROWS_EXCEPTION(FileError, EpsImporter::parse("%!PS-Adobe-3.0\nshowpage\n"));
        qunsetenv("OMASTRATOR_GS");
    }

    void notEpsIsAFileError() { QVERIFY_THROWS_EXCEPTION(FileError, EpsImporter::parse("not postscript at all")); }
};

QTEST_MAIN(EpsImporterTests)
#include "EpsImporterTests.moc"
