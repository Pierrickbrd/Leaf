#pragma once

// One series, as its own page draws it.
//
// The shelf hands a grid many series and words each in a line; this holds **one** and words
// it in an entire screen. Same arrangement as `Resume` and `Shelf`: a `Server *` through the
// constructor so a test builds its own, a singleton only in the application, and every French
// string comes out of `Words` — no `.qml` file spells one.
//
// **It points at an identifier rather than being constructed with one.** There is one series
// page at a time, and changing edition replaces the object the page is about while the page
// stays: « le sélecteur ne remplace pas un libellé, il remplace l'objet dont la page parle ».
// A model rebuilt for each would take the screen down with it.
//
// **Nothing here empties before it refills.** `point()` keeps what it holds until the answer
// arrives, so a screen showing Elfes goes on showing Elfes while the Intégrale is fetched. The
// one case with nothing to keep — the first opening from the shelf — is the only one a
// skeleton is drawn for.

#include "Api.h"
#include "Description.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class Series : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString identifier READ identifier NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)

    /// The three levels the model stacks, kept apart because a page has the room the shelf's
    /// one-line name did not: the universe above, the work large, the edition named plainly.
    Q_PROPERTY(QString universe READ universe NOTIFY changed)
    Q_PROPERTY(QString universeId READ universeId NOTIFY changed)
    Q_PROPERTY(QString work READ work NOTIFY changed)
    Q_PROPERTY(QString edition READ edition NOTIFY changed)
    Q_PROPERTY(QString cover READ cover NOTIFY changed)

    /// Who made it, what it weighs, its genres, its summary and the four columns of the
    /// description: everything that words the answer and asks nothing (`Description.h`).
    Q_PROPERTY(Description *description READ description CONSTANT)

    /// The other editions of the same work, this one among them. Below two the switcher is
    /// not drawn at all: there is nothing to choose between.
    Q_PROPERTY(QVariantList editions READ editions NOTIFY changed)
    Q_PROPERTY(QString editionsLabel READ editionsLabel NOTIFY changed)
    /// The single file of a book that is a whole book, and empty for everything else.
    Q_PROPERTY(QString oneShotEntry READ oneShotEntry NOTIFY changed)
    /// The gaps in the collection, for the list to weave in as rows of its own. They belong
    /// to the series and not to its files — a hole has no file behind it — so they travel
    /// from here to the list rather than being asked for twice.
    Q_PROPERTY(QVariantList missingVolumes READ missingVolumes NOTIFY changed)
    /// How many arcs the edition declares, which is the guard on asking for them at all.
    Q_PROPERTY(int arcCount READ arcCount NOTIFY changed)
    /// How many volumes this library holds of the edition, for what a reimport says it aims at.
    Q_PROPERTY(int volumeCount READ volumeCount NOTIFY changed)
    /// What is held is the answer for the series pointed at. Pointed at another, the old
    /// answer is kept under the eye until the new one arrives, and what it says — the gaps,
    /// the arcs, the universe — belongs to the one that was left.
    Q_PROPERTY(bool settled READ settled NOTIFY changed)

public:
    explicit Series(Server *server, QObject *parent = nullptr);

    static Series *create(QQmlEngine *engine, QJSEngine *);

    QString identifier() const { return m_id; }
    bool available() const { return m_one.has_value(); }
    bool settled() const { return m_one.has_value() && m_one->id == m_id; }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }

    QString universe() const;
    QString universeId() const;
    QString work() const;
    QString edition() const;
    QString cover() const;
    Description *description() const { return m_description; }
    QVariantList editions() const;
    QString editionsLabel() const;
    QString oneShotEntry() const;
    QVariantList missingVolumes() const;
    int arcCount() const;
    int volumeCount() const;

    /// Points the page at a series. Asking for the one already held asks again — that is what
    /// a retry is — but it keeps what is on screen either way.
    Q_INVOKABLE void point(const QString &identifier);
    Q_INVOKABLE void reload();
    /// The series as the server now has it, after something was done to it: the header's
    /// counts move, and nothing is asked. A mark used to ask for the page again, and for its
    /// siblings after it — two answers for « 4 lus » becoming « 5 lus ». Ignored for any
    /// other series, except in the list of editions, where it is put in its place.
    void take(const Api::Series &fresh);
    /// Lets go of the series it was showing, for a reader who has left the page.
    ///
    /// **Keeping is for a page being replaced, not for a page one has left.** Nothing empties
    /// to refill while the reader is on it — changing edition, opening another series of the
    /// universe — because what is there is what they are looking at. Once they are back on
    /// the shelf there is nothing to keep, and holding on meant the next series opened on the
    /// *last* one's cover and title for as long as the answer took. Ten seconds and a screen
    /// in between did not help: it was still there, and it was still wrong.
    Q_INVOKABLE void forget();

signals:
    void changed();

private:
    /// It words the answer held here, and reads it here rather than keeping a copy.
    friend class Description;

    void took(const Server::Answer &answer);
    void tookSiblings(const Server::Answer &answer);

    Server *m_server;
    QString m_id;
    std::optional<Api::Series> m_one;
    QList<Api::Series> m_siblings;
    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
    /// A child, so it goes with the page; made in the constructor, never null.
    Description *m_description;
};
