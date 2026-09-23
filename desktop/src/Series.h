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
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

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

    /// The two lines of the header: who made it, and what it weighs.
    Q_PROPERTY(QString makers READ makers NOTIFY changed)
    Q_PROPERTY(QString weights READ weights NOTIFY changed)
    Q_PROPERTY(QStringList genres READ genres NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

    /// The description, in the two columns the artifact asks for: who made it and who
    /// publishes it on the left, what it is on the right. Lists of `{label, value}` rather
    /// than a getter apiece — a fifteenth fact then costs nothing, and a line whose value is
    /// missing is simply not in the list.
    Q_PROPERTY(QVariantList credits READ credits NOTIFY changed)
    Q_PROPERTY(QVariantList nature READ nature NOTIFY changed)
    /// What *this library* holds of the edition, which is not what the edition is — the one
    /// block that speaks about you, and the only place that says a volume is missing.
    Q_PROPERTY(QVariantList holding READ holding NOTIFY changed)

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

public:
    explicit Series(Server *server, QObject *parent = nullptr);

    static Series *create(QQmlEngine *engine, QJSEngine *);

    QString identifier() const { return m_id; }
    bool available() const { return m_one.has_value(); }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }

    QString universe() const;
    QString universeId() const;
    QString work() const;
    QString edition() const;
    QString cover() const;
    QString makers() const;
    QString weights() const;
    QStringList genres() const;
    QString summary() const;
    QVariantList credits() const;
    QVariantList nature() const;
    QVariantList holding() const;
    QVariantList editions() const;
    QString editionsLabel() const;
    QString oneShotEntry() const;
    QVariantList missingVolumes() const;

    /// Points the page at a series. Asking for the one already held asks again — that is what
    /// a retry is — but it keeps what is on screen either way.
    Q_INVOKABLE void point(const QString &identifier);
    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    void took(const Server::Answer &answer);
    void tookSiblings(const Server::Answer &answer);

    Server *m_server;
    QString m_id;
    std::optional<Api::Series> m_one;
    QList<Api::Series> m_siblings;
    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
};
