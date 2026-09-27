#include "UI/BlendModePicker.h"

BlendModePicker::BlendModePicker(EditorSession &session, QWidget *parent) : QComboBox(parent), m_session(session)
{
    setAccessibleName(QStringLiteral("Blend mode"));
    setObjectName(QStringLiteral("blendMode"));
    for (const LayerBlendMode mode : allLayerBlendModes)
        addItem(rawValue(mode));
    connect(this, &QComboBox::activated, this, [this](int index) {
        if (const std::optional<LayerBlendMode> mode = layerBlendMode(itemText(index)))
            m_session.setBlendModeOfSelection(*mode);
    });
    connect(&m_session, &EditorSession::changed, this, &BlendModePicker::synchronize);
    synchronize();
}

void BlendModePicker::synchronize()
{
    setEnabled(m_session.hasSelection());
    const VectorObject *first = m_session.hasSelection() ? m_session.document()->find(m_session.selection().front()) : nullptr;
    setCurrentText(rawValue(first ? first->blendMode : LayerBlendMode::normal));
}
