#include "IO/PdfImporter.h"
#include "PdfFixtures.h"
#include <QTest>

namespace {
const VectorObject *firstOfKind(const VectorDocument &document, ObjectKind kind)
{
    for (const VectorObject &object : document.objects)
        if (object.kind == kind)
            return &object;
    return nullptr;
}
QList<const VectorObject *> allOfKind(const VectorDocument &document, ObjectKind kind)
{
    QList<const VectorObject *> result;
    for (const VectorObject &object : document.objects)
        if (object.kind == kind)
            result.append(&object);
    return result;
}
}

class PdfImporterTests : public QObject {
    Q_OBJECT

private slots:
    void pathsFillsAndStrokesImport()
    {
        PdfFixtureBuilder pdf;
        const int contentObj = pdf.nextNumber();
        const QByteArray content = "1 0 0 RG 0 0 1 rg 4 w 10 10 50 50 re B";
        pdf.addStream("", content);
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 200 100] /Contents %2 0 R /Resources << >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        QStringList warnings;
        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj), &warnings);
        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().rect.size(), QSizeF(200, 100));
        const VectorObject *path = firstOfKind(document, ObjectKind::path);
        QVERIFY(path);
        QCOMPARE(path->path.nodeCount(), 4);
        QVERIFY(path->hasVisibleFill());
        QVERIFY(path->hasVisibleStroke());
        QCOMPARE(path->fill.color, QColor(0, 0, 255));
        QCOMPARE(path->stroke.paint.color, QColor(255, 0, 0));
        QCOMPARE(path->stroke.width, 4.0);
    }

    void shadingPatternFillBecomesAGradient()
    {
        PdfFixtureBuilder pdf;
        const int functionObj = pdf.addDict("/FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1");
        const int shadingObj =
            pdf.addDict(QByteArray("/ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 100 0] /Function %1 0 R /Extend [true true]")
                            .replace("%1", QByteArray::number(functionObj)));
        const int patternObj = pdf.addDict(QByteArray("/Type /Pattern /PatternType 2 /Shading %1 0 R").replace("%1", QByteArray::number(shadingObj)));
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "/Pattern cs /P1 scn 0 0 100 100 re f");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R "
                                "/Resources << /Pattern << /P1 %3 0 R >> >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj))
                        .replace("%3", QByteArray::number(patternObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        const VectorObject *path = firstOfKind(document, ObjectKind::path);
        QVERIFY(path);
        QCOMPARE(int(path->fill.kind), int(PaintKind::linearGradient));
        QVERIFY(path->fill.stops.size() >= 2);
        QCOMPARE(path->fill.stops.front().color, QColor(255, 0, 0));
        QCOMPARE(path->fill.stops.back().color, QColor(0, 0, 255));
    }

    void clippingNestsFollowingMarksInAClipGroup()
    {
        PdfFixtureBuilder pdf;
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "0 0 50 50 re W n 1 0 0 rg 0 0 100 100 re f");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R /Resources << >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        const QList<const VectorObject *> groups = allOfKind(document, ObjectKind::group);
        QCOMPARE(groups.size(), 1);
        QVERIFY(groups.front()->isClipGroup);
        const auto children = document.children(groups.front()->id);
        QCOMPARE(children.size(), size_t(2));
        QCOMPARE(document.find(children[0])->name, QStringLiteral("Clipping Path"));
        QVERIFY(document.find(children[1])->hasVisibleFill());
    }

    void textUsesToUnicodeBeforeEncoding()
    {
        PdfFixtureBuilder pdf;
        // Map code 0x41 ('A' under WinAnsi) to U+00E9 (e-acute) via ToUnicode,
        // so a match proves ToUnicode won the tie-break PDF.md asks for.
        const int toUnicodeObj =
            pdf.addStream("", "/CIDInit /ProcSet findresource begin\n1 beginbfchar\n<41> <00E9>\nendbfchar\nend");
        const int fontObj = pdf.addDict(QByteArray("/Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding /ToUnicode %1 0 R")
                                             .replace("%1", QByteArray::number(toUnicodeObj)));
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "BT /F1 24 Tf 10 10 Td (A) Tj ET");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R "
                                "/Resources << /Font << /F1 %3 0 R >> >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj))
                        .replace("%3", QByteArray::number(fontObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        const VectorObject *text = firstOfKind(document, ObjectKind::text);
        QVERIFY(text);
        QCOMPARE(text->text.text, QString(QChar(0x00E9)));
    }

    void formXObjectAppliesItsOwnMatrix()
    {
        PdfFixtureBuilder pdf;
        const int formObj = pdf.addStream("/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Matrix [2 0 0 2 5 5]", "0 0 10 10 re f");
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "/Fx1 Do");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R "
                                "/Resources << /XObject << /Fx1 %3 0 R >> >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj))
                        .replace("%3", QByteArray::number(formObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        // Two clip groups (the form's own BBox clip) each holding a path; find the innermost path.
        const QList<const VectorObject *> paths = allOfKind(document, ObjectKind::path);
        const VectorObject *drawn = nullptr;
        for (const VectorObject *candidate : paths)
            if (candidate->name != QStringLiteral("Clipping Path"))
                drawn = candidate;
        QVERIFY(drawn);
        // Matrix [2 0 0 2 5 5] on a 10x10 rect at the origin: 20x20, offset by (5,5).
        const QRectF bounds = drawn->path.bounds();
        QCOMPARE(bounds.width(), 20.0);
        QCOMPARE(bounds.height(), 20.0);
    }

    void imageWithSoftMaskGetsAlpha()
    {
        PdfFixtureBuilder pdf;
        // A 2x1 opaque-then-transparent soft mask (grayscale, 8 bit): white then black.
        const int smaskObj = pdf.addStream("/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8",
                                            QByteArray::fromHex("FF00"));
        // A 2x1 solid red base image.
        const int realImageObj = pdf.addStream(
            QByteArray("/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask %1 0 R")
                .replace("%1", QByteArray::number(smaskObj)),
            QByteArray::fromHex("FF0000FF0000"));
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "q 100 0 0 100 0 0 cm /Im1 Do Q");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R "
                                "/Resources << /XObject << /Im1 %3 0 R >> >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj))
                        .replace("%3", QByteArray::number(realImageObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        const VectorObject *image = firstOfKind(document, ObjectKind::image);
        QVERIFY(image);
        QCOMPARE(image->image.width(), 2);
        QVERIFY(image->image.pixelColor(0, 0).alpha() > 200);
        QVERIFY(image->image.pixelColor(1, 0).alpha() < 50);
    }

    void twoPagesBecomeTwoArtboards()
    {
        PdfFixtureBuilder pdf;
        const int content1 = pdf.addStream("", "0 0 1 rg 0 0 10 10 re f");
        const int content2 = pdf.addStream("", "0 1 0 rg 0 0 10 10 re f");
        const int page1 = pdf.nextNumber();
        const int page2 = page1 + 1;
        const int pagesObj = page2 + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 50 50] /Contents %2 0 R /Resources << >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(content1)));
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 80 60] /Contents %2 0 R /Resources << >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(content2)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R %2 0 R] /Count 2")
                        .replace("%1", QByteArray::number(page1))
                        .replace("%2", QByteArray::number(page2)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        QCOMPARE(document.artboards.size(), size_t(2));
        QCOMPARE(document.artboards[0].rect.size(), QSizeF(50, 50));
        QCOMPARE(document.artboards[1].rect.size(), QSizeF(80, 60));
        QVERIFY(document.artboards[1].rect.left() >= document.artboards[0].rect.right());
        QCOMPARE(document.layers().size(), size_t(2));
    }

    void hiddenOcgImportsAsAHiddenLayer()
    {
        PdfFixtureBuilder pdf;
        const int ocgObj = pdf.addDict("/Type /OCG /Name (Notes)");
        const int ocPropsObj = pdf.addDict(QByteArray("/OCGs [%1 0 R] /D << /OFF [%1 0 R] >>").replace("%1", QByteArray::number(ocgObj)));
        const int contentObj = pdf.nextNumber();
        pdf.addStream("", "/OC /MC0 BDC 0 0 1 rg 0 0 10 10 re f EMC");
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 50 50] /Contents %2 0 R "
                                "/Resources << /Properties << /MC0 %3 0 R >> >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj))
                        .replace("%3", QByteArray::number(ocgObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R /OCProperties %2 0 R")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(ocPropsObj)));

        const VectorDocument document = PdfImporter::parse(pdf.build(catalogObj));
        bool found = false;
        for (const QUuid &id : document.layers()) {
            const VectorObject *layer = document.find(id);
            if (layer->name == QStringLiteral("Notes")) {
                found = true;
                QVERIFY(!layer->isVisible);
            }
        }
        QVERIFY(found);
    }

    void xrefStreamWithObjectStreamLoads()
    {
        // Objects 1 (Catalog), 2 (Pages), 3 (Page), 4 (Content stream, must
        // stay uncompressed/direct) live inside one object stream (object 5);
        // object 6 is the cross-reference stream itself, both stored raw
        // (no /Filter) to avoid hand-computing compressed bytes.
        const QByteArray content = "0 0 1 rg 0 0 10 10 re f";
        const QByteArray contentObjBody = "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream";

        QByteArray objStmData;
        QList<int> headerPairs; // objNum, offset
        QList<QByteArray> bodies{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 50 50] /Contents 4 0 R /Resources << >> >>",
        };
        QList<int> objectNumbers{1, 2, 3};
        QByteArray objects;
        QList<int> offsets;
        for (const QByteArray &body : bodies) {
            offsets.append(int(objects.size()));
            objects += body;
        }
        QByteArray header;
        for (int i = 0; i < objectNumbers.size(); ++i)
            header += QByteArray::number(objectNumbers[i]) + " " + QByteArray::number(offsets[i]) + " ";
        objStmData = header + objects;

        QByteArray out = "%PDF-1.5\n";
        QList<int> byteOffsets(7, 0);
        // Object 4: the content stream, a normal indirect object (object streams can't hold streams).
        byteOffsets[4] = int(out.size());
        out += "4 0 obj\n" + contentObjBody + "\nendobj\n";
        // Object 5: the object stream holding objects 1-3.
        byteOffsets[5] = int(out.size());
        out += "5 0 obj\n<< /Type /ObjStm /N 3 /First " + QByteArray::number(header.size()) + " /Length "
            + QByteArray::number(objStmData.size()) + " >>\nstream\n" + objStmData + "\nendstream\nendobj\n";
        const int xrefObjOffset = int(out.size());

        // The xref stream itself (object 6): W [1 2 2], one row per object
        // 0-6 (2-byte fields so a byte offset never has to be truncated).
        // Type 0 = free, type 1 = {offset}, type 2 = {objStm number, index}.
        QByteArray xrefRows;
        const auto row = [&](uchar type, int a, int b) {
            xrefRows += char(type);
            xrefRows += char((a >> 8) & 0xff);
            xrefRows += char(a & 0xff);
            xrefRows += char((b >> 8) & 0xff);
            xrefRows += char(b & 0xff);
        };
        row(0, 0, 0); // object 0, free
        row(2, 5, 0); // object 1 -> in objstm 5, index 0
        row(2, 5, 1); // object 2 -> in objstm 5, index 1
        row(2, 5, 2); // object 3 -> in objstm 5, index 2
        row(1, byteOffsets[4], 0); // object 4
        row(1, byteOffsets[5], 0); // object 5
        row(1, xrefObjOffset, 0); // object 6 (itself)
        out += "6 0 obj\n<< /Type /XRef /Size 7 /W [1 2 2] /Root 1 0 R /Length " + QByteArray::number(xrefRows.size()) + " >>\nstream\n"
            + xrefRows + "\nendstream\nendobj\n";
        out += "startxref\n" + QByteArray::number(xrefObjOffset) + "\n%%EOF";

        QStringList warnings;
        const VectorDocument document = PdfImporter::parse(out, &warnings);
        QCOMPARE(document.artboards.size(), size_t(1));
        const VectorObject *path = firstOfKind(document, ObjectKind::path);
        QVERIFY(path);
        QVERIFY(path->hasVisibleFill());
    }

    void encryptedFileIsRefused()
    {
        PdfFixtureBuilder pdf;
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 50 50]").replace("%1", QByteArray::number(pagesObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
        const int encryptObj = pdf.addDict("/Filter /Standard /V 1 /R 2 /O <0000000000000000000000000000000000000000000000000000000000000000> "
                                            "/U <0000000000000000000000000000000000000000000000000000000000000000> /P -44");

        const QByteArray bytes = pdf.build(catalogObj, QByteArray("/Encrypt %1 0 R").replace("%1", QByteArray::number(encryptObj)));
        QVERIFY_THROWS_EXCEPTION(FileError, PdfImporter::parse(bytes));
    }

    void notAPdfIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, PdfImporter::parse("hello, world"));
        QVERIFY_THROWS_EXCEPTION(FileError, PdfImporter::read(QStringLiteral("/nonexistent/file.pdf")));
    }
};

QTEST_MAIN(PdfImporterTests)
#include "PdfImporterTests.moc"
