#pragma once

// Talking to the server.
//
// One place that knows the address, puts the key on every request, and turns what comes back
// into either a document or **a sentence a person can read**. That last part is the reason
// this is a class and not a helper: without it every screen invents its own wording for a
// refused key, and they disagree.

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUrlQuery>
#include <QObject>
#include <QString>

#include <functional>

class Settings;

class Server : public QObject
{
    Q_OBJECT

public:
    /// What came back. `trouble` is empty when it went, and is the only thing a screen
    /// should ever show — the status is here for the few callers that treat one specially.
    struct Answer {
        int status = 0;
        QJsonDocument body;
        QString trouble;

        bool went() const { return trouble.isEmpty(); }
    };

    explicit Server(Settings *settings, QObject *parent = nullptr);

    /// A path, and what goes after the `?` — separately, and never spliced by the caller.
    ///
    /// Measured, not feared: `"/search?q=" + "Haikyū !! & l'été"` leaves the `&` unencoded,
    /// so the server reads a search for `Haikyū !!` and an unrelated parameter called
    /// `l’été`. Half the query is silently gone and a search term can introduce parameters
    /// of its own. `QUrlQuery` percent-encodes it as UTF-8 and the round trip is exact.
    ///
    /// Which is why a `?` in `path` is refused rather than passed on: the one shape that
    /// cannot be encoded correctly is the one nobody should be able to reach for.
    void get(const QString &path, const QUrlQuery &query,
             std::function<void(const Answer &)> then);
    void get(const QString &path, std::function<void(const Answer &)> then);

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

    Settings *m_settings;
    QNetworkAccessManager m_network;
    /// Empty while it will still send. Set by a refusal, cleared when the key changes.
    QString m_stopped;
    QDateTime m_notBefore;
};
