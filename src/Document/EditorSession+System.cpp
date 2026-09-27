#include "Document/EditorSession.h"
#include <algorithm>

namespace {
QString uniqueTokenName(const std::vector<DesignToken> &tokens, const QString &base)
{
    QString name = base.trimmed().isEmpty() ? QStringLiteral("token") : base.trimmed();
    for (int number = 2; DesignTokens::named(tokens, name); ++number)
        name = base.trimmed() + QStringLiteral("-%1").arg(number);
    return name;
}

DesignToken *mutableToken(VectorDocument &document, const QString &id)
{
    auto found = std::find_if(document.tokens.begin(), document.tokens.end(), [&](const DesignToken &token) { return token.id == id; });
    return found == document.tokens.end() ? nullptr : &*found;
}

// Uses of `id` lose the link and keep their look.
void unlinkEverywhere(VectorDocument &document, const QString &id)
{
    const auto clear = [&](Paint &paint) {
        if (paint.token == id)
            paint.token.clear();
    };
    for (VectorObject &object : document.objects) {
        clear(object.fill);
        clear(object.stroke.paint);
        for (Paint &paint : object.extraFills)
            clear(paint);
        for (StrokeStyle &stroke : object.extraStrokes)
            clear(stroke.paint);
        std::erase_if(object.tokenRefs, [&](const auto &ref) { return ref.second == id; });
    }
    for (TextStyle &style : document.textStyles) {
        if (style.typeToken == id)
            style.typeToken.clear();
    }
}

QString componentName(const VectorDocument &document)
{
    for (int number = 1;; ++number) {
        const QString name = QStringLiteral("Component %1").arg(number);
        if (Components::variantsOf(document, name).empty())
            return name;
    }
}
}

const DesignToken *EditorSession::token(const QString &id) const
{
    return m_document ? DesignTokens::find(m_document->tokens, id) : nullptr;
}

QString EditorSession::addToken(DesignToken token)
{
    if (!m_document)
        return {};
    if (token.id.isEmpty() || DesignTokens::find(m_document->tokens, token.id))
        token.id = DesignTokens::newId();
    token.name = uniqueTokenName(m_document->tokens, token.name);
    const QString id = token.id;
    edit(QStringLiteral("Add Token"), [&](VectorDocument &document) { document.tokens.push_back(token); });
    return id;
}

void EditorSession::setTokenValue(const QString &id, const TokenValue &value, const QString &mode)
{
    const DesignToken *found = token(id);
    if (!found || found->valueIn(mode) == value)
        return;
    edit(QStringLiteral("Edit Token"), [&](VectorDocument &document) {
        DesignToken *token = mutableToken(document, id);
        if (mode.isEmpty() || mode == document.tokenModes.value(0))
            token->value = value;
        else
            token->modes[mode] = value;
        DesignTokens::apply(document);
    });
}

void EditorSession::renameToken(const QString &id, const QString &name)
{
    const DesignToken *found = token(id);
    if (!found || name.trimmed().isEmpty() || found->name == name.trimmed())
        return;
    edit(QStringLiteral("Rename Token"), [&](VectorDocument &document) {
        mutableToken(document, id)->name = uniqueTokenName(document.tokens, name);
    });
}

void EditorSession::deleteToken(const QString &id)
{
    if (!token(id))
        return;
    edit(QStringLiteral("Delete Token"), [&](VectorDocument &document) {
        unlinkEverywhere(document, id);
        std::erase_if(document.tokens, [&](const DesignToken &token) { return token.id == id; });
    });
}

int EditorSession::mergeTokens(const std::vector<DesignToken> &tokens, const QString &editName, const QStringList &modes)
{
    if (!m_document)
        return 0;
    int changed = 0;
    edit(editName, [&](VectorDocument &document) {
        changed = DesignTokens::merge(document.tokens, tokens);
        for (const QString &mode : modes) {
            if (!document.tokenModes.contains(mode))
                document.tokenModes.append(mode);
        }
        if (document.tokenMode.isEmpty())
            document.tokenMode = document.tokenModes.value(0);
        DesignTokens::apply(document);
    });
    return changed;
}

