#include "Document/EditorSession.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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
};

QTEST_GUILESS_MAIN(ShellPluginTests)
#include "ShellPluginTests.moc"
