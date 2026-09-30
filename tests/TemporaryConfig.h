#pragma once
#include <QByteArray>
#include <QSettings>
#include <QTemporaryDir>

// Every test executable already runs this before main() (tests/TestConfigIsolation.cpp); calling it again is harmless.
// Windows and welcome tabs read presets.json; a test must never reach the user's own config.
inline void useTemporaryConfig()
{
    static QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    // Test mode puts QSettings in ~/.qttest, shared by the same test running at once in another worktree.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
}
