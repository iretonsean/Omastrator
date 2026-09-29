#include "IO/PdfFilters.h"
#include "IO/PdfImporter.h"
#include "PdfFixtures.h"
#include <QElapsedTimer>
#include <QTest>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

// Tiny hostile or corrupt files. Each one must finish quickly and either throw
// FileError or return a document (usually with a warning): never hang, exhaust
// memory or overflow the stack.
namespace {
constexpr qint64 timeLimitMs = 20'000;

struct Outcome {
    bool threw = false;
    QStringList warnings;
    VectorDocument document;
    qint64 elapsedMs = 0;
};

Outcome importPdf(const QByteArray &data)
{
    Outcome outcome;
    QElapsedTimer timer;
    timer.start();
    try {
        outcome.document = PdfImporter::parse(data, &outcome.warnings);
    } catch (const FileError &) {
        outcome.threw = true;
    }
    outcome.elapsedMs = timer.elapsed();
    return outcome;
}

bool hasWarning(const Outcome &outcome, const QString &fragment)
{
    for (const QString &warning : outcome.warnings)
        if (warning.contains(fragment, Qt::CaseInsensitive))
            return true;
    return false;
}

// A one-page PDF. `objectsBefore` have already been added to `pdf`, so the
// resources string can point at them by number.
QByteArray finishPage(PdfFixtureBuilder &pdf, const QByteArray &content, const QByteArray &resources = "<< >>")
{
    const int contentObj = pdf.addStream("", content);
    const int pageObj = pdf.nextNumber();
    const int pagesObj = pageObj + 1;
    const int catalogObj = pagesObj + 1;
    pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R /Resources %3")
                    .replace("%1", QByteArray::number(pagesObj))
                    .replace("%2", QByteArray::number(contentObj))
                    .replace("%3", resources));
    pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
    pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
    return pdf.build(catalogObj);
}

QByteArray pageWithContent(const QByteArray &content)
{
    PdfFixtureBuilder pdf;
    return finishPage(pdf, content);
}

int pathCount(const VectorDocument &document)
{
    int count = 0;
    for (const VectorObject &object : document.objects)
        if (object.kind == ObjectKind::path)
            ++count;
    return count;
}

#ifdef OMASTRATOR_HAVE_ZLIB
QByteArray deflate(const QByteArray &input, int repeat = 1)
{
    z_stream stream{};
    deflateInit(&stream, Z_BEST_COMPRESSION);
    QByteArray out;
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    for (int i = 0; i < repeat; ++i) {
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
        stream.avail_in = uInt(input.size());
        const int flush = i == repeat - 1 ? Z_FINISH : Z_NO_FLUSH;
        do {
            stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
            stream.avail_out = uInt(chunk.size());
            deflate(&stream, flush);
            out.append(chunk.constData(), chunk.size() - int(stream.avail_out));
        } while (stream.avail_out == 0);
    }
    deflateEnd(&stream);
    return out;
}
#endif
}

