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
// What Illustrator writes for "saved without PDF Content": a page that draws its
// own notice as text, over a path or two.
QByteArray placeholderPagePdf()
{
    PdfFixtureBuilder pdf;
    const int fontObj = pdf.addDict("/Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding");
    const int contentObj = pdf.addStream("",
                                          "0.5 g 10 10 100 50 re f\n"
                                          "BT /F1 8 Tf 10 100 Td (This is an Adobe\256 Illustrator\256 File that was saved without PDF) Tj\n"
                                          "0 -10 Td (Content. To place or open this file in other applications, it should be re-saved) Tj\n"
                                          "0 -10 Td (from Adobe Illustrator with the \"Create PDF Compatible File\" option turned on.) Tj ET");
    const int pageObj = pdf.nextNumber();
    const int pagesObj = pageObj + 1;
    const int catalogObj = pagesObj + 1;
    pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 200 200] /Contents %2 0 R /Resources << /Font << /F1 %3 0 R >> >>")
                    .replace("%1", QByteArray::number(pagesObj))
                    .replace("%2", QByteArray::number(contentObj))
                    .replace("%3", QByteArray::number(fontObj)));
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

    void aiWithAnEmptyArtboardStillImports()
    {
        const VectorDocument document = AiImporter::parse(emptyPagePdf());
        QCOMPARE(document.artboards.size(), size_t(1));
    }

    void placeholderAiWithNoPdfContentIsRefused()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, AiImporter::parse(placeholderPagePdf()));
        try {
            AiImporter::parse(placeholderPagePdf());
            QFAIL("expected FileError");
        } catch (const FileError &error) {
            QVERIFY(error.message().contains(QStringLiteral("PDF Compatible")));
        }
    }

    void notAnAiFileIsAFileError() { QVERIFY_THROWS_EXCEPTION(FileError, AiImporter::parse("not a document at all")); }
};

QTEST_MAIN(AiImporterTests)
#include "AiImporterTests.moc"
