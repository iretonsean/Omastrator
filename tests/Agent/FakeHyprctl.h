#pragma once
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

// A stand-in for hyprctl (OMASTRATOR_HYPRCTL): logs each call's arguments, one line per call, and answers
// `-j <query>` from `<script>.<query>.json` files the test writes, so it can change what Hyprland "says".
class FakeHyprctl {
public:
    explicit FakeHyprctl(const QString &directory) : m_path(QDir(directory).filePath(QStringLiteral("hyprctl")))
    {
        QFile file(m_path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$0.log\"\n"
                   "if [ \"$1\" = \"-j\" ] && [ -f \"$0.$2.json\" ]; then cat \"$0.$2.json\"; fi\n"
                   "if [ -f \"$0.fail\" ]; then echo 'no' >&2; exit 1; fi\n"
                   "if [ \"$1\" != \"-j\" ] && [ -f \"$0.faildispatch\" ]; then echo 'no' >&2; exit 1; fi\n");
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    QString path() const { return m_path; }
    void answer(const QString &query, const QByteArray &json) const { write(m_path + QStringLiteral(".") + query + QStringLiteral(".json"), json); }
    // Every call now fails (exit 1).
    void setFailing(bool failing) const
    {
        if (failing)
            write(m_path + QStringLiteral(".fail"), "1");
        else
            QFile::remove(m_path + QStringLiteral(".fail"));
    }
    // Queries still answer; every dispatcher fails (exit 1).
    void setDispatchFailing(bool failing) const
    {
        if (failing)
            write(m_path + QStringLiteral(".faildispatch"), "1");
        else
            QFile::remove(m_path + QStringLiteral(".faildispatch"));
    }
    void clearLog() const { QFile::remove(m_path + QStringLiteral(".log")); }
    QStringList log() const
    {
        QFile file(m_path + QStringLiteral(".log"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    // The log without the queries (`-j …`), which only read.
    QStringList dispatches() const
    {
        QStringList result;
        for (const QString &line : log()) {
            if (!line.startsWith(QLatin1String("-j ")))
                result << line;
        }
        return result;
    }

private:
    static void write(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(bytes);
    }
    QString m_path;
};
