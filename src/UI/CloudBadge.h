#pragma once
#include <QIcon>
#include <QString>

// A service's badge: its letters on its colour, drawn, so no brand artwork ships.
namespace CloudBadge {
QIcon icon(const QString &type);
}