void EditorSession::addTokenMode(const QString &mode)
{
    if (!m_document || mode.trimmed().isEmpty() || m_document->tokenModes.contains(mode.trimmed()))
        return;
    edit(QStringLiteral("Add Mode"), [&](VectorDocument &document) {
        // The values so far are the first mode's.
        if (document.tokenModes.isEmpty())
            document.tokenModes.append(QStringLiteral("light") == mode.trimmed() ? QStringLiteral("default") : QStringLiteral("light"));
        document.tokenModes.append(mode.trimmed());
        for (DesignToken &token : document.tokens)
            token.modes[mode.trimmed()] = token.value;
        if (document.tokenMode.isEmpty())
            document.tokenMode = document.tokenModes.front();
    });
}

void EditorSession::setTokenMode(const QString &mode)
{
    if (!m_document || !m_document->tokenModes.contains(mode) || m_document->tokenMode == mode)
        return;
    edit(QStringLiteral("Switch to %1 Mode").arg(mode.left(1).toUpper() + mode.mid(1)), [&](VectorDocument &document) {
        document.tokenMode = mode;
        DesignTokens::apply(document);
    });
}

QString EditorSession::applyToken(const QString &id, const QString &target)
{
    const DesignToken *found = token(id);
    if (!found)
        return QStringLiteral("That token isn't in this document.");
    if (m_selection.empty())
        return QStringLiteral("Select something to apply the token to.");
    const TokenKind kind = found->kind;
    QString key = target;
    if (key.isEmpty()) {
        const VectorObject *first = m_document->find(m_selection.front());
        key = kind == TokenKind::color ? QStringLiteral("fill")
            : kind == TokenKind::radius ? TokenRef::radius
            : kind == TokenKind::type   ? TokenRef::type
            : kind == TokenKind::spacing && first && first->kind == ObjectKind::group
                ? (m_document->bounds(first->id).width() >= m_document->bounds(first->id).height() ? TokenRef::gapX : TokenRef::gapY)
                : QString();
    }
    if (key.isEmpty())
        return kind == TokenKind::shadow ? QStringLiteral("Shadow tokens are kept for code; objects here have no shadow yet.")
                                         : QStringLiteral("Select a group to space its contents by this token.");
    const bool colour = key == QLatin1String("fill") || key == QLatin1String("stroke");
    if (colour != (kind == TokenKind::color))
        return QStringLiteral("A %1 token can't set that.").arg(title(kind).toLower());
    const bool gap = key == TokenRef::gapX || key == TokenRef::gapY;
    const std::vector<QUuid> targets = gap ? m_selection : selectedLeaves();
    const auto takes = [&](const VectorObject &object) {
        if (colour)
            return object.hasPaint();
        if (key == TokenRef::radius)
            return object.liveShape() != nullptr;
        if (key == TokenRef::type)
            return object.kind == ObjectKind::text;
        if (key == TokenRef::strokeWidth)
            return object.hasPaint();
        return gap && object.kind == ObjectKind::group;
    };
    if (std::none_of(targets.begin(), targets.end(), [&](const QUuid &each) { return m_document->find(each) && takes(*m_document->find(each)); }))
        return key == TokenRef::radius ? QStringLiteral("Corner tokens apply to live rectangles.") : QStringLiteral("Nothing selected can take this token.");
    edit(QStringLiteral("Apply Token"), [&](VectorDocument &document) {
        for (const QUuid &each : targets) {
            VectorObject *object = document.find(each);
            if (!object || !takes(*object))
                continue;
            if (colour) {
                Paint &paint = key == QLatin1String("fill") ? object->fill : object->stroke.paint;
                const Paint keep = paint;
                paint = Paint::solid(Qt::black).withCompositeOf(keep);
                paint.token = id;
                if (key == QLatin1String("stroke") && object->stroke.width <= 0)
                    object->stroke.width = 1;
            } else {
                if (gap) {
                    object->tokenRefs.erase(TokenRef::gapX);
                    object->tokenRefs.erase(TokenRef::gapY);
                }
                object->tokenRefs[key] = id;
            }
        }
        DesignTokens::apply(document);
    });
    return {};
}

