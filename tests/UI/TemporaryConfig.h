#pragma once
#include <QByteArray>
#include <QTemporaryDir>

// Windows and welcome tabs read presets.json; a test must never reach the user's own config.
inline void useTemporaryConfig()
{
    static QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
}
