#pragma once
#include "Document/EditorSession.h"
#include <QLineEdit>
#include <QSlider>
#include <QWidget>

class BlendModePicker;

// The selection's blend mode and opacity.
class LayerAppearanceControls : public QWidget {
    Q_OBJECT
public:
    explicit LayerAppearanceControls(EditorSession &session, QWidget *parent = nullptr);
    ~LayerAppearanceControls() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void synchronize();
    double shownOpacity() const;
    void applyPercentage();
    void endDrag();

    EditorSession &m_session;
    BlendModePicker *const m_picker;
    QSlider *const m_slider;
    QLineEdit *const m_percentage;
    bool m_syncing = false;
    bool m_dragging = false;
};
