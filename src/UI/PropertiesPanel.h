#pragma once
#include "Document/EditorSession.h"
#include "UI/FloatingPanel.h"
#include "UI/PanelIcons.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QScrollArea>

class NumberField;
class PaintSwatch;

// A fill or stroke: kind, colour, gradient end.
class PaintRow : public QWidget {
    Q_OBJECT
public:
    PaintRow(EditorSession &session, bool stroke, FloatingPanel &picker, QWidget *parent);
    void synchronize();
    // The paint as the kind menu turns it.
    static Paint converted(const Paint &paint, PaintKind kind);

private:
    Paint shown() const;
    void apply(const Paint &paint);
    void pickStop(bool last);

    EditorSession &m_session;
    const bool m_stroke;
    FloatingPanel &m_picker;
    QComboBox *const m_kind;
    PaintSwatch *const m_well;
    PaintSwatch *const m_end;
};

// The right-hand inspector: transform, appearance, align, pathfinder.
class PropertiesPanel : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertiesPanel(EditorSession &session, QWidget *parent = nullptr);

private:
    QWidget *transformSection();
    QWidget *artboardSection();
    QWidget *appearanceSection();
    QWidget *strokeSection();
    QWidget *alignSection();
    QWidget *pathfinderSection();
    QToolButton *iconButton(const QString &name, const QString &tip, PanelIcon icon, const std::function<void()> &run);
    void moveTo(double x, double y);
    void resizeTo(double width, double height);
    void setStrokeWidth(double width);
    void applyDashes();
    void synchronize();
    void applyIcons();

protected:
    void changeEvent(QEvent *event) override;

private:
    EditorSession &m_session;
    FloatingPanel m_picker{QStringLiteral("colorPickerPanel"), *this};
    QWidget *m_transform = nullptr;
    QWidget *m_artboard = nullptr;
    NumberField *m_x = nullptr;
    NumberField *m_y = nullptr;
    NumberField *m_width = nullptr;
    NumberField *m_height = nullptr;
    NumberField *m_rotation = nullptr;
    NumberField *m_artboardWidth = nullptr;
    NumberField *m_artboardHeight = nullptr;
    PaintRow *m_fill = nullptr;
    PaintRow *m_strokePaint = nullptr;
    NumberField *m_strokeWidth = nullptr;
    QComboBox *m_cap = nullptr;
    QComboBox *m_join = nullptr;
    QLineEdit *m_dashes = nullptr;
    QComboBox *m_alignTarget = nullptr;
    std::vector<std::pair<QToolButton *, PanelIcon>> m_iconButtons;
    std::vector<QToolButton *> m_alignButtons;
    std::vector<QToolButton *> m_distributeButtons;
    std::vector<QToolButton *> m_pathfinderButtons;
};
