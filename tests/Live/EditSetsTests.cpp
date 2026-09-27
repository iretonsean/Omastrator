#include "Live/EditSets.h"
#include "Live/LiveSession.h"
#include <QTemporaryDir>
#include <QtTest>

// Edit sets for sites that aren't yours (docs/ANYWHERE.md): kept per origin,
// merged by what they change, toggled, and exported as CSS or a userstyle.
class EditSetsTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    const QString m_origin = QStringLiteral("https://example.com");

    static EditSets::Edit edit(const QString &path, const QString &selector, const QString &property, const QString &value,
                               const QString &before, const QString &token = QString())
    {
        return {path, selector, property, value, before, token, {}, {}};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
    }

    void originsAndPaths()
    {
        QCOMPARE(EditSets::originOf(QUrl(QStringLiteral("https://example.com/pricing?x=1#top"))), QStringLiteral("https://example.com"));
        QCOMPARE(EditSets::originOf(QUrl(QStringLiteral("http://127.0.0.1:5173/"))), QStringLiteral("http://127.0.0.1:5173"));
        QCOMPARE(EditSets::pathOf(QUrl(QStringLiteral("https://example.com"))), QStringLiteral("/"));
        QCOMPARE(EditSets::pathOf(QUrl(QStringLiteral("https://example.com/pricing"))), QStringLiteral("/pricing"));
        QCOMPARE(EditSets::originOf(QUrl::fromLocalFile(QStringLiteral("/tmp/site/index.html"))), QStringLiteral("file:///tmp/site"));
        QCOMPARE(EditSets::pathOf(QUrl::fromLocalFile(QStringLiteral("/tmp/site/index.html"))), QStringLiteral("index.html"));
        QVERIFY(EditSets::path().startsWith(m_directory.path()));
    }

    void keepingMergesByWhatChanges()
    {
        QCOMPARE(EditSets::suggestedName(m_origin), QStringLiteral("Edits 1"));
        QVERIFY(EditSets::keep(m_origin, QString(), {edit("/", "#a", "color", "red", "blue")}).contains(QLatin1String("name")));
        QVERIFY(EditSets::keep(m_origin, QStringLiteral("Brand"), {}).contains(QLatin1String("no edits")));
        QVERIFY(EditSets::keep(m_origin, QStringLiteral("Brand"), {edit("/", "#title", "color", "#e11d48", "#0f172a", "--brand"),
                                                                    edit("/", "#title", "text", "Hi", "Hello")})
                    .isEmpty());
        // The same thing changed again: the new value, the page's first value.
        QVERIFY(EditSets::keep(m_origin, QStringLiteral("Brand"), {edit("/", "#title", "color", "#111111", "#e11d48")}).isEmpty());
        std::vector<EditSets::Set> sets = EditSets::read(m_origin);
        QCOMPARE(sets.size(), size_t(1));
        QCOMPARE(sets.front().edits.size(), size_t(2));
        QCOMPARE(sets.front().edits.front().value, QStringLiteral("#111111"));
        QCOMPARE(sets.front().edits.front().before, QStringLiteral("#0f172a"));
        QVERIFY(sets.front().enabled);
        QCOMPARE(EditSets::suggestedName(m_origin), QStringLiteral("Edits 1"));
        QVERIFY(EditSets::keep(m_origin, QStringLiteral("Edits 1"), {edit("/pricing", ".card", "padding-top", "12px", "20px")}).isEmpty());
        QCOMPARE(EditSets::suggestedName(m_origin), QStringLiteral("Edits 2"));
        // Other sites keep their own.
        QVERIFY(EditSets::read(QStringLiteral("https://other.example")).empty());

        // Only enabled sets come back, and only on their own page.
        QCOMPARE(EditSets::active(m_origin, QStringLiteral("/")).size(), size_t(2));
        QCOMPARE(EditSets::active(m_origin, QStringLiteral("/pricing")).size(), size_t(1));
        QVERIFY(EditSets::setEnabled(m_origin, QStringLiteral("Brand"), false).isEmpty());
        QVERIFY(EditSets::active(m_origin, QStringLiteral("/")).empty());
        QVERIFY(EditSets::setEnabled(m_origin, QStringLiteral("Nope"), true).contains(QLatin1String("Nope")));
        QVERIFY(EditSets::setEnabled(m_origin, QStringLiteral("Brand"), true).isEmpty());
        QVERIFY(EditSets::remove(m_origin, QStringLiteral("Edits 1")).isEmpty());
        QCOMPARE(EditSets::read(m_origin).size(), size_t(1));
    }

    void fromALiveEdit()
    {
        LiveEdit live;
        live.selector = QStringLiteral("#title");
        live.property = QStringLiteral("color");
        live.before = QStringLiteral("#000000");
        live.after = QStringLiteral("#e11d48");
        live.token = QStringLiteral("--brand");
        live.addClass = QStringLiteral("text-rose-600");
        const EditSets::Edit made = EditSets::Edit::fromLive(live);
        QCOMPARE(made.path, QStringLiteral("/"));
        QCOMPARE(made.value, QStringLiteral("#e11d48"));
        QCOMPARE(made.addClass, QStringLiteral("text-rose-600"));
        QCOMPARE(EditSets::Edit::fromJson(made.toJson()), made);
    }

    void cssAndUserstyle()
    {
        const std::vector<EditSets::Edit> edits{edit("/", "#title", "color", "#e11d48", "#0f172a", "--brand"),
                                                edit("/", "#title", "font-size", "40px", "32px"),
                                                edit("/pricing", ".card", "border-radius", "16px", "8px"),
                                                edit("/", "#title", "text", "Hi */ there", "Hello")};
        const QString css = EditSets::css(m_origin, QStringLiteral("Brand"), edits, false);
        QVERIFY(css.contains(QLatin1String("Not your site: these changes stay on this machine.")));
        QVERIFY2(css.contains(QLatin1String("#title {\n  color: var(--brand) !important;\n  font-size: 40px !important;\n}")), qPrintable(css));
        QVERIFY(css.contains(QLatin1String("/* https://example.com/pricing */\n.card {\n  border-radius: 16px !important;\n}")));
        // Text can't be CSS; it's listed, and can't close the comment early.
        QVERIFY(css.contains(QLatin1String("Text changes can't be made with CSS")));
        QVERIFY(css.contains(QLatin1String("\"Hi * / there\"")));
        QCOMPARE(css.count(QLatin1String("*/")), css.count(QLatin1String("/*")));

        const QString userstyle = EditSets::css(m_origin, QStringLiteral("Brand"), edits, true);
        QVERIFY(userstyle.startsWith(QLatin1String("/* ==UserStyle==\n@name           example.com: Brand\n")));
        QVERIFY(userstyle.contains(QLatin1String("@-moz-document url-prefix(\"https://example.com/\") {\n  #title {\n    color: var(--brand) !important;")));
        QVERIFY(userstyle.contains(QLatin1String("@-moz-document url-prefix(\"https://example.com/pricing\") {")));

        const QString diff = EditSets::diff(edits);
        QVERIFY2(diff.contains(QStringLiteral("/ #title { color: #0f172a → #e11d48 (the page's --brand) }")), qPrintable(diff));
        QVERIFY(diff.contains(QStringLiteral("/ #title { text: \"Hello\" → \"Hi * / there\" }")));
    }
};

QTEST_GUILESS_MAIN(EditSetsTests)
#include "EditSetsTests.moc"
