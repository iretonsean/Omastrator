#pragma once
#include "Document/EditorSession.h"
#include <QComboBox>

// The blend menu: the selection's mode, set for all.
class BlendModePicker : public QComboBox {
    Q_OBJECT
public:
    explicit BlendModePicker(EditorSession &session, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
};
