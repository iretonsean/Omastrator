#include "UI/Menus.h"
#include <QMenu>

// Illustrator's Select menu, between Object and Type.
void Menus::buildSelect(QMenuBar &bar)
{
    auto *select = new QMenu(QStringLiteral("&Select"), &bar);
    QAction *type = nullptr;
    for (QAction *entry : bar.actions()) {
        if (entry->text() == QLatin1String("&Type"))
            type = entry;
    }
    bar.insertMenu(type, select);
    add(select, QStringLiteral("selectAll"), QStringLiteral("All"), QKeySequence(Qt::CTRL | Qt::Key_A), [this] {
        if (m_field)
            m_field->selectAll();
        else
            session().selectAll();
    });
    add(select, QStringLiteral("deselect"), QStringLiteral("Deselect"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] { session().deselectAll(); });
    add(select, QStringLiteral("reselect"), QStringLiteral("Reselect"), QKeySequence(Qt::CTRL | Qt::Key_6), [this] { session().reselect(); });
    add(select, QStringLiteral("selectInverse"), QStringLiteral("Inverse"), QKeySequence(), [this] { session().selectInverse(); });
    select->addSeparator();
    add(select, QStringLiteral("nextObjectAbove"), QStringLiteral("Next Object Above"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketRight),
        [this] { session().selectAdjacent(true); });
    add(select, QStringLiteral("nextObjectBelow"), QStringLiteral("Next Object Below"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_BracketLeft),
        [this] { session().selectAdjacent(false); });
    select->addSeparator();
    QMenu *same = select->addMenu(QStringLiteral("Same"));
    same->menuAction()->setObjectName(QStringLiteral("selectSameMenu"));
    const std::vector<std::tuple<const char *, const char *, SameAttribute>> sames{
        {"selectSameFillAndStroke", "Fill & Stroke", SameAttribute::fillAndStroke},
        {"selectSameFillColor", "Fill Color", SameAttribute::fillColor},
        {"selectSameOpacity", "Opacity", SameAttribute::opacity},
        {"selectSameStrokeColor", "Stroke Color", SameAttribute::strokeColor},
        {"selectSameStrokeWeight", "Stroke Weight", SameAttribute::strokeWeight},
        {"selectSameBlendMode", "Blending Mode", SameAttribute::blendMode},
        {"selectSameFontFamily", "Font Family", SameAttribute::fontFamily},
        {"selectSameFontFamilyStyleSize", "Font Family, Style & Size", SameAttribute::fontFamilyStyleSize},
    };
    for (const auto &[name, text, attribute] : sames) {
        add(same, QString::fromLatin1(name), QString::fromUtf8(text), QKeySequence(), [this, attribute] { session().selectSame(attribute); });
    }
    QMenu *object = select->addMenu(QStringLiteral("Object"));
    object->menuAction()->setObjectName(QStringLiteral("selectObjectMenu"));
    add(object, QStringLiteral("selectSameLayers"), QStringLiteral("All on Same Layers"), QKeySequence(), [this] { session().selectAllOnSameLayers(); });
    object->addSeparator();
    const std::vector<std::tuple<const char *, const char *, ObjectFilter>> filters{
        {"selectClippingMasks", "Clipping Masks", ObjectFilter::clippingMasks},
        {"selectStrayPoints", "Stray Points", ObjectFilter::strayPoints},
        {"selectTextObjects", "Text Objects", ObjectFilter::textObjects},
        {"selectImages", "Images", ObjectFilter::images},
        {"selectOpenPaths", "Open Paths", ObjectFilter::openPaths},
    };
    for (const auto &[name, text, filter] : filters)
        add(object, QString::fromLatin1(name), QString::fromUtf8(text), QKeySequence(), [this, filter] { session().selectObjects(filter); });
}
