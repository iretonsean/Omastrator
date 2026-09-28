#include "IO/FigmaMapperInternal.h"
#include <QLineF>
#include <QtEndian>
#include <cstring>

// Figma's vector network blob (best effort: undocumented, and has changed
// before): vertices, then segments between them with optional tangent handles,
// then regions of loops (closed contours) built from those segments. See
// docs/import/figma.md for the byte layout this was reverse-engineered from.
namespace FigmaMap {
namespace {
struct Reader {
    const QByteArray &data;
    qsizetype pos = 0;
    bool ok = true;

    quint32 u32()
    {
        if (pos + 4 > data.size()) {
            ok = false;
            return 0;
        }
        const quint32 value = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + pos));
        pos += 4;
        return value;
    }

    float f32()
    {
        if (pos + 4 > data.size()) {
            ok = false;
            return 0;
        }
        const quint32 bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + pos));
        pos += 4;
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
};

double scaled(double value, double scale)
{
    return (value == 0 || scale == 0) ? 0.0 : value / scale;
}

VectorPath decodeBlob(const QByteArray &blob, QSizeF size, Context &ctx)
{
    Reader reader{blob};
    const quint32 vertexCount = reader.u32();
    const quint32 segmentCount = reader.u32();
    const quint32 regionCount = reader.u32();

    struct Segment {
        quint32 start = 0, end = 0;
        QPointF tangentStart, tangentEnd;
    };

    std::vector<QPointF> vertices(vertexCount);
    for (quint32 i = 0; i < vertexCount && reader.ok; ++i) {
        reader.u32(); // vertex styleID: corner style, not part of the outline itself.
        vertices[i] = QPointF(scaled(reader.f32(), size.width()), scaled(reader.f32(), size.height()));
    }

    std::vector<Segment> segments(segmentCount);
    for (quint32 i = 0; i < segmentCount && reader.ok; ++i) {
        reader.u32(); // segment styleID: a per-segment stroke style, not in the document model.
        Segment segment;
        segment.start = reader.u32();
        segment.tangentStart = QPointF(scaled(reader.f32(), size.width()), scaled(reader.f32(), size.height()));
        segment.end = reader.u32();
        segment.tangentEnd = QPointF(scaled(reader.f32(), size.width()), scaled(reader.f32(), size.height()));
        segments[i] = segment;
    }

    VectorPath path;
    bool anyRegion = false;
    std::optional<bool> firstWindingIsNonzero;
    bool mixedWindings = false;
    for (quint32 r = 0; r < regionCount && reader.ok; ++r) {
        const quint32 flags = reader.u32();
        const bool nonzero = (flags & 1) != 0;
        if (!firstWindingIsNonzero)
            firstWindingIsNonzero = nonzero;
        else if (*firstWindingIsNonzero != nonzero)
            mixedWindings = true;
        const quint32 loopCount = reader.u32();
        for (quint32 l = 0; l < loopCount && reader.ok; ++l) {
            const quint32 indexCount = reader.u32();
            std::vector<quint32> indices(indexCount);
            for (quint32 i = 0; i < indexCount; ++i)
                indices[i] = reader.u32();
            if (!reader.ok || indices.empty())
                continue;
            bool validLoop = true;
            for (quint32 index : indices) {
                if (index >= segments.size()) {
                    validLoop = false;
                    break;
                }
            }
            std::vector<PathNode> nodes;
            if (validLoop) {
                for (quint32 i = 0; i < indices.size(); ++i) {
                    const Segment &segment = segments[indices[i]];
                    if (segment.start >= vertices.size() || segment.end >= vertices.size()) {
                        validLoop = false;
                        break;
                    }
                    const QPointF startPoint = vertices[segment.start];
                    const QPointF endPoint = vertices[segment.end];
                    const QPointF outHandle = segment.tangentStart.isNull() ? startPoint : startPoint + segment.tangentStart;
                    const QPointF inHandle = segment.tangentEnd.isNull() ? endPoint : endPoint + segment.tangentEnd;
                    if (i == 0) {
                        PathNode node(startPoint);
                        node.out = outHandle;
                        nodes.push_back(node);
                    } else {
                        nodes.back().out = outHandle;
                    }
                    PathNode next(endPoint);
                    next.in = inHandle;
                    nodes.push_back(next);
                }
            }
            if (!validLoop || nodes.size() < 2) {
                ctx.warn(QStringLiteral("Part of a vector shape’s outline couldn’t be read, and was left out."));
                continue;
            }
            Contour contour;
            const bool closes = QLineF(nodes.back().anchor, nodes.front().anchor).length() < 1e-6;
            if (closes) {
                nodes.front().in = nodes.back().in;
                nodes.pop_back();
            }
            contour.nodes = std::move(nodes);
            contour.closed = closes;
            path.contours.push_back(std::move(contour));
            anyRegion = true;
        }
    }
    path.fillRule = firstWindingIsNonzero.value_or(true) ? Qt::WindingFill : Qt::OddEvenFill;
    if (mixedWindings)
        ctx.warn(QStringLiteral("A vector shape mixed fill rules between its regions; the first one was used for the whole shape."));
    if (!anyRegion && segmentCount > 0)
        ctx.warn(QStringLiteral("An open vector path was left out."));
    return path;
}
}

VectorPath decodeVectorNetwork(Context &ctx, const QVariantMap &node)
{
    const QVariantMap vectorData = map(node, "vectorData");
    if (!vectorData.contains(QStringLiteral("vectorNetworkBlob")))
        return {};
    const int blobIndex = vectorData.value(QStringLiteral("vectorNetworkBlob")).toInt();
    if (blobIndex < 0 || blobIndex >= ctx.tree.blobs.size()) {
        ctx.warn(QStringLiteral("A vector shape’s geometry couldn’t be found, and was left out."));
        return {};
    }
    const QVariantMap sizeField = map(vectorData, "normalizedSize");
    const QSizeF size(num(sizeField, "x", 1), num(sizeField, "y", 1));
    const QByteArray bytes = ctx.tree.blobs[blobIndex].toMap().value(QStringLiteral("bytes")).toByteArray();
    return decodeBlob(bytes, size, ctx);
}
}