class PdfHostileInputTests : public QObject {
    Q_OBJECT

private slots:
    void strayParenInContentStreamDoesNotHang()
    {
        const Outcome outcome = importPdf(pageWithContent("q ) Q ) ) ) 0 0 10 10 re f"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QCOMPARE(pathCount(outcome.document), 1);
    }

    void strayParenInTrailerAndArraysDoesNotHang()
    {
        QByteArray data = pageWithContent("0 0 10 10 re f");
        data.replace("/Size", "/Junk [ ) ) ] /Size");
        data.replace("/Type /Catalog", "/Type /Catalog /X [ ) ] /Y << /Z ) >>");
        const Outcome outcome = importPdf(data);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void longRunOfStrayClosersDoesNotOverflowTheStack()
    {
        const Outcome outcome = importPdf(pageWithContent(QByteArray("> ").repeated(400'000) + " 0 0 10 10 re f"));
        QVERIFY(!outcome.threw);
        QCOMPARE(pathCount(outcome.document), 1);
    }

    void deeplyNestedArraysInContentAndObjects()
    {
        // ~100 KB of "[" would recurse 100k frames without a depth cap.
        const QByteArray deep = QByteArray("[").repeated(100'000);
        PdfFixtureBuilder pdf;
        pdf.add(deep);
        const Outcome inObject = importPdf(finishPage(pdf, "0 0 10 10 re f"));
        QVERIFY(inObject.elapsedMs < timeLimitMs);

        const Outcome inContent = importPdf(pageWithContent(deep + " 0 0 10 10 re f"));
        QVERIFY(!inContent.threw);
        QVERIFY(inContent.elapsedMs < timeLimitMs);

        const Outcome deepDicts = importPdf(pageWithContent(QByteArray("<< /A ").repeated(100'000) + " 0 0 10 10 re f"));
        QVERIFY(deepDicts.elapsedMs < timeLimitMs);
    }

    void aChainOfIndirectLengthsDoesNotOverflowTheStack()
    {
        constexpr int chain = 30'000;
        PdfFixtureBuilder pdf;
        for (int i = 1; i <= chain; ++i)
            pdf.add("<< /Length " + QByteArray::number(i + 1) + " 0 R >>\nstream\nabc\nendstream");
        pdf.add("<< /Length 5 >>\nstream\nabc\nendstream");
        // The page's content stream is the head of the chain.
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 10 10] /Contents 1 0 R").replace("%1", QByteArray::number(pagesObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
        const Outcome outcome = importPdf(pdf.build(catalogObj));
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void aVeryDeepPageTreeIsCutOff()
    {
        constexpr int levels = 20'000;
        PdfFixtureBuilder pdf;
        for (int i = 1; i <= levels; ++i)
            pdf.addDict("/Type /Pages /Kids [" + QByteArray::number(i + 1) + " 0 R] /Count 1");
        pdf.addDict("/Type /Page /MediaBox [0 0 10 10]");
        const int catalogObj = pdf.addDict("/Type /Catalog /Pages 1 0 R");
        const Outcome outcome = importPdf(pdf.build(catalogObj));
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void selfReferentialIndexedColorSpace()
    {
        PdfFixtureBuilder pdf;
        const int cs = pdf.nextNumber();
        pdf.add("[/Indexed " + QByteArray::number(cs) + " 0 R 1 <ff00ff00ff00>]");
        const Outcome outcome = importPdf(finishPage(pdf, "/CS0 cs 0 sc 0 0 10 10 re f", "<< /ColorSpace << /CS0 " + QByteArray::number(cs) + " 0 R >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void iccAlternateThatPointsBackAtItself()
    {
        PdfFixtureBuilder pdf;
        const int stream = pdf.nextNumber();
        pdf.add("<< /N 0 /Alternate [/ICCBased " + QByteArray::number(stream) + " 0 R] /Length 0 >>\nstream\n\nendstream");
        const Outcome outcome = importPdf(finishPage(pdf, "/CS0 cs 0 0 0 sc 0 0 10 10 re f",
                                                      "<< /ColorSpace << /CS0 [/ICCBased " + QByteArray::number(stream) + " 0 R] >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void selfReferentialStitchingFunction()
    {
        PdfFixtureBuilder pdf;
        const int function = pdf.nextNumber();
        pdf.addDict("/FunctionType 3 /Domain [0 1] /Functions [" + QByteArray::number(function) + " 0 R " + QByteArray::number(function)
                    + " 0 R] /Bounds [0.5] /Encode [0 1 0 1]");
        const int arrayFunction = pdf.nextNumber();
        pdf.add("[" + QByteArray::number(arrayFunction) + " 0 R]");
        const QByteArray sep = "[/Separation /Spot /DeviceRGB " + QByteArray::number(function) + " 0 R]";
        const QByteArray sep2 = "[/Separation /Spot /DeviceRGB " + QByteArray::number(arrayFunction) + " 0 R]";
        const Outcome outcome = importPdf(finishPage(pdf, "/A cs 1 sc 0 0 10 10 re f /B cs 1 sc 20 0 10 10 re f",
                                                      "<< /ColorSpace << /A " + sep + " /B " + sep2 + " >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void postScriptFunctionBombs()
    {
        PdfFixtureBuilder pdf;
        // copy doubling the stack, and blocks nested 100k deep.
        QByteArray program = "{ dup dup dup ";
        for (int i = 0; i < 40; ++i)
            program += "3 copy 6 copy 12 copy 24 copy 48 copy 96 copy 192 copy ";
        program += "}";
        const int copyBomb = pdf.addStream("/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]", program);
        const int nestBomb = pdf.addStream("/FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1]", QByteArray("{").repeated(100'000));
        const QByteArray a = "[/Separation /A /DeviceRGB " + QByteArray::number(copyBomb) + " 0 R]";
        const QByteArray b = "[/Separation /B /DeviceRGB " + QByteArray::number(nestBomb) + " 0 R]";
        const Outcome outcome = importPdf(finishPage(pdf, "/A cs 1 sc 0 0 10 10 re f /B cs 1 sc 20 0 10 10 re f",
                                                      "<< /ColorSpace << /A " + a + " /B " + b + " >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void sampledFunctionWithAbsurdSizesAndBits()
    {
        PdfFixtureBuilder pdf;
        QByteArray sizes;
        for (int i = 0; i < 40; ++i)
            sizes += "2 ";
        const int manyInputs = pdf.addStream("/FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [" + sizes + "] /BitsPerSample 8", "abcd");
        const int hugeBits = pdf.addStream("/FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] /BitsPerSample 1000000000", "abcd");
        const QByteArray a = "[/Separation /A /DeviceRGB " + QByteArray::number(manyInputs) + " 0 R]";
        const QByteArray b = "[/Separation /B /DeviceRGB " + QByteArray::number(hugeBits) + " 0 R]";
        const Outcome outcome = importPdf(finishPage(pdf, "/A cs 1 sc 0 0 10 10 re f /B cs 1 sc 20 0 10 10 re f",
                                                      "<< /ColorSpace << /A " + a + " /B " + b + " >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void imageThatIsItsOwnSoftMask()
    {
        PdfFixtureBuilder pdf;
        const int image = pdf.nextNumber();
        pdf.add("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 /SMask " + QByteArray::number(image)
                + " 0 R /Length 4 >>\nstream\nabcd\nendstream");
        const Outcome outcome = importPdf(finishPage(pdf, "q 10 0 0 10 0 0 cm /Im0 Do Q", "<< /XObject << /Im0 " + QByteArray::number(image) + " 0 R >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
    }

    void formFanOutIsCutOffWithAWarning()
    {
        // Each form draws the next one 50 times, 12 levels down: 50^12 runs without a total budget.
        constexpr int levels = 12;
        PdfFixtureBuilder pdf;
        const int firstForm = pdf.nextNumber();
        for (int i = 0; i < levels; ++i) {
            QByteArray body;
            if (i + 1 < levels)
                body = QByteArray("/F Do ").repeated(50);
            else
                body = "0 0 1 1 re f";
            const QByteArray resources = i + 1 < levels ? "/Resources << /XObject << /F " + QByteArray::number(firstForm + i + 1) + " 0 R >> >>" : "";
            pdf.addStream("/Type /XObject /Subtype /Form /BBox [0 0 100 100] " + resources, body);
        }
        const Outcome outcome = importPdf(finishPage(pdf, "/F Do", "<< /XObject << /F " + QByteArray::number(firstForm) + " 0 R >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QVERIFY(hasWarning(outcome, QStringLiteral("too complex")));
        qInfo() << "form fan-out:" << outcome.elapsedMs << "ms," << outcome.document.objects.size() << "objects";
    }

    void flatFloodOfTinyPathsHitsTheObjectCap()
    {
        // 300k tiny paths in one stream: far past the object cap, and insert must stay linear to get there.
        const QByteArray content = QByteArray("0 0 1 1 re f ").repeated(300'000);
        const Outcome outcome = importPdf(pageWithContent(content));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QVERIFY(hasWarning(outcome, QStringLiteral("too complex")));
        QVERIFY(pathCount(outcome.document) > 1000);
        QVERIFY(pathCount(outcome.document) < 300'000);
        qInfo() << "flat flood:" << outcome.elapsedMs << "ms," << pathCount(outcome.document) << "paths";
    }

    void hugeImageDimensionsAreRefused()
    {
        PdfFixtureBuilder pdf;
        const int image = pdf.nextNumber();
        pdf.add("<< /Type /XObject /Subtype /Image /Width 60000 /Height 60000 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 0 >>\nstream\n\nendstream");
        const Outcome outcome = importPdf(finishPage(pdf, "q 10 0 0 10 0 0 cm /Im0 Do Q", "<< /XObject << /Im0 " + QByteArray::number(image) + " 0 R >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QVERIFY(hasWarning(outcome, QStringLiteral("too large")));
        for (const VectorObject &object : outcome.document.objects)
            QVERIFY(object.kind != ObjectKind::image);
    }

    void imageWhoseDataIsFarShorterThanItsHeaderIsRefused()
    {
        PdfFixtureBuilder pdf;
        const int image = pdf.nextNumber();
        pdf.add("<< /Type /XObject /Subtype /Image /Width 8000 /Height 8000 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length 4 >>\nstream\nabcd\nendstream");
        const Outcome outcome = importPdf(finishPage(pdf, "q 10 0 0 10 0 0 cm /Im0 Do Q", "<< /XObject << /Im0 " + QByteArray::number(image) + " 0 R >> >>"));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QVERIFY(hasWarning(outcome, QStringLiteral("too short")));
    }

    void saveStateFloodIsCapped()
    {
        const Outcome outcome = importPdf(pageWithContent(QByteArray("q ").repeated(300'000) + "0 0 10 10 re f " + QByteArray("Q ").repeated(300'000)));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QVERIFY(hasWarning(outcome, QStringLiteral("nested")));
        QCOMPARE(pathCount(outcome.document), 1);
    }

    void markedContentFloodIsCapped()
    {
        const Outcome outcome = importPdf(pageWithContent(QByteArray("/Span BMC ").repeated(300'000) + "0 0 10 10 re f " + QByteArray("EMC ").repeated(300'000)));
        QVERIFY(!outcome.threw);
        QVERIFY(outcome.elapsedMs < timeLimitMs);
        QCOMPARE(pathCount(outcome.document), 1);
    }

    void applyPredictorSurvivesOverflowingParameters()
    {
        const QByteArray data(64, 'x');
        QVERIFY(PdfFilters::applyPredictor(data, 15, 1 << 20, 1 << 20, 1 << 20).size() <= data.size());
        QVERIFY(PdfFilters::applyPredictor(data, 12, 1 << 30, 32, 1 << 30).size() <= data.size());
        QVERIFY(PdfFilters::applyPredictor(data, 2, -5, -5, -5).size() <= data.size());
    }

    void runLengthBombIsCapped()
    {
        // Two bytes expand to 128: 3 MB in, 192 MB out without a cap.
        QStringList warnings;
        const QByteArray out = PdfFilters::decodeRunLength(QByteArray("\x81\x41").repeated(1'600'000), &warnings);
        QVERIFY(out.size() <= PdfFilters::maximumDecodedSize + 256);
        QVERIFY(!warnings.isEmpty());
    }

#ifdef OMASTRATOR_HAVE_ZLIB
    void flateBombIsCapped()
    {
        // 300 MB of zeros deflates to ~300 KB.
        const QByteArray compressed = deflate(QByteArray(1024 * 1024, '\0'), 300);
        QVERIFY(compressed.size() < 2 * 1024 * 1024);
        QStringList warnings;
        const QByteArray out = PdfFilters::inflate(compressed, &warnings);
        QVERIFY(out.size() <= PdfFilters::maximumDecodedSize);
        QVERIFY(out.size() > 1024 * 1024);
        QVERIFY(!warnings.isEmpty());
    }

    void truncatedFlateStreamKeepsItsPartialOutput()
    {
        const QByteArray plain = QByteArray("0 0 1 rg 0 0 50 50 re f ").repeated(200);
        QByteArray compressed = deflate(plain);
        compressed.chop(compressed.size() / 3); // no end marker and no checksum either
        QStringList warnings;
        const QByteArray out = PdfFilters::inflate(compressed, &warnings);
        QVERIFY(!out.isEmpty());
        QVERIFY(plain.startsWith(out));
        QVERIFY(!warnings.isEmpty());

        // Only the checksum missing: everything decodes and the damage is still reported.
        QByteArray noChecksum = deflate(plain);
        noChecksum.chop(4);
        warnings.clear();
        QCOMPARE(PdfFilters::inflate(noChecksum, &warnings), plain);
        QVERIFY(!warnings.isEmpty());

        QStringList cleanWarnings;
        QCOMPARE(PdfFilters::inflate(deflate(plain), &cleanWarnings), plain);
        QVERIFY(cleanWarnings.isEmpty());
    }

    void aTruncatedContentStreamStillPaintsWhatItHolds()
    {
        const QByteArray plain = QByteArray("0 0 1 rg 0 0 50 50 re f\n") + QByteArray("% padding padding padding\n").repeated(400) + "1 0 0 rg 50 50 10 10 re f";
        QByteArray compressed = deflate(plain);
        compressed.chop(8);
        PdfFixtureBuilder pdf;
        const int contentObj = pdf.addStream("/Filter /FlateDecode", compressed);
        const int pageObj = pdf.nextNumber();
        const int pagesObj = pageObj + 1;
        const int catalogObj = pagesObj + 1;
        pdf.addDict(QByteArray("/Type /Page /Parent %1 0 R /MediaBox [0 0 100 100] /Contents %2 0 R /Resources << >>")
                        .replace("%1", QByteArray::number(pagesObj))
                        .replace("%2", QByteArray::number(contentObj)));
        pdf.addDict(QByteArray("/Type /Pages /Kids [%1 0 R] /Count 1").replace("%1", QByteArray::number(pageObj)));
        pdf.addDict(QByteArray("/Type /Catalog /Pages %1 0 R").replace("%1", QByteArray::number(pagesObj)));
        const Outcome outcome = importPdf(pdf.build(catalogObj));
        QVERIFY(!outcome.threw);
        QVERIFY(pathCount(outcome.document) >= 1);
        QVERIFY(hasWarning(outcome, QStringLiteral("cut short")));
    }
#endif
};

QTEST_MAIN(PdfHostileInputTests)
#include "PdfHostileInputTests.moc"
