#include "IO/FigmaMapper.h"
#include <algorithm>

namespace FigmaMap {

Guid guidKey(const QVariant &guidField)
{
    const QVariantMap guid = guidField.toMap();
    if (guid.isEmpty())
        return {};
    bool sessionOk = false, localOk = false;
    const quint32 session = guid.value(QStringLiteral("sessionID")).toUInt(&sessionOk);
    const quint32 local = guid.value(QStringLiteral("localID")).toUInt(&localOk);
    if (!sessionOk || !localOk || session == 0xffffffffu || local == 0xffffffffu)
        return {};
    return QString::number(session) + QLatin1Char(':') + QString::number(local);
}

QString str(const QVariantMap &node, const char *key, const QString &fallback)
{
    const QVariant value = node.value(QLatin1String(key));
    return value.isValid() ? value.toString() : fallback;
}

double num(const QVariantMap &node, const char *key, double fallback)
{
    const QVariant value = node.value(QLatin1String(key));
    return value.isValid() ? value.toDouble() : fallback;
}

bool boolean(const QVariantMap &node, const char *key, bool fallback)
{
    const QVariant value = node.value(QLatin1String(key));
    return value.isValid() ? value.toBool() : fallback;
}

QVariantList list(const QVariantMap &node, const char *key)
{
    return node.value(QLatin1String(key)).toList();
}

QVariantMap map(const QVariantMap &node, const char *key)
{
    return node.value(QLatin1String(key)).toMap();
}

QTransform matrix(const QVariant &field)
{
    if (field.typeId() == QMetaType::QVariantList) {
        // REST's [[m00,m01,m02],[m10,m11,m12]].
        const QVariantList rows = field.toList();
        if (rows.size() != 2)
            return {};
        const QVariantList top = rows[0].toList(), bottom = rows[1].toList();
        if (top.size() != 3 || bottom.size() != 3)
            return {};
        return QTransform(top[0].toDouble(), bottom[0].toDouble(), top[1].toDouble(), bottom[1].toDouble(), top[2].toDouble(), bottom[2].toDouble());
    }
    const QVariantMap m = field.toMap();
    if (m.isEmpty())
        return {};
    return QTransform(m.value(QStringLiteral("m00"), 1).toDouble(), m.value(QStringLiteral("m10")).toDouble(),
                       m.value(QStringLiteral("m01")).toDouble(), m.value(QStringLiteral("m11"), 1).toDouble(),
                       m.value(QStringLiteral("m02")).toDouble(), m.value(QStringLiteral("m12")).toDouble());
}

QColor color(const QVariantMap &node, double opacity)
{
    const auto channel = [&](const char *key, double fallback) {
        const QVariant value = node.value(QLatin1String(key));
        return std::clamp(value.isValid() ? value.toDouble() : fallback, 0.0, 1.0);
    };
    QColor result;
    result.setRgbF(float(channel("r", 0)), float(channel("g", 0)), float(channel("b", 0)), float(channel("a", 1) * std::clamp(opacity, 0.0, 1.0)));
    return result;
}

QPointF point(const QVariantMap &node)
{
    return {num(node, "x"), num(node, "y")};
}

Tree buildTree(const QVariantList &nodeChanges)
{
    Tree tree;
    for (const QVariant &entry : nodeChanges) {
        const QVariantMap fields = entry.toMap();
        Node node;
        node.fields = fields;
        node.guid = guidKey(fields.value(QStringLiteral("guid")));
        if (node.guid.isEmpty())
            continue;
        const QVariantMap parentIndex = map(fields, "parentIndex");
        node.parent = guidKey(parentIndex.value(QStringLiteral("guid")));
        node.position = parentIndex.value(QStringLiteral("position")).toString();
        tree.nodes[node.guid] = std::move(node);
    }
    for (auto &[guid, node] : tree.nodes) {
        if (node.parent.isEmpty() || !tree.nodes.count(node.parent))
            continue;
        tree.nodes[node.parent].children.push_back(guid);
    }
    for (auto &[guid, node] : tree.nodes) {
        std::sort(node.children.begin(), node.children.end(),
                  [&](const Guid &a, const Guid &b) { return tree.nodes.at(a).position < tree.nodes.at(b).position; });
    }
    for (auto &[guid, node] : tree.nodes) {
        if (str(node.fields, "type") == QLatin1String("CANVAS"))
            tree.pages.push_back(guid);
    }
    std::sort(tree.pages.begin(), tree.pages.end(),
              [&](const Guid &a, const Guid &b) { return tree.nodes.at(a).position < tree.nodes.at(b).position; });
    return tree;
}
}
