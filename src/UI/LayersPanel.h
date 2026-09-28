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
    // Name with AI: asks the agent to name the layers, with a naming convention or none. Returns an error to show,
    // or empty once it's on its way. Unset, the button stays hidden.
    void setNamer(std::function<QString(const QString &convention)> namer);
    // Runs Name with AI; with `ask`, asks for a naming convention first.
    void nameWithAI(bool ask);

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
    QToolButton *m_name = nullptr;
    std::function<QString(const QString &)> m_namer;
};
