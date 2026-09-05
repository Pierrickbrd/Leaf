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

#include <QObject>
#include <QPointer>
#include <QQmlNetworkAccessManagerFactory>
#include <QUrl>

class QQmlEngine;
class Settings;

class Covers : public QObject, public QQmlNetworkAccessManagerFactory
{
public:
    /// The form the application uses. The `Settings` singleton is resolved on first use rather
    /// than now, and that is the whole point of this constructor: the factory has to be
    /// installed **before** `engine.load(...)`, and before that load "Leaf" is not a resolvable
    /// module — see `Boot.h`, which is four paragraphs about this exact ordering. Installing it
    /// after the load would work today only because nothing on screen fetches anything until
    /// the event loop runs, which is a fact about today's QML and not a rule anybody could see.
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

    /// The settings in force, resolved on first use when this was built from an engine. Public
    /// because the network manager this hands out asks for them on every request rather than
    /// holding them: the keyring answers after the run has started, and the settings screen can
    /// change the address again afterwards.
    Settings *settings();

private:
    QPointer<QQmlEngine> m_engine;
    QPointer<Settings> m_settings;
};
