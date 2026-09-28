// Temporary demo-mode harness for the second promo video. Not committed.
#include "Agent/Cli.h"
#include "Anywhere/Desk.h"
#include "Live/StaticServer.h"
#include "Rendering/VectorRenderer.h"
#include "UI/AgentPanels.h"
#include "UI/AgentSheets.h"
#include "UI/DesignController.h"
#include "UI/DesignSystemPanel.h"
#include "UI/DesktopLookPanel.h"
#include "UI/Menus.h"
#include "UI/NumberField.h"
#include "UI/OmarchyTheme.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SyncConfirmDialog.h"
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

namespace {
QString env(const char *name) { return qEnvironmentVariable(name); }
QString demo(const QString &path) { return QDir(env("OMA_DEMO")).filePath(path); }
QString shot(const QString &name) { return QDir(env("OMA_SHOTS")).filePath(name); }

template<typename T = QWidget>
T *shown(const QString &name)
{
    for (QWidget *widget : QApplication::allWidgets())
        if (widget->objectName() == name && widget->isVisible())
            if (T *typed = qobject_cast<T *>(widget))
                return typed;
    return nullptr;
}

void settle(int ms = 150)
{
    QTest::qWait(ms);
    QApplication::processEvents();
}

// Demo paths never reach a picture: the demo home reads as ~.
QString fix(QString text)
{
    text.replace(env("OMA_DEMO") + QStringLiteral("/bin/"), QString());
    text.replace(env("OMA_SCRUB"), QStringLiteral("~"));
    text.replace(QDir::homePath(), QStringLiteral("~"));
    return text;
}
void scrub()
{
    for (QWidget *widget : QApplication::allWidgets()) {
        if (auto *label = qobject_cast<QLabel *>(widget); label && label->text() != fix(label->text()))
            label->setText(fix(label->text()));
        if (auto *edit = qobject_cast<QPlainTextEdit *>(widget); edit && edit->toPlainText() != fix(edit->toPlainText()))
            edit->setPlainText(fix(edit->toPlainText()));
        if (auto *edit = qobject_cast<QTextEdit *>(widget); edit && edit->toPlainText() != fix(edit->toPlainText()))
            edit->setPlainText(fix(edit->toPlainText()));
        if (auto *line = qobject_cast<QLineEdit *>(widget); line && line->text() != fix(line->text()))
            line->setText(fix(line->text()));
        if (auto *tree = qobject_cast<QTreeWidget *>(widget)) {
            QList<QTreeWidgetItem *> items;
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                items << tree->topLevelItem(i);
            while (!items.isEmpty()) {
                QTreeWidgetItem *item = items.takeFirst();
                for (int c = 0; c < item->columnCount(); ++c)
                    if (item->text(c) != fix(item->text(c)))
                        item->setText(c, fix(item->text(c)));
                for (int i = 0; i < item->childCount(); ++i)
                    items << item->child(i);
            }
        }
    }
}

void save(const QImage &image, const QString &name)
{
    QVERIFY2(image.save(shot(name + QStringLiteral(".png"))), qPrintable(name));
    qInfo().noquote() << "SHOT" << name << image.size();
}

// A widget alone (a dialog or a panel), with its paths scrubbed.
void grabWidget(QWidget *widget, const QString &name)
{
    scrub();
    settle(80);
    save(widget->grab().toImage(), name);
}

// The window with every other visible window drawn where it floats, as the compositor would.
QImage composite(QWidget *main)
{
    scrub();
    settle(80);
    QImage image = main->grab().toImage();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget == main || !widget->isVisible() || widget->width() < 8)
            continue;
        const QRect at(widget->geometry().topLeft() - main->geometry().topLeft(), widget->size());
        for (int i = 14; i > 0; --i) {
            QPainterPath shadow;
            shadow.addRoundedRect(QRectF(at).adjusted(-i, -i + 6, i, i + 6), 10 + i, 10 + i);
            painter.fillPath(shadow, QColor(0, 0, 0, 7));
        }
        painter.drawPixmap(at.topLeft(), widget->grab());
    }
    return image;
}

