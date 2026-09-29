#pragma once
#include "Document/VectorDocument.h"
#include "IO/PdfColorSpace.h"
#include "IO/PdfEncoding.h"
#include "IO/PdfObject.h"
#include <QHash>
#include <QStack>
#include <QStringList>
#include <QTransform>

// The content-stream interpreter: q/Q, path construction and painting,
// clipping, color and ExtGState, text, Form and Image XObjects, shadings and
// marked-content (OCG) layers. One Interpreter is shared across every page of
// an import so OCG layers and font lookups are reused.
namespace Pdf {
class Document;

class Interpreter {
public:
    Interpreter(const Document &document, VectorDocument &target, QStringList *warnings);

    // Runs one page's content stream, inserting its marks as children of
    // `pageLayer` (already positioned by `pageTransform`, which maps the
    // page's own PDF space, CropBox origin included, into the artboard).
    void runPage(const QByteArray &content, const Dict &resources, const QTransform &pageTransform, const QRectF &pageBounds,
                 const QUuid &pageLayer);

private:
    struct GraphicsState {
        QTransform ctm;
        ColorSpace fillSpace = ColorSpace::deviceGray();
        ColorSpace strokeSpace = ColorSpace::deviceGray();
        QList<double> fillComponents{0};
        QList<double> strokeComponents{0};
        QByteArray fillPatternName;
        QByteArray strokePatternName;
        double fillAlpha = 1;
        double strokeAlpha = 1;
        LayerBlendMode blendMode = LayerBlendMode::normal;
        double lineWidth = 1;
        Qt::PenCapStyle cap = Qt::FlatCap;
        Qt::PenJoinStyle join = Qt::MiterJoin;
        double miterLimit = 10;
        QList<double> dashArray;
        QUuid insertionParent; // where painted marks are inserted; changes when a clip group opens
        QRectF clipBounds; // the current clip's device-space bounding box, for `sh`'s unbounded fill

        // Text state (part of the graphics state; also saved/restored by q/Q).
        QString fontFamily = QStringLiteral("Sans Serif");
        QString fontStyle = QStringLiteral("Regular");
        double fontSize = 12;
        std::shared_ptr<Font> font;
        double charSpace = 0;
        double wordSpace = 0;
        double hScale = 100;
        double leading = 0;
        double rise = 0;
        int renderMode = 0;
    };

    struct PendingClip {
        bool active = false;
        Qt::FillRule rule = Qt::WindingFill;
    };

    // One baseline's worth of text (PDF.md: "merge runs on one baseline into
    // point-type lines"), flushed to a single live text object on Td/TD/Tm/T*/ET.
    struct PendingLine {
        bool valid = false;
        QString text;
        QTransform placement;
        std::vector<TextRun> runs;
    };

    const Document &m_document;
    VectorDocument &m_target;
    QStringList *m_warnings;
    QSet<QString> m_warnedOnce;
    QHash<QString, std::shared_ptr<Font>> m_fontCache; // keyed by the font resource's indirect reference
    QHash<int, QUuid> m_ocgLayers; // OCG object number -> its shared top-level layer
    int m_formDepth = 0;
    // Import-wide budgets: nesting depth alone can't stop 50 forms drawing 50 forms drawing…
    qint64 m_operatorCount = 0;
    int m_formRuns = 0;
    bool m_tooComplex = false;
    int m_ignoredMarks = 0; // likewise for BMC/BDC past the nesting cap
    int m_ignoredSaves = 0; // q's dropped at the stack cap, so their matching Q's don't pop a real state

    QStack<GraphicsState> m_stack;
    GraphicsState m_state;
    QPainterPath m_path; // built in document space (CTM already applied per point)
    QPointF m_currentPoint;
    QPointF m_subpathStart;
    // QPainterPath::isEmpty() stays true for a path holding only a moveTo, so
    // this (not isEmpty()) is what l/c/v/y ask before deciding to fall back
    // to a moveTo of their own.
    bool m_hasOpenSubpath = false;
    PendingClip m_pendingClip;

    QTransform m_textMatrix;
    QTransform m_lineMatrix;
    PendingLine m_pendingLine;
    QStack<QUuid> m_ocgRestore;

    void warnOnce(const QString &key, const QString &message);
    void runContent(const QByteArray &content, const Dict &resources);

    // PdfContent.cpp: state, paths, painting, clipping, color, ExtGState, XObjects, marked content.
    void setColorSpace(bool stroking, const Object &nameObject, const Dict &resources);
    void setColor(bool stroking, const QList<Object> &operands);
    void paintPath(bool fill, bool stroke, Qt::FillRule rule, const Dict &resources);
    void applyPendingClip();
    QUuid clipGroupFor(const VectorPath &clipPath, Qt::FillRule rule);
    Paint resolvePaint(bool stroking, const QRectF &bounds, const Dict &resources);
    void applyExtGState(const Object &nameObject, const Dict &resources);
    void doXObject(const Object &nameObject, const Dict &resources);
    void runForm(const Object &formObject, const Dict &resources);
    void runImage(const Object &imageObject, const Dict &resources);
    void runInlineImage(const Dict &dict, const QByteArray &rawData, const Dict &resources);
    void beginMarkedContent(const QList<Object> &operands, const Dict &resources);
    void endMarkedContent();
    bool isOcgHidden(int ocgObjectNumber) const;

    // PdfContent+Text.cpp
    void op_Tf(const QList<Object> &operands, const Dict &resources);
    void showText(const QByteArray &bytes, const Dict &resources);
    void showTextArray(const Array &array, const Dict &resources);
    void appendToLine(const QString &text, const CharacterFormat &format);
    void flushTextLine();
    std::shared_ptr<Font> fontFor(const Dict &resources, const QByteArray &name);

    // PdfContent+Image.cpp
    // `isMask` marks a soft mask or stencil being decoded for another image: it never reads a mask of its own.
    QImage decodeImageObject(const Object &imageObject, const Dict &resources, bool isMask = false);

    // PdfContent+Shading.cpp
    void runShading(const Object &nameObject, const Dict &resources);
    Paint paintFromShading(const Object &shadingObject, const QRectF &targetBounds, const QTransform &shadingToDocument, bool *ok);
};

}
