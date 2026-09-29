#pragma once
#include <QFont>
#include <QFontInfo>

// A bare container has no fonts, and Qt still lists its Sans Serif, Serif and Monospace names:
// what tells the two apart is whether the default family resolves to a real one.
inline bool haveInstalledFonts()
{
    return !QFontInfo(QFont(QStringLiteral("Sans Serif"))).family().isEmpty();
}