void EditorSession::unlinkToken(const QString &target)
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> targets = target == TokenRef::gapX || target == TokenRef::gapY ? m_selection : selectedLeaves();
    edit(QStringLiteral("Detach Token"), [&](VectorDocument &document) {
        for (const QUuid &each : targets) {
            VectorObject *object = document.find(each);
            if (!object)
                continue;
            if (target == QLatin1String("fill"))
                object->fill.token.clear();
            else if (target == QLatin1String("stroke"))
                object->stroke.paint.token.clear();
            else
                object->tokenRefs.erase(target);
        }
    });
}

QString EditorSession::linkedToken(const QString &target) const
{
    if (!m_document || m_selection.empty())
        return {};
    const bool gap = target == TokenRef::gapX || target == TokenRef::gapY;
    QString shared;
    for (const QUuid &each : gap ? m_selection : selectedLeaves()) {
        const VectorObject *object = m_document->find(each);
        if (!object)
            continue;
        QString id;
        if (target == QLatin1String("fill"))
            id = object->fill.token;
        else if (target == QLatin1String("stroke"))
            id = object->stroke.paint.token;
        else
            id = object->tokenRefs.count(target) ? object->tokenRefs.at(target) : QString();
        if (id.isEmpty() || (!shared.isEmpty() && shared != id))
            return {};
        shared = id;
    }
    return shared;
}

void EditorSession::linkTextStyle(const QUuid &style, const QString &tokenId)
{
    if (!textStyle(style) || (!tokenId.isEmpty() && (!token(tokenId) || token(tokenId)->kind != TokenKind::type)))
        return;
    edit(tokenId.isEmpty() ? QStringLiteral("Detach Token") : QStringLiteral("Apply Token"), [&](VectorDocument &document) {
        for (TextStyle &each : document.textStyles) {
            if (each.id == style)
                each.typeToken = tokenId;
        }
        DesignTokens::apply(document);
    });
}

std::optional<QUuid> EditorSession::makeComponent(const QString &name)
{
    if (!m_document || m_selection.empty())
        return std::nullopt;
    std::optional<QUuid> made;
    const QString set = name.trimmed().isEmpty() ? componentName(*m_document) : name.trimmed();
    edit(QStringLiteral("Make Component"), [&](VectorDocument &document) {
        QUuid root;
        const VectorObject *only = m_selection.size() == 1 ? document.find(m_selection.front()) : nullptr;
        if (only && only->kind == ObjectKind::group && !only->component && !only->instance) {
            root = only->id;
        } else {
            const std::vector<QUuid> members = selectionInOrder();
            VectorObject group;
            group.kind = ObjectKind::group;
            root = group.id;
            const QUuid top = members.back();
            document.insert(group, *document.find(top)->parentID, top);
            for (const QUuid &id : members)
                document.move(id, root, -1);
        }
        VectorObject *group = document.find(root);
        group->name = set;
        group->instance.reset();
        group->component = ComponentInfo{set, {}, QTransform()};
        m_selection = {root};
        made = root;
    });
    return made;
}

QUuid EditorSession::placeInstance(const QUuid &master, std::optional<QPointF> center)
{
    const VectorObject *main = m_document ? m_document->find(master) : nullptr;
    if (!main || !main->component)
        return {};
    const QRectF bounds = m_document->bounds(master);
    const QPointF at = center.value_or(bounds.center() + QPointF(bounds.width() + 24, 0));
    VectorObject instance;
    instance.kind = ObjectKind::group;
    instance.name = main->name;
    instance.instance = InstanceInfo{master, main->component->placement * QTransform::fromTranslate(at.x() - bounds.center().x(), at.y() - bounds.center().y()), {}, {}};
    const QUuid id = instance.id;
    edit(QStringLiteral("Place Instance"), [&](VectorDocument &document) {
        m_selection.clear();
        m_isolation.clear();
        insertNew(document, std::move(instance));
    });
    return id;
}

