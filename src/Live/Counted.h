#pragma once
#include <QString>

// "1 edit", "2 edits": a message names a count with the noun in agreement.
inline QString counted(qsizetype count, const QString &noun)
{
    return QStringLiteral("%1 %2%3").arg(count).arg(noun, count == 1 ? QString() : QStringLiteral("s"));
}
