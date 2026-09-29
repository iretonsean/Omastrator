#include "Document/EditorSession.h"

bool EditorSession::refuseWhenLocked()
{
    if (!isDocumentLocked())
        return false;
    emit editRefused();
    // A field that showed the refused value goes back to the document's.
    notify(false);
    return true;
}

void EditorSession::setDocumentLocked(bool locked)
{
    if (!m_document || m_document->locked == locked)
        return;
    // A drag in flight ends first, so its step is recorded before the door shuts.
    if (m_interaction)
        commitInteraction();
    m_document->locked = locked;
    // The flag is saved, but it is no undo step: a locked document has no undo to step through.
    m_history.markUnsaved();
    notify(false);
}

QString EditorSession::lockedNotice()
{
    return QStringLiteral("This document is locked. Choose File ▸ Unlock Document to edit it.");
}
