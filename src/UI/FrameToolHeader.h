#pragma once
#include "UI/ToolHeaderStyle.h"

class EditorSession;
class QCheckBox;
class QComboBox;
class QPushButton;

// The island with the Frame tool (docs/WINDOW-LAYOUT.md): a size to drop, and the selected frames'
// clipping and auto layout.
class FrameToolHeader : public ToolHeaderBar {
    Q_OBJECT
public:
    explicit FrameToolHeader(EditorSession &session, QWidget *parent = nullptr);

private:
    void synchronize();

    EditorSession &m_session;
    QComboBox *const m_size;
    QCheckBox *const m_clip;
    QPushButton *const m_autoLayout;
};
