#pragma once
#include "Document/EditorSession.h"
#include <QLabel>
#include <QToolButton>
#include <QWidget>

class NativeLayerList;

// The layers panel: heading, the tree, the footer's buttons.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(EditorSession &session, QWidget *parent = nullptr);

    NativeLayerList &list() const { return *m_list; }
    // Deletes the selection, else the active layer.
    void deleteTarget();

protected:
    void changeEvent(QEvent *event) override;

private:
    void synchronize();
    void applyIcons();
    QToolButton *footerButton(const QString &name, const QString &tip, const std::function<void()> &run);

    EditorSession &m_session;
    QLabel *const m_count;
    NativeLayerList *const m_list;
    QToolButton *m_newLayer = nullptr;
    QToolButton *m_delete = nullptr;
};