QUuid EditorSession::placeFromLibrary(const std::vector<VectorObject> &objects, const QString &set, const std::map<QString, QString> &variant,
                                      std::optional<QPointF> center)
{
    if (!m_document || objects.empty())
        return {};
    QUuid placed;
    edit(QStringLiteral("Place Component"), [&](VectorDocument &document) {
        // Library components keep their ids, so a second placement finds the first.
        std::optional<QUuid> layer;
        double right = document.size.width() + 80;
        for (const QUuid &existing : Components::masters(document))
            right = std::max(right, document.bounds(existing).right() + 40);
        for (size_t index = 0; index < objects.size(); ++index) {
            const VectorObject &root = objects[index];
            if (root.parentID && std::any_of(objects.begin(), objects.end(), [&](const VectorObject &o) { return o.id == *root.parentID; }))
                continue;
            if (!root.component || document.find(root.id))
                continue;
            if (!layer) {
                for (const QUuid &each : document.layers()) {
                    if (document.find(each)->name == QLatin1String("Components"))
                        layer = each;
                }
                if (!layer) {
                    VectorObject components;
                    components.kind = ObjectKind::layer;
                    components.name = QStringLiteral("Components");
                    components.layerColor = nextLayerColor(int(document.layers().size()));
                    layer = components.id;
                    document.objects.push_back(components);
                }
            }
            // The subtree, in order, under the Components layer.
            std::vector<VectorObject> subtree{root};
            std::vector<QUuid> inside{root.id};
            for (size_t each = index + 1; each < objects.size(); ++each) {
                if (objects[each].parentID && std::find(inside.begin(), inside.end(), *objects[each].parentID) != inside.end()) {
                    subtree.push_back(objects[each]);
                    inside.push_back(objects[each].id);
                }
            }
            subtree.front().parentID = layer;
            const auto end = document.objects.begin() + document.indexOf(*layer) + 1 + int(document.descendants(*layer).size());
            document.objects.insert(end, subtree.begin(), subtree.end());
            const QRectF bounds = document.bounds(root.id);
            document.transform(root.id, QTransform::fromTranslate(right - bounds.left(), 40 - bounds.top()));
            right += bounds.width() + 40;
        }
        const std::optional<QUuid> master = Components::bestVariant(document, set, variant);
        if (!master)
            return;
        const VectorObject *main = document.find(*master);
        const QRectF bounds = document.bounds(*master);
        const QPointF at = center.value_or(QPointF(document.size.width() / 2, document.size.height() / 2));
        VectorObject instance;
        instance.kind = ObjectKind::group;
        instance.name = main->name;
        instance.instance = InstanceInfo{*master, main->component->placement * QTransform::fromTranslate(at.x() - bounds.center().x(), at.y() - bounds.center().y()), {}, {}};
        placed = instance.id;
        m_selection.clear();
        m_isolation.clear();
        insertNew(document, std::move(instance));
    });
    return placed;
}

std::optional<QUuid> EditorSession::addVariant(const QUuid &master, const QString &property, const QString &value)
{
    const VectorObject *main = m_document ? m_document->find(master) : nullptr;
    if (!main || !main->component || property.trimmed().isEmpty() || value.trimmed().isEmpty())
        return std::nullopt;
    std::optional<QUuid> made;
    edit(QStringLiteral("Add Variant"), [&](VectorDocument &document) {
        VectorObject *source = document.find(master);
        // The first variant names its own value.
        if (!source->component->variant.count(property.trimmed())) {
            const QString first = QStringLiteral("default");
            for (const QUuid &sibling : Components::variantsOf(document, source->component->set))
                document.find(sibling)->component->variant.emplace(property.trimmed(), first);
        }
        std::vector<VectorObject> copies = document.copySubtree(master);
        // copySubtree makes an instance of a copied component; a variant is a component of its own.
        copies.front().instance.reset();
        copies.front().component = document.find(master)->component;
        copies.front().component->variant[property.trimmed()] = value.trimmed();
        copies.front().name = document.find(master)->name;
        const QRectF bounds = document.bounds(master);
        const int at = document.indexOf(master) + 1 + int(document.descendants(master).size());
        document.objects.insert(document.objects.begin() + at, copies.begin(), copies.end());
        document.transform(copies.front().id, QTransform::fromTranslate(bounds.width() + 24, 0));
        made = copies.front().id;
        m_selection = {copies.front().id};
    });
    return made;
}

