#pragma once
#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPalette>
#include <QTimer>

class OmarchyStyle;

// Omarchy's colors.toml, read into the colours the chrome uses.
struct OmarchyColors {
    bool dark;
    QColor accent;
    QColor selection;
    QColor muted;
    QColor background;
    QColor darkBackground;
    QColor darkerBackground;
    QColor lighterBackground;
    QColor foreground;
    QColor darkForeground;
    QColor lightForeground;
    QColor brightForeground;
    QColor red;
    // `orange` where the theme has it, else `yellow`.
    QColor warning;

    // Component tokens (Graphite's surface_0 … error, docs in the theme's
    // colors.toml): surfaces from the screen up to the lift under the pointer,
    // text from titles down to disabled, and the soft accent for chosen fills.
    // A theme without them gets the same roles made from the colours above.
    bool components = false;
    QColor surface0{};
    QColor surface1{};
    QColor surface2{};
    QColor surface3{};
    QColor lift{};
    QColor text1{};
    QColor text2{};
    QColor text3{};
    QColor text4{};
    QColor accentSoft{};
    QColor onAccent{};

    // Throws std::runtime_error naming the missing or malformed key.
    static OmarchyColors parse(const QString &toml);
    // The macOS look, for a desktop without Omarchy.
    static OmarchyColors builtInDark();
    QPalette palette() const;
    // Labels and values: the theme's own type when it has components and the
    // families are installed, else empty (the desktop's font stands).
    QString labelFamily() const;
    QString valueFamily() const;
    friend bool operator==(const OmarchyColors &, const OmarchyColors &) = default;
};

// Applies the current theme to the application and follows switches.
class OmarchyTheme : public QObject {
    Q_OBJECT
public:
    static QString defaultDirectory();
    explicit OmarchyTheme(const QString &directory = defaultDirectory(), QObject *parent = nullptr);

    const OmarchyColors &colors() const { return m_colors; }
    void apply();

private:
    void watch();
    void use(const OmarchyColors &colors);

    const QString m_directory;
    OmarchyColors m_colors;
    OmarchyStyle *m_style;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
};
