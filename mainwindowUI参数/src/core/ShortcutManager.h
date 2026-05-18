#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QSettings>

class ShortcutManager : public QObject {
    Q_OBJECT
public:
    struct ShortcutInfo {
        QString id;
        QString description;
        QKeySequence defaultKey;
        QString category;
    };

    static ShortcutManager& instance();

    QKeySequence getShortcut(const QString& id) const;
    void setShortcut(const QString& id, const QKeySequence& key);
    
    QList<ShortcutInfo> getAllShortcuts() const { return m_shortcuts.values(); }
    QList<ShortcutInfo> getShortcutsByCategory(const QString& category) const;

    void save();
    void load();
    void resetToDefaults();

signals:
    void shortcutsChanged();

private:
    ShortcutManager(QObject* parent = nullptr);
    void initDefaults();

    QMap<QString, ShortcutInfo> m_shortcuts;
    QMap<QString, QKeySequence> m_customKeys;
};

#endif // SHORTCUTMANAGER_H
