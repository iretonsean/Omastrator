#include "Document/VectorDocument.h"
#include <QElapsedTimer>
#include <QTest>
#include <map>
#include <random>

// Where insert puts an object, and where a subtree ends: the order of `objects` is the z-order.
namespace {
VectorObject make(ObjectKind kind = ObjectKind::path)
{
    VectorObject object;
    object.kind = kind;
    return object;
}

QList<QUuid> order(const VectorDocument &document)
{
    QList<QUuid> ids;
    for (const VectorObject &object : document.objects)
        ids.append(object.id);
    return ids;
}

// The same tree as nested child lists, flattened depth-first: what the order must be.
struct Model {
    std::map<QUuid, QList<QUuid>> children;
    QList<QUuid> roots;

    QList<QUuid> &siblings(const std::optional<QUuid> &parent) { return parent ? children[*parent] : roots; }
    void flatten(const QList<QUuid> &ids, QList<QUuid> &out)
    {
        for (const QUuid &id : ids) {
            out.append(id);
            flatten(children[id], out);
        }
    }
    QList<QUuid> flat()
    {
        QList<QUuid> out;
        flatten(roots, out);
        return out;
    }
};
}

class VectorDocumentTests : public QObject {
    Q_OBJECT

private slots:
    void aMixOfInsertsLandsWhereExpected()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const QUuid layer = document.objects.front().id;
        const VectorObject a = make(), b = make(), g = make(ObjectKind::group), c = make(), d = make(), e = make(), f = make();
        document.insert(a, layer);
        document.insert(b, layer);
        document.insert(g, layer);
        document.insert(c, g.id); // into the group, which sits last
        document.insert(d, layer); // back out: after the group and its child
        document.insert(e, g.id); // into the group again, though it no longer ends the list
        document.insert(f, layer, a.id); // directly above a
        const QList<QUuid> expected{layer, a.id, f.id, b.id, g.id, c.id, e.id, d.id};
        QCOMPARE(order(document), expected);
        QCOMPARE(document.find(f.id)->parentID, std::optional<QUuid>(layer));
        QCOMPARE(document.find(e.id)->parentID, std::optional<QUuid>(g.id));
    }

    void insertAboveAGroupClearsItsWholeSubtree()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const QUuid layer = document.objects.front().id;
        const VectorObject g = make(ObjectKind::group), inner = make(ObjectKind::group), leaf = make(), top = make(), x = make();
        document.insert(g, layer);
        document.insert(inner, g.id);
        document.insert(leaf, inner.id);
        document.insert(top, layer);
        document.insert(x, layer, g.id);
        const QList<QUuid> expected{layer, g.id, inner.id, leaf.id, x.id, top.id};
        QCOMPARE(order(document), expected);
    }

    void insertIntoAMissingParentGoesToTheEnd()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const VectorObject a = make(), b = make();
        const QUuid layer = document.objects.front().id;
        document.insert(a, QUuid::createUuid()); // an orphan is not in the layer's subtree
        document.insert(b, layer);
        const QList<QUuid> expected{layer, b.id, a.id};
        QCOMPARE(order(document), expected);
    }

    void insertIntoASecondLayer()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const QUuid first = document.objects.front().id;
        VectorObject second = make(ObjectKind::layer);
        document.objects.push_back(second);
        const VectorObject a = make(), b = make(), c = make();
        document.insert(a, second.id);
        document.insert(b, first); // the first layer no longer ends the list
        document.insert(c, second.id);
        const QList<QUuid> expected{first, b.id, second.id, a.id, c.id};
        QCOMPARE(order(document), expected);
    }

    void randomInsertsMatchATreeModel()
    {
        std::mt19937 random(7);
        for (int round = 0; round < 20; ++round) {
            VectorDocument document = VectorDocument::blank({100, 100});
            Model model;
            const QUuid layer = document.objects.front().id;
            model.roots.append(layer);
            QList<QUuid> containers{layer};
            for (int step = 0; step < 150; ++step) {
                const QUuid parent = containers[int(random() % containers.size())];
                const bool group = random() % 4 == 0;
                VectorObject object = make(group ? ObjectKind::group : ObjectKind::path);
                QList<QUuid> &siblings = model.siblings(parent);
                if (!siblings.isEmpty() && random() % 3 == 0) {
                    const QUuid above = siblings[int(random() % siblings.size())];
                    document.insert(object, parent, above);
                    siblings.insert(siblings.indexOf(above) + 1, object.id);
                } else {
                    document.insert(object, parent);
                    siblings.append(object.id);
                }
                if (group)
                    containers.append(object.id);
            }
            QCOMPARE(order(document), model.flat());
        }
    }

    void appendingInPaintOrderIsFast()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        const QUuid layer = document.objects.front().id;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 100'000; ++i)
            document.insert(make(), layer);
        QCOMPARE(document.objects.size(), size_t(100'001));
        QVERIFY2(timer.elapsed() < 5000, "inserting in paint order should be linear");
    }
};

QTEST_MAIN(VectorDocumentTests)
#include "VectorDocumentTests.moc"