void EditorSession::setVariantProperty(const QUuid &master, const QString &property, const QString &value)
{
    const VectorObject *main = m_document ? m_document->find(master) : nullptr;
    if (!main || !main->component || property.trimmed().isEmpty())
        return;
    edit(QStringLiteral("Edit Variant"), [&](VectorDocument &document) {
        auto &variant = document.find(master)->component->variant;
        if (value.trimmed().isEmpty())
            variant.erase(property.trimmed());
        else
            variant[property.trimmed()] = value.trimmed();
    });
}

void EditorSession::renameComponent(const QString &set, const QString &name)
{
    if (!m_document || name.trimmed().isEmpty() || Components::variantsOf(*m_document, set).empty() || set == name.trimmed())
        return;
    edit(QStringLiteral("Rename Component"), [&](VectorDocument &document) {
        for (const QUuid &id : Components::variantsOf(document, set)) {
            VectorObject *main = document.find(id);
            main->component->set = name.trimmed();
            main->name = name.trimmed();
        }
    });
}

QString EditorSession::swapVariant(const QString &property, const QString &value)
{
    const std::vector<QUuid> instances = selectedInstances();
    if (instances.empty())
        return QStringLiteral("Select an instance to swap its variant.");
    bool any = false;
    edit(QStringLiteral("Swap Variant"), [&](VectorDocument &document) {
        for (const QUuid &id : instances) {
            VectorObject *instance = document.find(id);
            const VectorObject *master = document.find(instance->instance->master);
            if (!master || !master->component)
                continue;
            std::map<QString, QString> wanted = master->component->variant;
            wanted[property] = value;
            const std::optional<QUuid> best = Components::bestVariant(document, master->component->set, wanted);
            if (!best || *best == master->id || document.find(*best)->component->variant.count(property) == 0
                || document.find(*best)->component->variant.at(property) != value)
                continue;
            const VectorObject *next = document.find(*best);
            // Kept where it is: the new variant's frame lands on the old one's.
            instance->instance->placement = master->component->placement.inverted() * instance->instance->placement;
            instance->instance->placement = next->component->placement * instance->instance->placement;
            instance->instance->master = *best;
            instance->instance->made.clear();
            any = true;
        }
    });
    return any ? QString() : QStringLiteral("There's no variant with %1 = %2.").arg(property, value);
}

void EditorSession::detachInstances()
{
    const std::vector<QUuid> instances = selectedInstances();
    if (instances.empty())
        return;
    edit(QStringLiteral("Detach Instance"), [&](VectorDocument &document) {
        for (const QUuid &id : instances)
            document.find(id)->instance.reset();
    });
}

void EditorSession::resetOverrides()
{
    const std::vector<QUuid> instances = selectedInstances();
    if (instances.empty())
        return;
    edit(QStringLiteral("Reset Overrides"), [&](VectorDocument &document) {
        for (const QUuid &id : instances) {
            InstanceInfo &info = *document.find(id)->instance;
            info.overrides.clear();
            info.made.clear();
        }
    });
}

std::vector<QUuid> EditorSession::selectedInstances() const
{
    std::vector<QUuid> result;
    if (!m_document)
        return result;
    for (const QUuid &id : m_selection) {
        // A layer inside an instance counts as its instance.
        for (std::optional<QUuid> at = id; at; at = m_document->find(*at) ? m_document->find(*at)->parentID : std::nullopt) {
            const VectorObject *object = m_document->find(*at);
            if (object && object->instance) {
                if (std::find(result.begin(), result.end(), *at) == result.end())
                    result.push_back(*at);
                break;
            }
        }
    }
    return result;
}

std::optional<QUuid> EditorSession::selectedMaster() const
{
    if (!m_document)
        return std::nullopt;
    for (const QUuid &id : m_selection) {
        const VectorObject *object = m_document->find(id);
        if (object && object->component)
            return id;
    }
    const auto instances = selectedInstances();
    if (!instances.empty()) {
        const QUuid master = m_document->find(instances.front())->instance->master;
        if (m_document->find(master))
            return master;
    }
    return std::nullopt;
}

void EditorSession::settle()
{
    if (!m_document)
        return;
    // A colour changed by hand leaves its token first, so an instance sees an override.
    DesignTokens::dropStale(*m_document);
    Components::sync(*m_document);
}
