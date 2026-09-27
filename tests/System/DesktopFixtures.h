#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

// A made-up Omarchy desktop in a temporary HOME for the phase 4 tests: Omarchy's
// defaults, a Graphite theme, the user's Hyprland Lua config, shell.json and
// shell.toml, a wallpaper link, GTK and Qt config, and fake hyprctl, omarchy
// and omarchy-shell commands that only log what they were asked. Nothing
// here reads or writes the real desktop.
namespace DesktopFixtures {
inline void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(bytes);
}

inline QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline void script(const QString &path, const QString &body)
{
    write(path, (QStringLiteral("#!/bin/sh\n") + body).toUtf8());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

inline const QByteArray userLooknfeel = "-- Change the default Omarchy look'n'feel.\n"
                                        "-- hl.config({\n"
                                        "--   general = {\n"
                                        "--     gaps_in = 0,\n"
                                        "--   },\n"
                                        "-- })\n"
                                        "hl.env(\"XCURSOR_THEME\", \"macOS\")\n";

inline const QByteArray shellJson = "{\n"
                                    "  \"version\": 1,\n"
                                    "  \"bar\": {\n"
                                    "    \"position\": \"top\",\n"
                                    "    \"transparent\": true,\n"
                                    "    \"centerAnchor\": \"omarchy.clock\",\n"
                                    "    \"layout\": {\n"
                                    "      \"left\": [\n"
                                    "        {\n"
                                    "          \"id\": \"omarchy.workspaces\"\n"
                                    "        }\n"
                                    "      ],\n"
                                    "      \"center\": [\n"
                                    "        {\n"
                                    "          \"id\": \"omarchy.clock\",\n"
                                    "          \"format\": \"h:mm AP\",\n"
                                    "          \"verticalFormat\": \"HH\\n\\u2014\\nmm\"\n"
                                    "        }\n"
                                    "      ],\n"
                                    "      \"right\": [\n"
                                    "        {\n"
                                    "          \"id\": \"omarchy.tray\"\n"
                                    "        },\n"
                                    "        {\n"
                                    "          \"id\": \"omarchy.power\"\n"
                                    "        }\n"
                                    "      ]\n"
                                    "    }\n"
                                    "  },\n"
                                    "  \"plugins\": []\n"
                                    "}\n";

inline const QByteArray colorsToml = "# Graphite.\nmode = \"dark\"\n\naccent = \"#0a84ff\"\nbackground = \"#1a1a1c\"\nforeground = \"#e5e5e7\"\n"
                                     "hyprland_active_border = \"rgba(0a84ffb3)\"\n";

struct Desktop {
    QTemporaryDir dir;
    QString home() const { return dir.path(); }
    QString config() const { return dir.path() + QStringLiteral("/.config"); }
    QString state() const { return dir.path() + QStringLiteral("/.local/state/omarchy/current"); }
    QString log() const { return dir.path() + QStringLiteral("/commands.log"); }
    QString theme() const { return config() + QStringLiteral("/omarchy/themes/graphite"); }
    QString wallpaper() const { return dir.path() + QStringLiteral("/Pictures/first.png"); }

    Desktop()
    {
        const QString omarchy = dir.path() + QStringLiteral("/omarchy");
        write(omarchy + QStringLiteral("/default/hypr/looknfeel.lua"),
              "hl.config({\n  general = {\n    gaps_in = 5,\n    gaps_out = 10,\n    border_size = 2,\n  },\n  decoration = {\n    rounding = 0,\n  },\n"
              "  group = {\n    groupbar = {\n      gaps_in = 5,\n      gaps_out = 0,\n    },\n  },\n})\n");
        write(omarchy + QStringLiteral("/config/omarchy/shell.json"), shellJson);
        write(config() + QStringLiteral("/hypr/hyprland.lua"), "require(\"hypr.looknfeel\")\n");
        write(config() + QStringLiteral("/hypr/looknfeel.lua"), userLooknfeel);
        write(config() + QStringLiteral("/omarchy/shell.json"), shellJson);
        write(config() + QStringLiteral("/omarchy/shell.toml"), "[font]\nbase-size = 12\n");
        write(theme() + QStringLiteral("/colors.toml"), colorsToml);
        write(state() + QStringLiteral("/theme/colors.toml"), colorsToml);
        write(state() + QStringLiteral("/theme/shell.toml"), "[bar]\nbackground = \"#1a1a1c\"\nbackground-alpha = 1.0\ntext = \"#e5e5e7\"\nsize-horizontal = 26\n");
        write(state() + QStringLiteral("/theme/hyprland.lua"),
              "local active_border_color = \"rgba(0a84ffb3)\"\nhl.config({\n  general = {\n    gaps_in = 4,\n    gaps_out = 8,\n    border_size = 1,\n"
              "    col = {\n      active_border = active_border_color,\n    },\n  },\n  decoration = {\n    rounding = 12,\n  },\n})\n");
        write(state() + QStringLiteral("/theme.name"), "graphite\n");
        write(wallpaper(), QByteArray("\x89PNG\r\n\x1a\n\0first", 14));
        QFile::link(wallpaper(), state() + QStringLiteral("/background"));
        write(config() + QStringLiteral("/gtk-4.0/gtk.css"), "@import url(\"../../theme.css\");\n\nwindow.nautilus-window {\n  font-family: \"SF Pro Text\";\n}\n");
        write(config() + QStringLiteral("/gtk-4.0/settings.ini"), "[Settings]\n");
        write(config() + QStringLiteral("/alacritty/alacritty.toml"), "[font]\nnormal = { family = \"JetBrainsMono Nerd Font\", style = \"Regular\" }\n");
        write(config() + QStringLiteral("/foot/foot.ini"), "[main]\nfont=JetBrainsMono Nerd Font:size=9\n");

        // Fakes: each logs its arguments, one line per call, and answers what the code asks.
        script(dir.path() + QStringLiteral("/bin/hyprctl"),
               QStringLiteral("printf 'hyprctl %s\\n' \"$*\" >> '%1'\n"
                              "case \"$2\" in\n"
                              "  'getoption general:gaps_in') echo '{\"option\": \"general:gaps_in\", \"css\": \"6 6 6 6\", \"set\": true}' ;;\n"
                              "  'getoption general:gaps_out') echo '{\"option\": \"general:gaps_out\", \"css\": \"14 14 14 14\", \"set\": true}' ;;\n"
                              "  'getoption general:border_size') echo '{\"option\": \"general:border_size\", \"int\": 3, \"set\": true}' ;;\n"
                              "  'getoption decoration:rounding') echo '{\"option\": \"decoration:rounding\", \"int\": 9, \"set\": true}' ;;\n"
                              "  'getoption general:col.active_border') echo '{\"option\": \"general:col.active_border\", \"gradient\": \"b3ff375f 0deg\", \"set\": true}' ;;\n"
                              "  *) echo '[]' ;;\n"
                              "esac\n")
                   .arg(log()));
        script(dir.path() + QStringLiteral("/bin/omarchy"), QStringLiteral("printf 'omarchy %s\\n' \"$*\" >> '%1'\n").arg(log()));
        script(dir.path() + QStringLiteral("/bin/omarchy-shell"), QStringLiteral("printf 'omarchy-shell %s\\n' \"$*\" >> '%1'\n").arg(log()));
        script(dir.path() + QStringLiteral("/bin/app"), QStringLiteral("printf 'app %s\\n' \"$*\" >> '%1'\n").arg(log()));
    }

    // HOME, the XDG folders and the fakes, for code that reads them now.
    void use() const
    {
        qputenv("HOME", home().toUtf8());
        qputenv("XDG_CONFIG_HOME", config().toUtf8());
        qputenv("XDG_DATA_HOME", (home() + QStringLiteral("/.local/share")).toUtf8());
        qputenv("XDG_DATA_DIRS", (home() + QStringLiteral("/system")).toUtf8());
        qputenv("OMARCHY_PATH", (home() + QStringLiteral("/omarchy")).toUtf8());
        qputenv("OMASTRATOR_HYPRCTL", (home() + QStringLiteral("/bin/hyprctl")).toUtf8());
        qputenv("OMASTRATOR_OMARCHY", (home() + QStringLiteral("/bin/omarchy")).toUtf8());
        qputenv("OMASTRATOR_OMARCHY_SHELL", (home() + QStringLiteral("/bin/omarchy-shell")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }

    QString logged() const { return QString::fromUtf8(readAll(log())); }
};
}
