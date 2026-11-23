#pragma once

// Several things picked at once, so that one command can be given to all of them.
//
// What is picked is one kind of thing at a time: the series of a shelf, or the volumes of one
// series. A pick across the two (a series, then a volume) or across two series' volumes has
// no command that means the same to every member, so the older pick is dropped and the new
// one starts a selection of its own. A volume is picked with its edition (`seriesId`), which
// is also how this tells the two kinds apart.
//
// It holds ids and nothing else: the commands are `Commands`', so that a mark means the same
// whether it was asked for on one tile or on twenty.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

#include <memory>

class Selection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The picked ids, in the order they were picked. A property rather than a `has(id)`:
    /// a tile that asks a function is a tile nobody tells when the answer changes.
    Q_PROPERTY(QStringList ids READ ids NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    /// The edition the picked volumes belong to, empty when what is picked is editions.
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY changed)

    /// « 3 séries sélectionnées », and the three words of the bar. Here and not in the `.qml`,
    /// which writes no French.
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(QString readWord READ readWord CONSTANT)
    Q_PROPERTY(QString unreadWord READ unreadWord CONSTANT)
    Q_PROPERTY(QString clearWord READ clearWord CONSTANT)
    Q_PROPERTY(QString exportWord READ exportWord CONSTANT)

public:
    using QObject::QObject;

    /// Handed to the engine, which deletes it with itself; built as `Preferences::create` is.
    static Selection *create(QQmlEngine *, QJSEngine *)
    {
        return std::make_unique<Selection>().release();
    }

    QStringList ids() const { return m_ids; }
    int count() const { return static_cast<int>(m_ids.size()); }
    bool active() const { return !m_ids.isEmpty(); }
    QString seriesId() const { return m_seriesId; }
    QString label() const;
    QString readWord() const;
    QString unreadWord() const;
    QString clearWord() const;
    QString exportWord() const;

    /// Picks `id` if it was not, puts it back if it was. `seriesId` is empty for an edition.
    Q_INVOKABLE void toggle(const QString &id, const QString &seriesId = QString());
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    QStringList m_ids;
    QString m_seriesId;
};
