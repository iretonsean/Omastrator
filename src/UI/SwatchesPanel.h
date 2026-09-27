#pragma once
#include "UI/FloatingPanel.h"
#include <QWidget>
#include <functional>

class EditorSession;
class Swatches;
class QVBoxLayout;

// Window ▸ Swatches: each group's colours as chips. A click sets the fill,
// Shift-click the stroke, of the selection or of the next shape. A global
// swatch (white corner) stays linked, so Edit Color… recolours every use.
class SwatchesPanel : public QWidget {
    Q_OBJECT
public:
    SwatchesPanel(Swatches &library, std::function<EditorSession &()> session, QWidget *parent = nullptr);

private:
    void rebuild();

    Swatches &m_library;
    const std::function<EditorSession &()> m_session;
    QVBoxLayout *const m_column;
    FloatingPanel m_picker{QStringLiteral("colorPickerPanel"), *this};
};
