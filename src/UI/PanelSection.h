#pragma once
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

// A Properties section: a heading that folds its body away, remembered per section.
class PanelSection : public QWidget {
    Q_OBJECT
public:
    // `key` names the section in settings and in object names.
    PanelSection(const QString &title, const QString &key, QWidget *parent);
    QVBoxLayout *const body;
    // Controls at the heading's right end, such as an options button.
    QHBoxLayout *const trailing;
    QToolButton *toggle() const { return m_toggle; }
    bool isCollapsed() const;
    void setCollapsed(bool collapsed);
    static QString settingsKey(const QString &key);

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyChevron();
    const QString m_key;
    QToolButton *const m_toggle;
    QWidget *const m_content;
};

// Illustrator's 9-point reference locator: which point of the bounds X and Y
// read, and what W, H and rotation pivot on.
class ReferencePointPicker : public QWidget {
    Q_OBJECT
public:
    explicit ReferencePointPicker(QWidget *parent = nullptr);
    // 0 is top left, 4 the centre, 8 bottom right.
    int point() const { return m_point; }
    void setPoint(int point);
    // The point's place within `bounds`.
    static QPointF locate(const QRectF &bounds, int point);
    static QString name(int point);
    QSize sizeHint() const override { return {38, 38}; }

signals:
    void pointChanged(int point);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF cell(int point) const;
    int m_point = 0;
};
