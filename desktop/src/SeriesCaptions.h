#pragma once

// What a series page says, as opposed to what it shows.
//
// The same arrangement as `Captions` and for the same reason: `Words` holds the French and
// the typography, `Series`, `Entries` and `Elsewhere` hold the state, and this binds the two
// so a caption is refreshed by the change that refreshed what it quotes. Kept off both models, because a
// page's words are not a list of volumes and a class carrying both is a class where neither
// is easy to find.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QJSEngine;
class QQmlEngine;
class Entries;
class Elsewhere;

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
    /// What each of the two view buttons does, which is not what they act on.
    Q_PROPERTY(QString asList READ asList CONSTANT)
    Q_PROPERTY(QString asGrid READ asGrid CONSTANT)
    /// Said under a list a search emptied — which is not a series with no files, and the two
    /// have to be told apart or the screen cannot say why there is nothing.
    Q_PROPERTY(QString nothingFound READ nothingFound NOTIFY changed)
    Q_PROPERTY(bool narrowedToNothing READ narrowedToNothing NOTIFY changed)

    /// The two headings of the last tab, and the third the one on the right grows when a way
    /// through the universe is walked.
    Q_PROPERTY(QString sameWorkHeading READ sameWorkHeading CONSTANT)
    Q_PROPERTY(QString universeHeading READ universeHeading CONSTANT)
    Q_PROPERTY(QString outsideHeading READ outsideHeading CONSTANT)
    /// « Terres d'Arran · 3 autres », and « 2 séries » under the heading of what a walk leaves
    /// out. Counted here rather than in the `.qml`, which would then be writing French.
    Q_PROPERTY(QString universeLine READ universeLine NOTIFY changed)
    Q_PROPERTY(QString outsideLine READ outsideLine NOTIFY changed)

public:
    explicit SeriesCaptions(Entries *entries, Elsewhere *elsewhere,
                            QObject *parent = nullptr);

    static SeriesCaptions *create(QQmlEngine *engine, QJSEngine *);

    QString volumesTab() const;
    QString descriptionTab() const;
    QString elsewhereTab() const;
    QString neverRead() const;
    QString inThisLibrary() const;
    QString volumesAxis() const;
    QString asList() const;
    QString asGrid() const;
    QString nothingFound() const;
    bool narrowedToNothing() const;
    QString sameWorkHeading() const;
    QString universeHeading() const;
    QString outsideHeading() const;
    QString universeLine() const;
    QString outsideLine() const;
    /// « 29 albums · ici » — a tile's grey line with the mark that it is the one being read.
    /// A function and not a property: it is said of a tile, and there are as many as the
    /// block draws.
    Q_INVOKABLE QString hereToo(const QString &detail) const;

signals:
    void changed();

private:
    Entries *m_entries;
    Elsewhere *m_elsewhere;
};
