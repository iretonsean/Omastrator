#include "Document/EditorSession.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTest>

// The omarchy-shell plugins in shell/: each manifest keeps the contract in
// /usr/share/omarchy/shell/README.md, and the island can draw every tool.
class ShellPluginTests : public QObject {
    Q_OBJECT

private:
    QDir m_shell{QStringLiteral(OMASTRATOR_SOURCE_DIR "/shell")};

    QString read(const QString &path)
    {
        QFile file(m_shell.filePath(path));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    // The text of the first `name {` block, braces balanced.
    static QString block(const QString &source, const QString &name)
    {
        const qsizetype start = source.indexOf(name + QStringLiteral(" {"));
        if (start < 0)
            return {};
        int depth = 0;
        for (qsizetype i = source.indexOf(QLatin1Char('{'), start); i < source.size(); ++i) {
            if (source[i] == QLatin1Char('{'))
                ++depth;
            else if (source[i] == QLatin1Char('}') && --depth == 0)
                return source.mid(start, i - start + 1);
        }
        return {};
    }

private slots:
    void manifestsKeepTheContract()
    {
        const QStringList plugins = m_shell.entryList({QStringLiteral("omastrator.*")}, QDir::Dirs);
        QVERIFY(plugins.contains(QStringLiteral("omastrator.island")));
        for (const QString &plugin : plugins) {
            QJsonParseError error{};
            const QJsonObject manifest = QJsonDocument::fromJson(read(plugin + QStringLiteral("/manifest.json")).toUtf8(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(plugin));
            QCOMPARE(manifest["schemaVersion"].toInt(), 1);
            // The folder is named by the id, as `omarchy plugin` installs it.
            QCOMPARE(manifest["id"].toString(), plugin);
            for (const char *key : {"name", "version", "description"})
                QVERIFY2(!manifest[QLatin1String(key)].toString().isEmpty(), key);
            const QJsonObject entries = manifest["entryPoints"].toObject();
            QVERIFY(!manifest["kinds"].toArray().isEmpty());
            for (const QJsonValue &kind : manifest["kinds"].toArray()) {
                const QString key = kind.toString() == QLatin1String("bar-widget") ? QStringLiteral("barWidget") : kind.toString();
                QVERIFY2(entries.contains(key), qPrintable(plugin + QLatin1Char(' ') + key));
                QVERIFY2(!read(plugin + QLatin1Char('/') + entries[key].toString()).isEmpty(), qPrintable(entries[key].toString()));
            }
        }
    }

    void theIslandDrawsEveryToolItOffers()
    {
        const QString icons = read(QStringLiteral("omastrator-ui/Icons.js"));
        const QString island = read(QStringLiteral("omastrator.island/Island.qml"));
        QVERIFY(!icons.isEmpty() && !island.isEmpty());
        auto hasIcon = [&](const QString &name) { return icons.contains(QStringLiteral("\n  ") + name + QStringLiteral(": [")); };
        const QRegularExpression item(QStringLiteral("\\{ id: \"(\\w+)\"(?:, icon: \"(\\w+)\")?"));
        // Draw mode's buttons are the app's own tools.
        const qsizetype draw = island.indexOf(QStringLiteral("draw: ["));
        const QString drawList = island.mid(draw, island.indexOf(QLatin1Char(']'), draw) - draw);
        int tools = 0;
        for (auto match = item.globalMatch(drawList); match.hasNext(); ++tools)
            QVERIFY2(toolNamed(match.next().captured(1)).has_value(), qPrintable(drawList));
        QVERIFY(tools >= 13);
        // Every button anywhere has an icon.
        for (auto match = item.globalMatch(island); match.hasNext();) {
            const auto found = match.next();
            const QString name = found.captured(2).isEmpty() ? found.captured(1) : found.captured(2);
            QVERIFY2(hasIcon(name), qPrintable(name));
        }
        for (const char *mode : {"normal", "draw", "capture", "ai", "live", "previous", "next"})
            QVERIFY2(hasIcon(QLatin1String(mode)), mode);
    }

    void everyQmlFileParses()
    {
        QString qmlformat = QStandardPaths::findExecutable(QStringLiteral("qmlformat"), {QLibraryInfo::path(QLibraryInfo::BinariesPath)});
        if (qmlformat.isEmpty())
            qmlformat = QStandardPaths::findExecutable(QStringLiteral("qmlformat"));
        if (qmlformat.isEmpty())
            QSKIP("qmlformat isn't installed");
        int files = 0;
        for (const QString &plugin : m_shell.entryList({QStringLiteral("omastrator*")}, QDir::Dirs)) {
            for (const QString &name : QDir(m_shell.filePath(plugin)).entryList({QStringLiteral("*.qml")}, QDir::Files)) {
                QProcess process;
                process.start(qmlformat, {m_shell.filePath(plugin + QLatin1Char('/') + name)});
                QVERIFY(process.waitForFinished(30000));
                QVERIFY2(process.exitCode() == 0, qPrintable(plugin + QLatin1Char('/') + name + QLatin1Char(' ') + QString::fromUtf8(process.readAllStandardError())));
                ++files;
            }
        }
        QVERIFY(files >= 4);
    }

    // Sizing the surface to the tooltip moved the pill from under the pointer,
    // which hid the tooltip and moved it back: hover flickered many times a second.
    void theIslandNeverResizesOnHover()
    {
        const QString window = block(read(QStringLiteral("omastrator.island/Island.qml")), QStringLiteral("PanelWindow"));
        QVERIFY(!window.isEmpty());
        for (const char *anchor : {"anchors.top: true", "anchors.left: true", "anchors.right: true"})
            QVERIFY2(window.contains(QLatin1String(anchor)), anchor);
        // Only the window's own lines count, not its children's.
        const QString own = window.left(window.indexOf(QStringLiteral("Rectangle {")));
        QVERIFY(!own.contains(QStringLiteral("implicitWidth")));
        const QRegularExpression height(QStringLiteral("implicitHeight:([^\\n]*)"));
        const QString heightBinding = height.match(own).captured(1);
        QVERIFY(!heightBinding.isEmpty());
        for (const char *hoverDependent : {"tip", "hover", "pill.", "row.", "expanded", "activity"})
            QVERIFY2(!heightBinding.contains(QLatin1String(hoverDependent)), qPrintable(heightBinding));
        QVERIFY(own.contains(QStringLiteral("mask: Region { item: pill }")));
    }

    // A binary that can't start never sends exited; the island would never connect.
    void theStatusStreamRetriesAFailedStart()
    {
        const QString process = block(read(QStringLiteral("omastrator-ui/Status.qml")), QStringLiteral("Process"));
        QVERIFY(process.contains(QStringLiteral("onRunningChanged")));
        QVERIFY(process.contains(QStringLiteral("restart.start()")));
    }
};

QTEST_GUILESS_MAIN(ShellPluginTests)
#include "ShellPluginTests.moc"
