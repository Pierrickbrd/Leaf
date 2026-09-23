#pragma once

// What a series page says, as opposed to what it shows.
//
// The same arrangement as `Captions` and for the same reason: `Words` holds the French and
// the typography, `Series` and `Entries` hold the state, and this binds the two so a caption
// is refreshed by the change that refreshed what it quotes. Kept off both models, because a
// page's words are not a list of volumes and a class carrying both is a class where neither
// is easy to find.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QJSEngine;
class QQmlEngine;
class Entries;

class SeriesCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The three tabs, in the order they are drawn.
    Q_PROPERTY(QString volumesTab READ volumesTab CONSTANT)
    Q_PROPERTY(QString descriptionTab READ descriptionTab CONSTANT)
    Q_PROPERTY(QString elsewhereTab READ elsewhereTab CONSTANT)

    /// « Non lu », written on a line where the two other states carry a ring.
    Q_PROPERTY(QString neverRead READ neverRead CONSTANT)
    /// « Dans cette bibliothèque » — over the only block that speaks about you.
    Q_PROPERTY(QString inThisLibrary READ inThisLibrary CONSTANT)
    /// The axis the field narrows, which `LeafSearchLine` turns into « Chercher dans … ».
    Q_PROPERTY(QString volumesAxis READ volumesAxis CONSTANT)
    /// Said under a list a search emptied — which is not a series with no files, and the two
    /// have to be told apart or the screen cannot say why there is nothing.
    Q_PROPERTY(QString nothingFound READ nothingFound NOTIFY changed)
    Q_PROPERTY(bool narrowedToNothing READ narrowedToNothing NOTIFY changed)

public:
    explicit SeriesCaptions(Entries *entries, QObject *parent = nullptr);

    static SeriesCaptions *create(QQmlEngine *engine, QJSEngine *);

    QString volumesTab() const;
    QString descriptionTab() const;
    QString elsewhereTab() const;
    QString neverRead() const;
    QString inThisLibrary() const;
    QString volumesAxis() const;
    QString nothingFound() const;
    bool narrowedToNothing() const;

signals:
    void changed();

private:
    Entries *m_entries;
};
