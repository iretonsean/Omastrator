#pragma once
#include <QImage>
#include <QString>
#include <QUuid>

// What the canvas asks of whoever streams a Browser View's page (docs/BROWSER-VIEW.md), so the canvas
// needn't link the browser code.
class BrowserViewHost {
public:
    virtual ~BrowserViewHost() = default;
    // The newest picture of the page, or a null image to draw the frame's stored one.
    virtual QImage picture(const QUuid &frame) const = 0;
    // A line drawn over the frame, as "Paused by reset"; empty when the page shows as it is.
    virtual QString message(const QUuid &frame) const = 0;
};
