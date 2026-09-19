#pragma once

// The key, on the requests QML makes for itself.
//
// A cover is an `Image` in a delegate, and an `Image` is fetched by the engine's own network
// manager — which has never heard of `Server` and would send no key. The server answers 403
// and the grid draws a wall of broken images.
//
// **A factory rather than a `QQuickImageProvider`**, which was the other way to put the key
// on. An `Image` that stays an `Image` keeps Qt's cache, its asynchronous decode and — the
// reason this settled it — its cancellation: §01 asks that a cover leaving the screen be
// abandoned, and a delegate destroyed outside the view's cache buffer destroys its `Image`,
// which takes the reply with it. Behind a provider every one of those is code to write.
//
// It is a `QObject` only so that something can own it: `QQmlEngine::setNetworkAccessManager
// Factory` takes no ownership and the factory has to outlive the engine, so it is parented to
// the application, which does.

#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QQmlNetworkAccessManagerFactory>
#include <QUrl>

class QQmlEngine;
class Settings;

class Covers : public QObject, public QQmlNetworkAccessManagerFactory
{
    Q_OBJECT

public:
    /// The form the application uses. The `Settings` singleton cannot be resolved now, and
    /// that is the whole point of this constructor: the factory has to be installed
    /// **before** `engine.load(...)`, and before that load "Leaf" is not a resolvable module
    /// — see `Boot.h`, which is four paragraphs about this exact ordering. Installing it
    /// after the load would work today only because nothing on screen fetches anything until
    /// the event loop runs, which is a fact about today's QML and not a rule anybody could see.
    ///
    /// So it posts `resolve` to itself instead. That call lands on this object's own thread
    /// once the loop turns, which is before the first image can be asked for and long before
    /// anything reaches `carry`.
    explicit Covers(QQmlEngine *engine, QObject *parent = nullptr);

    /// The form a test uses, and the one that says what this class actually needs.
    explicit Covers(Settings *settings, QObject *parent = nullptr);

    QNetworkAccessManager *create(QObject *parent) override;

    /// Whether two addresses name the same server — scheme, host and port, with the port each
    /// scheme implies filled in.
    ///
    /// Public because it is the whole of the decision worth testing: it is what keeps the key
    /// from riding to wherever else the engine happens to fetch from. A test that can only
    /// reach it through a live socket checks one pair; this one walks them.
    static bool sameServer(const QUrl &one, const QUrl &other);

    /// The settings in force, once they have been resolved — never resolving them itself.
    Settings *settings() const;

    /// Finds the `Settings` singleton in the engine and starts following it.
    ///
    /// **On this object's own thread, always.** It used to happen on first use, and first
    /// use is `createRequest` on whichever thread Qt loads an image on — so it reached into
    /// a QML engine from a worker thread and wrote a member nobody was guarding. That is a
    /// heap corrupted at random: the run died in a different test each time, none of them
    /// the one at fault, and never on the machine it was written on.
    void resolve();

    /// Whether this URL is our server and, if so, the key to carry to it.
    ///
    /// **Callable from any thread**, which is the whole reason it exists:
    /// `QQmlNetworkAccessManagerFactory` says the managers it hands out are used wherever
    /// Qt pleases. The address and the key are a copy taken on this object's thread, so
    /// nothing here reads a `QString` while another thread assigns it.
    bool carry(const QUrl &url, QByteArray &key) const;

private:
    /// Starts following whatever `Settings` was found, and takes a first copy.
    void follow();
    /// Copies the address and the key out of `Settings`, under the lock. On this thread.
    void take();

    QPointer<QQmlEngine> m_engine;
    QPointer<Settings> m_settings;
    mutable QMutex m_lock;
    QString m_address;
    QString m_key;
};
