#include "IO/AiImporter.h"
#include "PdfFixtures.h"
#include <QTest>

namespace {
QByteArray onePagePdfWithArt()
{
    PdfFixtureBuilder pdf;
    const int contentObj = pdf.addStream("", "0 0 1 rg 0 0 10 10 re f");
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
QByteArray emptyPagePdf()
{
    PdfFixtureBuilder pdf;
    const int pageObj = pdf.nextNumber();
    const int pagesObj = pageObj + 1;
    const int catalogObj = pagesObj + 1;
    pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 20 20]").replace("%1", QByteArray::number(pagesObj)));
    pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
    pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
    return pdf.build(catalogObj);
}
}

class AiImporterTests : public QObject {
    Q_OBJECT

private slots:
    void modernAiIsReadAsPdf()
    {
        const VectorDocument document = AiImporter::parse(onePagePdfWithArt());
        QCOMPARE(document.artboards.size(), size_t(1));
    }

    void placeholderAiWithNoPdfContentIsRefused()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, AiImporter::parse(emptyPagePdf()));
        try {
            AiImporter::parse(emptyPagePdf());
            QFAIL("expected FileError");
        } catch (const FileError &error) {
            QVERIFY(error.message().contains(QStringLiteral("PDF Compatible")));
        }
    }

    void notAnAiFileIsAFileError() { QVERIFY_THROWS_EXCEPTION(FileError, AiImporter::parse("not a document at all")); }
};

QTEST_MAIN(AiImporterTests)
#include "AiImporterTests.moc"
