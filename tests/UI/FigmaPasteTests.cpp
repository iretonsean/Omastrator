#include "../IO/FigmaKiwiFixtures.h"
#include "Document/EditorSession.h"
#include "UI/FigmaPasteHandler.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

namespace {
void putHtml(const QByteArray &html)
{
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("text/html"), html);
    QApplication::clipboard()->setMimeData(mime);
}

QByteArray figmaHtml(const QByteArray &fig)
{
    return "<meta charset='utf-8'><span data-metadata=\"<!--(figmeta)" + QByteArray("meta").toBase64() + "(/figmeta)-->\"></span><span data-buffer=\"<!--(figma)"
        + fig.toBase64() + "(/figma)-->\"></span>";
}
}

class FigmaPasteTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { FigmaPasteHandler::install(); }

    void canPasteRecognisesFigmaWithoutDecodingIt()
    {
        EditorSession session;
        session.loadDocument(VectorDocument::blank(QSizeF(400, 400)));
        // A marker with a payload that couldn't be decoded: recognised all the same.
        putHtml("<span data-buffer=\"<!--(figma)!!!!(/figma)-->\"></span>");
        QVERIFY(session.canPaste());
        putHtml("<html><body>plain</body></html>");
        QVERIFY(!session.canPaste());
    }

    void pasteReportsWhatItLeftOut()
    {
        EditorSession session;
        session.loadDocument(VectorDocument::blank(QSizeF(400, 400)));
        MinimalFig fig;
        fig.addNode(1, 0, QStringLiteral("FRAME"), QStringLiteral("Kept"));
        fig.addNode(2, 0, QStringLiteral("WIDGET"), QStringLiteral("Dropped"));
        putHtml(figmaHtml(fig.file()));
        QSignalSpy spy(&session, &EditorSession::pasteLeftOut);
        const size_t before = session.document()->objects.size();
        session.paste();
        QCOMPARE(spy.count(), 1);
        const QStringList warnings = spy.takeFirst().at(0).toStringList();
        QVERIFY(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("WIDGET")));
        QVERIFY(session.document()->objects.size() > before);
    }
};

QTEST_MAIN(FigmaPasteTests)
#include "FigmaPasteTests.moc"
