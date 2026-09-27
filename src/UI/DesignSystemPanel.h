#pragma once
#include "System/SiteExtract.h"
#include <QPointer>
#include <QUrl>
#include <QWidget>
#include <functional>

class EditorSession;
class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;

// Window ▸ Design System (docs/DESIGN-SYSTEMS.md): the document's tokens and
// components, and the places a system lives (the project's code, the global
// library, a site, the Omarchy theme). Every push and pull goes through the
// confirmation dialog.
class DesignSystemPanel : public QWidget {
    Q_OBJECT
public:
    // `session` is asked again each time: it follows the front tab, or an overlay.
    explicit DesignSystemPanel(std::function<EditorSession *()> session, QWidget *parent = nullptr);
    void follow();

    QTabWidget *tabs() const { return m_tabs; }
    QTreeWidget *tokenList() const { return m_tokens; }
    QTreeWidget *componentList() const { return m_components; }
    // The line under the tabs: what the last action did, or why it couldn't.
    QString message() const;

    // Sources, as the buttons run them. Each builds a plan and shows it for confirmation first.
    QString pullFromCode(const QString &folder);
    QString pushToCode(const QString &folder);
    QString saveToLibrary(const QString &name);
    QString useLibrary(const QString &name);
    QString placeFromLibrary(const QString &name, const QString &set);
    // Reads `url` in a headless browser of Omastrator's own, then offers the proposal.
    QString extractSite(const QUrl &url);
    QString useProposal(const SiteExtract::Proposal &proposal);
    QString useOmarchyTheme();
    QString saveOmarchyTheme(const QString &name, bool apply);

private:
    EditorSession *session() const;
    QWidget *buildTokens();
    QWidget *buildComponents();
    QWidget *buildSources();
    void rebuild();
    void rebuildTokens();
    void rebuildComponents();
    void refreshSources();
    void editToken(QTreeWidgetItem *item);
    void tokenMenu(const QPoint &at);
    void componentMenu(const QPoint &at);
    void addToken(int kind);
    QString say(const QString &text);
    QString documentName() const;

    const std::function<EditorSession *()> m_sessionOf;
    QPointer<EditorSession> m_session;
    QMetaObject::Connection m_watch;
    QTabWidget *m_tabs = nullptr;
    QTreeWidget *m_tokens = nullptr;
    QComboBox *m_mode = nullptr;
    QTreeWidget *m_components = nullptr;
    QWidget *m_instanceBox = nullptr;
    QLineEdit *m_folder = nullptr;
    QLabel *m_detected = nullptr;
    QComboBox *m_library = nullptr;
    QTreeWidget *m_libraryComponents = nullptr;
    QLineEdit *m_site = nullptr;
    QLabel *m_theme = nullptr;
    QLineEdit *m_themeName = nullptr;
    QCheckBox *m_applyTheme = nullptr;
    QLabel *m_message = nullptr;
    bool m_rebuilding = false;
};
