#pragma once
#include <QByteArray>
#include <QtGlobal>

// Puts an environment variable back to what it was when this was made, on every way out of the test. A test that pretends
// there is no Chromium and then unsets OMASTRATOR_CHROMIUM would drop the one the run was given, and every later test that
// needs Chromium would skip.
class RestoreEnvironment {
public:
    explicit RestoreEnvironment(const char *name) : m_name(name), m_set(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name)) {}
    ~RestoreEnvironment()
    {
        if (m_set)
            qputenv(m_name, m_value);
        else
            qunsetenv(m_name);
    }
    RestoreEnvironment(const RestoreEnvironment &) = delete;
    RestoreEnvironment &operator=(const RestoreEnvironment &) = delete;

private:
    const char *m_name;
    bool m_set;
    QByteArray m_value;
};
