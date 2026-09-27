#pragma once
#include <QFrame>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

class Menus;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;

// Ctrl+K (Figma's Actions menu): one box that reaches every command, tool, panel, recent file, setting and AI action
// by name, with its current key. Text that reads as a request goes to the agent instead, as a preview to keep or discard.
class CommandPalette : public QFrame {
    Q_OBJECT
public:
    CommandPalette(Menus &menus, QWidget *window);

    struct Command {
        // "action:group", "tool:pen", "recent:<path>", "setting:smartGuides", "ai:roast", "ask".
        QString id;
        QString title;
        // Where it lives: "Object ▸ Path", "Tools", "AI".
        QString where;
        // The current key, as remapped.
        QString shortcut;
        // More words it's found by.
        QString keywords;
        bool enabled = true;
        // A checked entry or a setting that's on.
        bool on = false;
        // Runs it; returns why it couldn't, or empty.
        std::function<QString()> run;
    };

    void open();
    void close();
    QLineEdit *search() const { return m_search; }
    QListWidget *list() const { return m_list; }
    // The line under the list: why the last choice couldn't run.
    QString message() const;
    // Everything the palette reaches right now, gathered as it opened.
    const std::vector<Command> &commands() const { return m_commands; }
    // What the list shows for `query`, best first. Recently run commands float up; an Ask row leads when the text
    // starts with "?" or reads as a request rather than a command's name, and closes the list otherwise.
    std::vector<Command> results(const QString &query) const;
    // Runs a shown row, as Enter or a click does; returns why it couldn't, or empty.
    QString runRow(int row);

    // How well `query` matches `text`, higher is better: whole, prefix, every word's start, substring, initials,
    // then letters in order. Nullopt when it doesn't match.
    static std::optional<int> score(const QString &query, const QString &text);
    // A request for new art ("a fox logo", "draw a cat") rather than a change to what's there.
    static bool asksForNewArt(const QString &text);
    // Command ids run lately, newest first.
    static QStringList recent();
    static void remember(const QString &id);
    static constexpr int recentLimit = 8;
    // A top match below this, for text of a few words, reads as a request instead.
    static constexpr int commandScore = 600;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void gather();
    void gatherMenus();
    void gatherContext();
    void gatherRest();
    void populate(const QString &query);
    void place();
    void step(int by);
    std::optional<Command> ask(const QString &request) const;

    Menus &m_menus;
    QLineEdit *const m_search;
    QListWidget *const m_list;
    QLabel *const m_footer;
    std::vector<Command> m_commands;
    std::vector<Command> m_shown;
    QString m_agentName;
    QPointer<QWidget> m_returnFocus;
    // The selection's right-click menu, kept while open: its own entries (Align, Pathfinder, Isolate) run from it.
    QPointer<QMenu> m_context;
};
