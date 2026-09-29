#pragma once
#include <QImage>
#include <QString>
#include <QWidget>

// A chrome-less window that keeps a page's Hyprland workspace alive and shows a picture of the page (docs/WORKSPACES.md).
// It takes no input: arriving on its workspace swaps the editor in.
class PageStandIn : public QWidget {
    Q_OBJECT
public:
    explicit PageStandIn(int number);

    int number() const { return m_number; }
    // The title the window has until its address is known, so it can be told from the editor.
    QString firstTitle() const;
    // Once placed, a title people see in window lists.
    void setLabel(const QString &label);
    void setPicture(const QImage &picture);
    const QImage &picture() const { return m_picture; }

signals:
    // The user closed it (Super+W); not sent when Omastrator deletes it.
    void closedByUser(PageStandIn *standIn);

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    int m_number;
    QImage m_picture;
    QString m_label;
};
