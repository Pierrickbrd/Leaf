#pragma once

// Talking to the server.
//
// One place that knows the address, puts the key on every request, and turns what comes back
// into either a document or **a sentence a person can read**. That last part is the reason
// this is a class and not a helper: without it every screen invents its own wording for a
// refused key, and they disagree.

#include <QDateTime>
#include <QJsonDocument>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQmlEngine>
#include <QUrlQuery>
#include <QObject>
#include <QString>

#include <functional>
#include <type_traits>

class Settings;

class Server : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    /// The header the key rides on. Spelled once because two different network managers write
    /// it now: this one, and the engine's own for the covers an `Image` fetches.
    static constexpr const char *KeyHeader = "X-Leaf-Key";

    /// What came back. `trouble` is empty when it went, and is the only thing a screen
    /// should ever show — the status is here for the few callers that treat one specially.
    struct Answer {
        int status = 0;
        QJsonDocument body;
        QString trouble;

        bool went() const { return trouble.isEmpty(); }
    };

    explicit Server(Settings *settings, QObject *parent = nullptr);

    /// The one instance the application talks through, built by the engine from the `Settings`
    /// singleton.
    ///
    /// One, and not one per screen: `stopped()` is the whole reason. A refused key stops this
    /// client for good, and three screens each holding their own `Server` would each have to
    /// be refused separately — ten times over, which is what the server counts before it shuts
    /// the address out for a quarter of an hour.
    static Server *create(QQmlEngine *engine, QJSEngine *);

    /// Where the server is, tidied — for whoever has to build a URL this class will not fetch
    /// itself. A cover is one: it is drawn by an `Image`, not asked for here.
    QString address() const;

    /// A path, and what goes after the `?` — separately, and never spliced by the caller.
    ///
    /// Measured, not feared: `"/search?q=" + "Haikyū !! & l'été"` leaves the `&` unencoded,
    /// so the server reads a search for `Haikyū !!` and an unrelated parameter called
    /// `l’été`. Half the query is silently gone and a search term can introduce parameters
    /// of its own. `QUrlQuery` percent-encodes it as UTF-8 and the round trip is exact.
    ///
    /// Which is why a `?` in `path` is refused rather than passed on: the one shape that
    /// cannot be encoded correctly is the one nobody should be able to reach for.
    ///
    /// `caller` is whoever the answer belongs to, and its life is what the answer waits on: a
    /// screen destroyed while its request is out is not told anything, because there is
    /// nobody left to tell. Nothing about it is advisory — `then` almost always captures a
    /// `this`, and calling it afterwards reads freed memory. A null `caller` binds the answer
    /// to this client instead, which is as long as an answer can possibly be held.
    void get(const QString &path, const QUrlQuery &query, const QObject *caller,
             std::function<void(const Answer &)> then);
    void get(const QString &path, const QObject *caller,
             std::function<void(const Answer &)> then);

    /// Whether anything more will be sent.
    ///
    /// A refused key is not a hiccup: it stays refused until somebody changes a file, and the
    /// server counts wrong keys — ten in five minutes and the address is shut out for a
    /// quarter of an hour. A client that retried a 403 would lock itself out, and with it
    /// everything else answering from that address.
    ///
    /// So one refusal stops the client for good, and a 429 stops it until Retry-After. The
    /// distinction is the whole reason there is no generic "something went wrong, try again".
    bool stopped() const { return !m_stopped.isEmpty(); }
    QString whyStopped() const { return m_stopped; }

    /// The address with its edges filed off: a scheme if none was given, no trailing slash.
    /// Typing `leaf.local:8081` into a box is not a mistake anybody should be corrected for.
    static QString tidy(const QString &address);

private:
    Answer read(class QNetworkReply *reply) const;

    /// A request made before `Settings` had finished loading, kept until it can be sent.
    ///
    /// A keyring is a service: for the first moments of a run there is no address, and no
    /// answer worth giving either. Held here rather than refused, because a refusal is
    /// final — a screen told "Leaf is still looking" had nothing to tell it the looking had
    /// ended half a second later, and stayed on that sentence for the rest of the session.
    ///
    /// It is emptied on the next `Settings::changed`, which `Settings` emits however the
    /// keyring went — refused included — so nothing is held for ever.
    ///
    /// Bounded, and watched. A request held here is one nobody has answered yet, so the two
    /// things a queue can do wrong are both open: a caller that retries on a timer while
    /// `loaded()` is false appends every time and nothing ever leaves, and a caller destroyed
    /// while its request waits is a `std::function` holding a `this` that the drain would
    /// call. Before this branch neither could happen, because an unloaded client answered on
    /// the spot and no request outlived the call that made it.
    struct Waiting {
        QString path;
        /// Encoded, and named so — not a `QUrlQuery`, and not a query anybody can read.
        ///
        /// `QUrlQuery` declares a copy constructor, a copy assignment and a destructor, and
        /// no move constructor — so moving one calls the copy, which Qt does not mark
        /// `noexcept`. Held by value in a `QList` that grows, that is a relocation which may
        /// throw while it is carrying up to thirty-two `std::function`s, each holding a
        /// screen's `this`. The round trip costs nothing: measured on the very string this
        /// file was written for, `q=Haiky%C5%AB%20!!%20%26%20l'%C3%A9t%C3%A9` parses back
        /// equal to the query it came from, ampersand included.
        QString encodedQuery;
        QPointer<const QObject> caller;
        std::function<void(const Answer &)> then;
    };

    /// Checked, not assumed. Every member above promises a `noexcept` move, so `QList`
    /// relocates the queue rather than copying it. A member added later that does not
    /// promise it fails this line, instead of failing the next time the queue grows.
    static_assert(std::is_nothrow_move_constructible_v<Waiting>,
                  "a held request has to move without throwing: QList relocates them");

    Settings *m_settings;
    QNetworkAccessManager m_network;
    /// Empty while it will still send. Set by a refusal, cleared when the key changes.
    QString m_stopped;
    QDateTime m_notBefore;
    QList<Waiting> m_waiting;
};
