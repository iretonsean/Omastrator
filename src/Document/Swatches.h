#pragma once
#include <QColor>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <vector>

// Named colours in groups, as Illustrator's Swatches panel keeps them. The
// library belongs to the install, not a document, and is kept in QSettings.
struct Swatch {
    Swatch() = default;
    Swatch(QString name, QColor color, bool global = false, QString id = {})
        : name(std::move(name)), color(std::move(color)), global(global), id(std::move(id))
    {
    }
    QString name;
    QColor color;
    // Global swatches are linked: paints keep `id`, and editing the colour updates every use.
    bool global = false;
    QString id;
    friend bool operator==(const Swatch &, const Swatch &) = default;
};

struct SwatchGroup {
    QString name;
    std::vector<Swatch> swatches;
};

class Swatches : public QObject {
    Q_OBJECT
public:
    // Where picked colours go.
    static inline const QString defaultGroup = QStringLiteral("Swatches");
    // An empty key keeps the library in memory only, for tests.
    explicit Swatches(QString settingsKey = QStringLiteral("swatches"), QObject *parent = nullptr);

    const std::vector<SwatchGroup> &groups() const { return m_groups; }
    // Adds to `group`, creating it at the end; `replace` empties it first. A colour
    // already in the group is not added twice. Returns how many were added.
    int add(const QString &group, const std::vector<Swatch> &swatches, bool replace = false);
    void remove(const QString &group, int index);
    void removeGroup(const QString &group);
    // Edits one swatch's colour; a global one says so, so every use follows.
    void setColor(const QString &group, int index, const QColor &color);
    void setGlobal(const QString &group, int index, bool global);

    QJsonArray toJson() const;
    static std::vector<SwatchGroup> fromJson(const QJsonArray &json);
    // "bright_red" → "Bright Red".
    static QString nameFromKey(const QString &key);

signals:
    void changed();
    void globalSwatchRecolored(const QString &id, const QColor &color);

private:
    Swatch *at(const QString &group, int index);
    void save() const;
    const QString m_key;
    std::vector<SwatchGroup> m_groups;
};