void writeJson(const QString &name, const QJsonObject &object)
{
    QFile file(shot(name + QStringLiteral(".json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(object).toJson());
}

// The desktop as the promo draws it, pictured by headless Chromium from desktop.html: grabs crop it.
class PromoDesktop final : public DesktopSource {
public:
    QImage picture;
    std::optional<QPoint> pointer;
    std::vector<Hyprland::Window> clients;
    std::optional<QPoint> cursor() override { return pointer; }
    std::vector<Hyprland::Window> windows() override { return clients; }
    std::vector<Hyprland::Monitor> monitors() override
    {
        Hyprland::Monitor monitor;
        monitor.id = 0;
        monitor.name = QStringLiteral("DP-1");
        monitor.rect = QRect(0, 0, 1920, 1080);
        monitor.focused = true;
        monitor.activeWorkspace = 1;
        monitor.scale = 2;
        return {monitor};
    }
    std::optional<QJsonObject> accessible(const Hyprland::Window &, QPoint) override { return std::nullopt; }
    std::optional<QJsonObject> accessibleTree(const Hyprland::Window &, int, QString *error) override
    {
        if (error)
            *error = QStringLiteral("This app has no accessibility tree.");
        return std::nullopt;
    }
    std::optional<QColor> pixel(QPoint at) override
    {
        if (picture.isNull() || !picture.rect().contains(at))
            return std::nullopt;
        return picture.pixelColor(at);
    }
    std::vector<Hyprland::Layer> layers() override { return {{QStringLiteral("omarchy-bar"), QRect(0, 0, 1920, 26), QStringLiteral("DP-1")}}; }
    QImage grab(const QRect &rect, QString *) override { return picture.copy(rect); }
};
}

class ZzPromo2Tests : public QObject {
    Q_OBJECT

    std::unique_ptr<OmarchyTheme> m_theme;
    std::unique_ptr<ProjectWorkspace> m_workspace;
    std::unique_ptr<ProjectWorkspaceView> m_view;
    PromoDesktop *m_desktop = nullptr;
    StaticServer m_server;
    int m_overlayCopies = 0;

    AgentBridge &bridge() { return *m_view->agent(); }
    DesignController &design() { return bridge().designMode(); }
    QJsonObject call(const QString &action, const QJsonObject &params = {})
    {
        QJsonObject result;
        const QString failure = design().run(action, params, result);
        if (!failure.isEmpty())
            qWarning().noquote() << "DESIGN" << action << failure;
        return result;
    }
    // Points at `at` and lets the pointer rest, so the slower reads (colour under it) come in.
    void point(QPoint at)
    {
        m_desktop->pointer = at;
        for (int i = 0; i < 4; ++i) {
            design().mode().poll();
            settle(60);
        }
        settle(260);
    }
    // The design status as the overlay reads it, with each overlay picture copied next to it.
    void dump(const QString &name)
    {
        settle(700);
        QJsonObject status = bridge().statusExtras();
        QJsonObject designStatus = status["design"].toObject();
        QJsonArray overlays = designStatus["overlays"].toArray();
        for (int i = 0; i < overlays.size(); ++i) {
            QJsonObject each = overlays[i].toObject();
            const QString copy = shot(QStringLiteral("%1-art%2.png").arg(name).arg(i));
            QFile::remove(copy);
            QFile::copy(each["png"].toString(), copy);
            each["png"] = copy;
            overlays[i] = each;
        }
        designStatus["overlays"] = overlays;
        status["design"] = designStatus;
        writeJson(name, status);
        qInfo().noquote() << "STATUS" << name << QJsonDocument(designStatus["bar"].toObject()).toJson(QJsonDocument::Compact).left(300);
    }
    void picture(const QString &name) { m_desktop->picture = QImage(demo(QStringLiteral("out/") + name)); }
    QString live(const QString &js)
    {
        QString error;
        const QJsonValue value = bridge().liveSession().evaluate(js, &error);
        return error.isEmpty() ? value.toVariant().toString() : error;
    }
    void pageShot(const QString &name)
    {
        LiveSession &session = bridge().liveSession();
        CdpConnection &cdp = session.browser().cdp();
        QString error;
        cdp.callAndWait(QStringLiteral("Emulation.setDeviceMetricsOverride"),
                        {{"width", 1271}, {"height", 1030}, {"deviceScaleFactor", 2}, {"mobile", false}}, session.pageSession(), &error);
        settle(300);
        const QJsonObject shotResult = cdp.callAndWait(QStringLiteral("Page.captureScreenshot"), {{"format", "png"}}, session.pageSession(), &error);
        QFile file(shot(name + QStringLiteral(".png")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromBase64(shotResult["data"].toString().toLatin1()));
        file.close();
        cdp.callAndWait(QStringLiteral("Emulation.setDeviceMetricsOverride"),
                        {{"width", 1271}, {"height", 1030}, {"deviceScaleFactor", 1}, {"mobile", false}}, session.pageSession(), &error);
        settle(300);
        qInfo().noquote() << "PAGE" << name << error;
    }
    // The lifted art in outline view, at 2x on the monitor, in the accent: what Lift made, shape by shape.
    void outlines(const QString &surface, QPointF origin, const QString &name)
    {
        EditorSession &overlay = design().overlays().session();
        QImage image(3840, 2160, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(2, 2);
            painter.translate(origin);
            VectorRenderer::Options options;
            options.drawBackground = false;
            options.outlineMode = true;
            options.outlineWidth = 2;
            for (const QUuid &id : design().overlays().art(surface))
                VectorRenderer::drawObject(painter, *overlay.document(), id, options);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.resetTransform();
            painter.fillRect(image.rect(), QColor(0xff, 0x9e, 0x64));
        }
        save(image, name);
    }

private slots:
    void initTestCase()
    {
        QApplication::setApplicationName(QStringLiteral("Omastrator"));
        QSettings().setValue(QStringLiteral("roast/heat"), QStringLiteral("Spicy"));
        m_theme = std::make_unique<OmarchyTheme>(OmarchyTheme::defaultDirectory());
        QCOMPARE(m_server.serve(demo(QStringLiteral("site"))), QString());
        qputenv("EMBER_PORT", QByteArray::number(m_server.url().port()));
        m_workspace = std::make_unique<ProjectWorkspace>();
        m_view = std::make_unique<ProjectWorkspaceView>(*m_workspace);
        m_view->setProperty("background", true);
        m_view->resize(1440, 900);
        m_view->move(0, 0);
        QCOMPARE(bridge().startServer(env("OMASTRATOR_SOCKET")), QString());
        // The made-up site, in Omastrator's browser, as a site that isn't yours.
        const QString started = bridge().startLive(QUrl(QStringLiteral("http://emberline.coffee/")), QString());
        QVERIFY2(started.isEmpty(), qPrintable(started));
        QTRY_VERIFY_WITH_TIMEOUT(bridge().liveSession().state() == LiveSession::State::running, 60000);
        LiveSession &session = bridge().liveSession();
        QString error;
        session.browser().cdp().callAndWait(QStringLiteral("Emulation.setDeviceMetricsOverride"),
                                            {{"width", 1271}, {"height", 1030}, {"deviceScaleFactor", 1}, {"mobile", false}}, session.pageSession(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        settle(800);
        qInfo() << "PAGE SIZE" << live(QStringLiteral("innerWidth + 'x' + innerHeight + ' ' + location.href"));
        // Hide the "not your site" strip for the plain page pictures; it comes back for the overlay beats.
        auto desktop = std::make_unique<PromoDesktop>();
        m_desktop = desktop.get();
        Hyprland::Window terminal;
        terminal.address = QStringLiteral("0x1");
        terminal.className = QStringLiteral("Alacritty");
        terminal.title = QStringLiteral("~/Code/portfolio");
        terminal.rect = QRect(12, 38, 611, 1030);
        terminal.pid = 4242;
        terminal.workspace = 1;
        terminal.monitor = 0;
        terminal.focusHistory = 1;
        Hyprland::Window browser = terminal;
        browser.address = QStringLiteral("0x2");
        browser.className = QStringLiteral("chromium");
        browser.title = QStringLiteral("Emberline Coffee Roasters");
        browser.rect = QRect(637, 38, 1271, 1030);
        browser.pid = session.browser().processId();
        browser.focusHistory = 0;
        m_desktop->clients = {terminal, browser};
        picture(QStringLiteral("desktop-nightfall.png"));
        design().setSource(std::move(desktop));
        design().start();
    }

    // 2. Design mode: the terminal inspected, then Alt measuring between the panes.
    void designMode()
    {
        pageShot(QStringLiteral("page-plain"));
        call(QStringLiteral("on"));
        settle(300);
        dump(QStringLiteral("onboarding"));
        call(QStringLiteral("onboarding"), {{"question", "makes"}, {"values", QJsonArray{"rice", "web"}}});
        call(QStringLiteral("onboarding"), {{"question", "ai"}, {"values", QJsonArray{"some"}}});
        call(QStringLiteral("onboarding"), {{"finish", true}});
        point({330, 560});
        dump(QStringLiteral("hover-terminal"));
        design().mode().setAlt(true);
        settle(100);
        point({1100, 560});
        dump(QStringLiteral("measure-panes"));
        design().mode().setAlt(false);
        point({330, 560});
        design().mode().setAlt(true);
        point({330, 560});
        point({900, 280});
        dump(QStringLiteral("measure-headline"));
        design().mode().setAlt(false);
        settle(200);
    }

    // 3. A site that isn't yours: the bar on an element, a note and an arrow on top, then a card lifted into vectors.
    void anySite()
    {
        pageShot(QStringLiteral("page-strip"));
        // The headline: the page's bar.
        point({900, 290});
        dump(QStringLiteral("web-hover"));
        const QJsonObject bar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        call(QStringLiteral("action"), {{"id", "inspect"}, {"target", bar["target"]}});
        dump(QStringLiteral("web-inspect"));
        call(QStringLiteral("action"), {{"id", "closeDetail"}});
        // A note and an arrow at the call to action, drawn on top; the page stays click-through.
        call(QStringLiteral("draw"), {{"tool", "note"}, {"points", QJsonArray{QJsonArray{1080, 560}, QJsonArray{1330, 640}}},
                                      {"text", "Bigger, warmer CTA?"}});
        dump(QStringLiteral("web-note"));
        call(QStringLiteral("draw"), {{"tool", "arrow"}, {"points", QJsonArray{QJsonArray{1070, 590}, QJsonArray{880, 530}}}});
        dump(QStringLiteral("web-arrow"));
        call(QStringLiteral("deselect"));
        // The first card, lifted: every box and line of type as editable vectors, in place.
        point({870, 900});
        dump(QStringLiteral("web-hover-card"));
        QJsonObject lifted = call(QStringLiteral("lift"), {{"region", QJsonArray{685, 637, 376, 395}}, {"to", "overlay"}});
        settle(200);
        dump(QStringLiteral("web-lifting"));
        QTRY_VERIFY_WITH_TIMEOUT(!design().liftJob(), 60000);
        qInfo().noquote() << "LIFT" << design().message();
        dump(QStringLiteral("web-lifted"));
        const QString surface = bridge().statusExtras()["design"].toObject()["bar"].toObject()["surface"].toString();
        outlines(surface, QPointF(637, 38), QStringLiteral("lift-outlines"));
    }

    // 4. The page's design system: extracted, reviewed, and a push to your own code that asks first.
    void designSystem()
    {
        m_workspace->createDocument(QSizeF(1200, 800));
        settle();
        call(QStringLiteral("deselect"));
        point({900, 290});
        const QJsonObject bar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        int dialogs = 0;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            dialog.resize(dialog.sizeHint().expandedTo(QSize(760, 560)));
            dialog.show();
            settle(250);
            grabWidget(&dialog, QStringLiteral("confirm-extract"));
            ++dialogs;
            return true;
        });
        call(QStringLiteral("action"), {{"id", "extractSystem"}, {"target", bar["target"]}});
        settle(800);
        qInfo() << "EXTRACT dialogs" << dialogs;
        DesignSystemPanel *panel = m_view->menus()->designSystem();
        QVERIFY(panel);
        panel->window()->resize(460, 820);
        settle(400);
        grabWidget(panel->window(), QStringLiteral("ds-panel"));
        save(composite(m_view.get()), QStringLiteral("ds-window"));
        // Your own site's code: the dialog names each file, the repository and the branch. Cancelled here.
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            dialog.resize(dialog.sizeHint().expandedTo(QSize(760, 560)));
            dialog.show();
            settle(250);
            grabWidget(&dialog, QStringLiteral("confirm-push"));
            QFile text(shot(QStringLiteral("confirm-push.txt")));
            if (text.open(QIODevice::WriteOnly))
                text.write(fix(dialog.text()).toUtf8());
            return false;
        });
        qInfo().noquote() << "PUSH" << panel->pushToCode(QDir::home().filePath(QStringLiteral("Code/portfolio")));
        SyncConfirmDialog::setResponder({});
    }

    // 5. The site's colours become an Omarchy theme, saved (after the dialog) and applied; then a gap dragged.
    void makeItOmarchy()
    {
        DesignSystemPanel *panel = m_view->menus()->designSystem();
        QVERIFY(panel);
        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        qInfo().noquote() << "USE THEME" << panel->useOmarchyTheme();
        EditorSession &session = m_workspace->current().session;
        const std::pair<const char *, const char *> ember[] = {
            {"accent", "#ff6b35"}, {"selection", "#4a3226"}, {"muted", "#9c7b62"}, {"background", "#1f1510"},
            {"dark-background", "#1a110c"}, {"darker-background", "#140c08"}, {"lighter-background", "#2b1d16"},
            {"foreground", "#f3e6d4"}, {"dark-foreground", "#d9c4ae"}, {"light-foreground", "#fff1e0"},
            {"bright-foreground", "#fff8ee"}, {"orange", "#ff6b35"}, {"yellow", "#e9b949"}, {"green", "#8fae7e"},
            {"red", "#e0533d"}, {"blue", "#ff9a62"}, {"magenta", "#d98c6a"}, {"cyan", "#e9c79a"}, {"brown", "#7c5c46"}};
        for (const auto &[key, value] : ember) {
            const DesignToken *token = DesignTokens::named(session.document()->tokens, QStringLiteral("color/") + QLatin1String(key));
            if (!token) {
                qWarning() << "no token" << key;
                continue;
            }
            TokenValue colour;
            colour.color = QColor(QLatin1String(value));
            session.setTokenValue(token->id, colour);
        }
        settle(300);
        grabWidget(panel->window(), QStringLiteral("ds-theme-panel"));
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            dialog.resize(dialog.sizeHint().expandedTo(QSize(760, 560)));
            dialog.show();
            settle(250);
            grabWidget(&dialog, QStringLiteral("confirm-theme"));
            QFile text(shot(QStringLiteral("confirm-theme.txt")));
            if (text.open(QIODevice::WriteOnly))
                text.write(fix(dialog.text()).toUtf8());
            return true;
        });
        qInfo().noquote() << "SAVE THEME" << panel->saveOmarchyTheme(QStringLiteral("Ember"), true);
        SyncConfirmDialog::setResponder({});
        panel->window()->hide();
        m_view->hide();
        settle(1500);
        // The shell re-tints; the overlay follows the theme. Design mode, on the Ember desktop.
        picture(QStringLiteral("desktop-ember.png"));
        call(QStringLiteral("deselect"));
        point({330, 560});
        const QJsonObject bar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        call(QStringLiteral("action"), {{"id", "gapsAndBorders"}, {"target", bar["target"]}});
        settle(500);
        qInfo().noquote() << "EDITS" << QJsonDocument(design().lookEdits()).toJson(QJsonDocument::Compact);
        if (DesktopLookPanel *look = design().lookPanel())
            grabWidget(look->window(), QStringLiteral("look-panel"));
        qInfo().noquote() << "EDITS2" << QJsonDocument(design().lookEdits()).toJson(QJsonDocument::Compact);
        point({330, 560});
        dump(QStringLiteral("look-gap"));
        qInfo().noquote() << "EDITS3" << QJsonDocument(design().lookEdits()).toJson(QJsonDocument::Compact);
        call(QStringLiteral("look"), {{"op", "discard"}});
        if (DesktopLookPanel *look = design().lookPanel()) {
            NumberField *gap = look->field(QStringLiteral("gapsIn"));
            gap->field->setText(QStringLiteral("14"));
            gap->commit();
        }
        settle(400);
        if (DesktopLookPanel *look = design().lookPanel())
            grabWidget(look->window(), QStringLiteral("look-panel-14"));
        dump(QStringLiteral("look-gap-14"));
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            dialog.resize(dialog.sizeHint().expandedTo(QSize(760, 560)));
            dialog.show();
            settle(250);
            grabWidget(&dialog, QStringLiteral("confirm-look"));
            QFile text(shot(QStringLiteral("confirm-look.txt")));
            if (text.open(QIODevice::WriteOnly))
                text.write(fix(dialog.text()).toUtf8());
            return true;
        });
        qInfo().noquote() << "LOOK SAVE" << QJsonDocument(call(QStringLiteral("look"), {{"op", "save"}})).toJson(QJsonDocument::Compact);
        SyncConfirmDialog::setResponder({});
        if (DesktopLookPanel *look = design().lookPanel())
            look->window()->hide();
        QFile log(demo(QStringLiteral("control/commands.log")));
        if (log.open(QIODevice::ReadOnly))
            qInfo().noquote() << "COMMANDS" << fix(QString::fromUtf8(log.readAll()));
    }

    // 6. The Desk: captures as labelled frames, a suggestion, and wallpaper variations.
    void desk()
    {
        picture(QStringLiteral("desktop-ember-14.png"));
        call(QStringLiteral("deselect"));
        // The note and arrow's layer, with the page under it, to the Desk from the bar's suggestion.
        point({900, 290});
        const QJsonObject webBar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        call(QStringLiteral("send"), {{"destination", "desk"}, {"surface", webBar["surface"]}});
        point({330, 560});
        const QJsonObject termBar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        call(QStringLiteral("action"), {{"id", "capture"}, {"target", termBar["target"]}});
        call(QStringLiteral("lift"), {{"region", QJsonArray{685, 637, 376, 395}}, {"to", "desk"}});
        QTRY_VERIFY_WITH_TIMEOUT(!design().liftJob(), 60000);
        point({960, 12});
        const QJsonObject shellBar = bridge().statusExtras()["design"].toObject()["bar"].toObject();
        dump(QStringLiteral("shell-bar"));
        call(QStringLiteral("action"), {{"id", "capture"}, {"target", shellBar["target"]}});
        settle(500);
        QCOMPARE(design().desk(QStringLiteral("window")), QString());
        settle(600);
        ProjectTab *deskTab = design().deskTab();
        QVERIFY(deskTab);
        deskTab->session.deselectAll();
        deskTab->session.zoomToFit();
        settle(500);
        save(composite(m_view.get()), QStringLiteral("desk"));
        const auto frames = Desk::frames(*deskTab->session.document());
        for (const auto &frame : frames)
            qInfo().noquote() << "FRAME" << frame.second;
        if (!frames.empty()) {
            deskTab->session.select({frames.front().first});
            settle(400);
            save(composite(m_view.get()), QStringLiteral("desk-selected"));
            deskTab->session.deselectAll();
        }

        // A wallpaper in the new theme: Generate, four variations, one kept.
        m_workspace->createDocument(QSizeF(1600, 900));
        settle(300);
        QDialog *sheet = AgentSheets::generate(bridge(), m_view.get(), QStringLiteral("A wallpaper for my Ember theme: warm, quiet, 16:9"));
        settle(300);
        save(composite(m_view.get()), QStringLiteral("generate-sheet"));
        sheet->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(!bridge().rounds().empty() && !bridge().rounds().front().variations.empty(), 30000);
        settle(600);
        if (QWidget *panel = shown(QStringLiteral("variationsPanel"))) {
            panel->window()->move(m_view->geometry().topLeft() + QPoint(860, 120));
            settle(200);
            grabWidget(panel->window(), QStringLiteral("variations-panel"));
        }
        save(composite(m_view.get()), QStringLiteral("variations-window"));
        shown<QToolButton>(QStringLiteral("variation:0:0"))->click();
        settle(500);
        save(composite(m_view.get()), QStringLiteral("variation-picked"));
        bridge().keepProposal();
        settle(300);
        QJsonObject result;
        qInfo().noquote() << "WALLPAPER" << design().look({{"op", "wallpaperFromArtboard"}}, result) << result["wallpaper"].toString();
        QFile::copy(result["wallpaper"].toString(), shot(QStringLiteral("wallpaper-low-sun.png")));
    }

    // 7. Roast My Design, on the wallpaper: Spicy, then one Savage line; the fixes; variations from them; Keep.
    void roast()
    {
        if (QWidget *panel = shown(QStringLiteral("variationsPanel")))
            panel->window()->hide();
        m_view->findChild<QToolButton *>(QStringLiteral("roastMyDesign"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(bridge().roastResult().has_value(), 30000);
        settle(500);
        QDialog *panel = shown<QDialog>(QStringLiteral("roastPanel"));
        QVERIFY(panel);
        panel->move(m_view->geometry().topLeft() + QPoint(820, 140));
        RoastPanel *roastPanel = panel->findChild<RoastPanel *>();
        roastPanel->showPage(0);
        settle(300);
        grabWidget(panel, QStringLiteral("roast-spicy"));
        save(composite(m_view.get()), QStringLiteral("roast-window"));
        auto *heat = shown<QComboBox>(QStringLiteral("roastHeat"));
        QVERIFY(heat);
        heat->setCurrentIndex(heat->findText(QStringLiteral("Savage")));
        emit heat->activated(heat->currentIndex());
        settle(100);
        shown<QAbstractButton>(QStringLiteral("roastAgain"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(bridge().roastResult() && bridge().roastResult()->roast.contains(QLatin1String("buffering")), 30000);
        settle(500);
        panel = shown<QDialog>(QStringLiteral("roastPanel"));
        roastPanel = panel->findChild<RoastPanel *>();
        roastPanel->showPage(0);
        settle(300);
        grabWidget(panel, QStringLiteral("roast-savage"));
        roastPanel->showPage(1);
        settle(300);
        grabWidget(panel, QStringLiteral("roast-fixes"));
        roastPanel->showPage(2);
        settle(300);
        grabWidget(panel, QStringLiteral("roast-next"));
        shown<QAbstractButton>(QStringLiteral("makeVariations"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(bridge().rounds().size() >= 2 && !bridge().rounds().back().variations.empty(), 30000);
        settle(600);
        panel->hide();
        const int round = int(bridge().rounds().size()) - 1;
        if (QWidget *variations = shown(QStringLiteral("variationsPanel"))) {
            variations->window()->move(m_view->geometry().topLeft() + QPoint(860, 120));
            settle(200);
            grabWidget(variations->window(), QStringLiteral("variations-fixed"));
            shown<QToolButton>(QStringLiteral("variation:%1:0").arg(round))->click();
            settle(500);
            variations->window()->hide();
        }
        settle(300);
        save(composite(m_view.get()), QStringLiteral("roast-keep"));
        if (QWidget *bar = shown(QStringLiteral("proposalBar")))
            grabWidget(bar, QStringLiteral("proposal-bar"));
        bridge().keepProposal();
        settle(300);
        QJsonObject result;
        qInfo().noquote() << "WALLPAPER" << design().look({{"op", "wallpaperFromArtboard"}}, result);
        QFile::copy(result["wallpaper"].toString(), shot(QStringLiteral("wallpaper-reworked.png")));
        QFile log(demo(QStringLiteral("control/tasks.log")));
        if (log.open(QIODevice::ReadOnly))
            qInfo().noquote() << "TASKS" << log.readAll();
    }

    void cleanupTestCase()
    {
        bridge().liveSession().stop();
        call(QStringLiteral("off"));
    }
};

// Run as `ZzPromo2Tests agent …`, this binary is the Omastrator CLI the demo agent calls.
int main(int argc, char **argv)
{
    if (argc > 1 && Cli::handles(argv[1])) {
        QCoreApplication app(argc, argv);
        return Cli::run(QCoreApplication::arguments().mid(1));
    }
    QApplication app(argc, argv);
    ZzPromo2Tests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}
#include "ZzPromo2Tests.moc"
