#include "Agent/AgentParams.h"
#include "Agent/AgentTools.h"
#include "Document/EditorSession.h"
#include <QJsonArray>

// The `page` method (docs/PAGES.md section 6): the designer's own page operations, each a normal
// undo step rather than part of the proposal. There is no delete: losing a page is the designer's call.
using AgentProtocol::Error;
using namespace AgentParams;

QJsonObject AgentTools::page(const QJsonObject &params)
{
    static const QStringList actions{QStringLiteral("add"), QStringLiteral("rename"), QStringLiteral("duplicate"),
                                     QStringLiteral("reorder"), QStringLiteral("move_objects"), QStringLiteral("show")};
    const int action = *choice(params, QStringLiteral("action"), actions, true);
    EditorSession &current = idleSession();
    const VectorDocument &document = *current.document();
    const std::optional<QUuid> named = pageParam(params, document);
    const QUuid target = named.value_or(document.currentPageId());
    const auto describe = [&](const QUuid &id) {
        const VectorDocument &now = *current.document();
        return QJsonObject{{"id", idString(id)}, {"name", now.allPages()[size_t(now.pageIndex(id))].name}, {"current", id == now.currentPageId()}};
    };
    const auto pages = [&] {
        const VectorDocument &now = *current.document();
        QJsonArray list;
        for (const Page &each : now.allPages())
            list.append(QJsonObject{{"id", idString(each.id)}, {"name", each.name}, {"current", each.id == now.currentPageId()}});
        return list;
    };
    const auto answer = [&](const QUuid &id, QJsonObject extra = {}) {
        extra["page"] = describe(id);
        extra["pages"] = pages();
        return extra;
    };
    switch (action) {
    case 0: {
        const QUuid id = current.addPage(string(params, QStringLiteral("name")).value_or(QString()));
        return answer(id);
    }
    case 1: {
        const QString name = requiredString(params, QStringLiteral("name")).simplified();
        if (name.isEmpty())
            fail(QStringLiteral("“name” is empty: give the page a name."));
        current.renamePage(target, name);
        return answer(target);
    }
    case 2:
        return answer(current.duplicatePage(target));
    case 3: {
        const auto index = number(params, QStringLiteral("index"));
        if (!index || *index < 0 || *index != std::floor(*index))
            fail(QStringLiteral("“index” is required: the page's new position, counting from 0."));
        if (!named)
            fail(QStringLiteral("“page” is required: the page to move."));
        current.movePage(target, int(*index));
        return answer(target);
    }
    case 4: {
        if (!named)
            fail(QStringLiteral("“page” is required: the page the objects go to."));
        const std::vector<QUuid> objects = targets(params);
        for (const QUuid &id : objects) {
            const VectorObject *found = document.find(id);
            if (found && found->kind == ObjectKind::layer)
                fail(QStringLiteral("%1 is a layer. move_objects moves objects; layers stay where they are.").arg(idString(id)));
            if (!document.isOnCurrentPage(id))
                fail(QStringLiteral("%1 is on another page. Call page with action “show” for its page first.").arg(idString(id)));
        }
        if (target == document.currentPageId())
            fail(QStringLiteral("Those objects are already on that page."));
        current.select(objects);
        current.moveSelectionToPage(target);
        // What went, as the document now has it: a locked document or a proposal moves nothing.
        std::vector<QUuid> moved;
        for (const QUuid &id : objects) {
            if (current.document()->pageOf(id) == target)
                moved.push_back(id);
        }
        if (moved.empty())
            fail(current.isDocumentLocked() ? EditorSession::lockedNotice() : QStringLiteral("Nothing moved."));
        return answer(target, {{"moved", idArray(moved)}});
    }
    default:
        if (!named)
            fail(QStringLiteral("“page” is required: the page to show."));
        current.setCurrentPage(target);
        return answer(target);
    }
}
