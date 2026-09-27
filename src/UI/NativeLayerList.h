#pragma once
#include "Document/EditorSession.h"
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <memory>

class NativeLayerList;

// One row: eye, lock, disclosure, colour, icon, name, selection mark.
class LayerCell : public QWidget {
    Q_OBJECT
public:
    explicit LayerCell(NativeLayerList &list);
    static constexpr int rowHeight = 30;

    void configure(const VectorObject &object, int depth, bool visible, const QColor &layerColor);
    QUuid objectID() const { return m_id; }
    bool isLayer() const { return m_isLayer; }
    bool isContainer() const { return m_isContainer; }
    void beginRenaming();
    bool isRenaming() const { return m_renaming; }
    bool isOnControl(QPoint point) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();
    void endRenaming(bool keeping);

    NativeLayerList &m_list;
    QToolButton *const m_eye;
    QToolButton *const m_lock;
    QToolButton *const m_disclosure;
    QLabel *const m_icon;
    QLabel *const m_name;
    QLineEdit *const m_editor;
    QGraphicsOpacityEffect *const m_fade;
    QUuid m_id;
    QString m_objectName;
    QColor m_layerColor;
    bool m_isLayer = false;
    bool m_isContainer = false;
    bool m_renaming = false;
    int m_indent = 0;
    std::optional<QPoint> m_press;
    // The eye or lock press's keys: Alt acts on the other rows.
    Qt::KeyboardModifiers m_controlModifiers;
};

// Where a drag lands: its parent and position.
struct LayerDropTarget {
    // Null for a layer dragged among layers.
    QUuid parent;
    // The sibling rows go above or below; none: into parent.
    std::optional<QUuid> anchor;
    bool above = false;
    // With no anchor: the parent's top, else its bottom.
    bool atTop = true;
    // Indicator: a line before this row, or the row filled.
    int row = 0;
    bool fills = false;
    friend bool operator==(const LayerDropTarget &, const LayerDropTarget &) = default;
};

// The column of rows; it paints the drop indicator.
class LayerColumn : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    std::optional<QRect> indicator;
    bool indicatorFills = false;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// The layer tree, top down: layers, groups and objects.
class NativeLayerList : public QScrollArea {
    Q_OBJECT
public:
    explicit NativeLayerList(EditorSession &session, QWidget *parent = nullptr);
    // A dragged row carries its ids, one a line.
    static const QString rowType;
    // A row drag names its list; other lists refuse it.
    static const QString sourceType;

    struct Row {
        QUuid id;
        int depth;
    };
    // The rows the tree shows, top down, open groups unfolded.
    static std::vector<Row> rows(const VectorDocument &document);

    EditorSession &session() const { return m_session; }
    void update();
    const std::vector<LayerCell *> &cells() const { return m_cells; }
    // The row under a point of this list, or -1.
    int rowAt(QPoint listPoint) const;
    // A row pressed; Ctrl toggles, Shift extends.
    void clickRow(const LayerCell &cell, Qt::KeyboardModifiers modifiers);
    std::unique_ptr<QMimeData> dragData(const LayerCell &cell) const;
    void startDrag(const LayerCell &cell);
    std::optional<LayerDropTarget> dropTarget(const QMimeData &data, QPoint listPoint) const;
    bool acceptDrop(const QMimeData &data, QPoint listPoint);
    // Selected or active, as the row paints it.
    bool isHighlighted(const QUuid &id) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    std::vector<QUuid> draggedObjects(const QMimeData &data) const;
    // Dragged layer rows, top down; a drag with any layer moves only layers.
    std::vector<QUuid> draggedLayers(const QMimeData &data) const;
    std::optional<LayerDropTarget> layerDropTarget(const std::vector<QUuid> &layers, QPoint inColumn) const;
    bool acceptLayerDrop(const std::vector<QUuid> &layers, const LayerDropTarget &target);
    void showIndicator(const std::optional<LayerDropTarget> &target);
    void autoscroll(QPoint inViewport);

    EditorSession &m_session;
    LayerColumn *const m_column;
    std::vector<LayerCell *> m_cells;
    std::vector<Row> m_rows;
    // The row a Shift-click ranges from.
    std::optional<QUuid> m_anchor;
    const QString m_dragToken = QUuid::createUuid().toString();
    QTimer m_edgeScroll;
    QPoint m_dragPoint;
};
