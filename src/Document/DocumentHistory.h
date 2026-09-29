#pragma once
#include "Document/VectorDocument.h"
#include <QString>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

// Snapshots share image data; layer edits copy no pixels.
class DocumentHistory {
public:
    struct Snapshot {
        std::optional<VectorDocument> document;
        std::vector<QUuid> selection;
        QUuid revision;
    };

    int entryLimit() const { return m_entryLimit; }
    // Fewer steps drop the oldest at the next edit.
    void setEntryLimit(int limit) { m_entryLimit = std::max(1, limit); }
    const qint64 retainedByteLimit;

    explicit DocumentHistory(int entryLimit = 100, qint64 retainedByteLimit = 256LL * 1024 * 1024);

    bool canUndo() const { return m_depth == 0 && !m_past.empty(); }
    bool canRedo() const { return m_depth == 0 && !m_future.empty(); }
    QString undoName() const { return m_past.empty() ? QString() : m_past.back().name; }
    QString redoName() const { return m_future.empty() ? QString() : m_future.back().name; }
    bool isModified() const { return m_revision != m_savedRevision; }
    int undoCount() const { return int(m_past.size()); }
    std::vector<QString> undoNames() const;
    std::vector<QString> redoNames() const;
    void markSaved() { m_savedRevision = m_revision; }
    // What's shown matches no file any more, as when a cloud conflict detaches it.
    void markUnsaved() { m_savedRevision = QUuid::createUuid(); }
    void reset();
    // Takes the steps a staged copy of this history recorded.
    void adopt(DocumentHistory &&staged) noexcept;

    void begin(const QString &name, const std::optional<VectorDocument> &document, const std::vector<QUuid> &selection);
    void end(const std::optional<VectorDocument> &document, const std::vector<QUuid> &selection);
    // Folds a further change into the last step, as a held key's repeats are one step.
    bool amend(const QString &name, const std::optional<VectorDocument> &document, const std::vector<QUuid> &selection);
    std::optional<Snapshot> undo();
    std::optional<Snapshot> redo();
    qint64 retainedBytes(const std::optional<VectorDocument> &current) const;
    // Rewrites every recorded snapshot, so a change that isn't an edit (a page's own navigation) isn't undone by a later step.
    void mapDocuments(const std::function<void(VectorDocument &)> &change);

private:
    struct Entry {
        QString name;
        Snapshot before;
        Snapshot after;
    };

    void trim(const std::optional<VectorDocument> &current);

    int m_entryLimit;
    std::vector<Entry> m_past;
    std::vector<Entry> m_future;
    QUuid m_revision = QUuid::createUuid();
    QUuid m_savedRevision = m_revision;
    std::optional<Snapshot> m_pending;
    QString m_pendingName = QStringLiteral("Edit");
    int m_depth = 0;
};
