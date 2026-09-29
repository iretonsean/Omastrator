#pragma once
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

class QLabel;

// A Properties section: a heading that folds its body away, remembered per section.
// Folded, the heading keeps a one-line summary of what's inside (docs/PANELS.md).
class PanelSection : public QWidget {
    Q_OBJECT
public:
    // `key` names the section in settings and in object names; `folded` is how it starts
    // until the user folds or opens it. `settings` is where the fold is remembered; Layers' Pages list
    // keeps its own.
    PanelSection(const QString &title, const QString &key, QWidget *parent, bool folded = false,
                 const QString &settings = {});
    QVBoxLayout *const body;
    // Controls at the heading's right end, such as an options button.
    QHBoxLayout *const trailing;
    QToolButton *toggle() const { return m_toggle; }
    bool isCollapsed() const;
    void setCollapsed(bool collapsed);
    static QString settingsKey(const QString &key);
    // What the folded heading says, such as "1 pt · Center · Butt".
    std::function<QString()> summary;
    // Reads `summary` again; the panel calls it as the selection changes.
    void refreshSummary();
    QString summaryText() const;

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyChevron();
    const QString m_key;
    const QString m_settings;
    QToolButton *const m_toggle;
    QLabel *const m_summary;
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
    QSize sizeHint() const override { return {m_side, m_side}; }
    // Drawn to fit one row of fields.
    void setCompact();

signals:
    void pointChanged(int point);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF cell(int point) const;
    int m_point = 0;
    int m_side = 38;
};
