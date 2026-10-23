#pragma once

// This device's link to its library, once there is one: the settings tab « Bibliothèque ».
//
// Three things a person does with it, and one the server does to it:
//
//   · **rename** — `PATCH /device`, and the name kept is the one the server answers with,
//     because that is the spelling every other device sees;
//   · **move** — the same library at another address. The key is tried there *before*
//     anything is written: a key is known only to its library, so the one proof that the
//     new address is the same library is that it accepts the key. The certificate is judged
//     first, and the key never reaches a server presenting another self-signed one;
//   · **unlink** — `DELETE /device`, and then `Settings::forget` whatever the server said:
//     the confirmation promised this device would forget, and a server that does not answer
//     is told about by name in that same confirmation;
//   · **revoked** — a `403 unknown key` to any request, from any screen: removed on the
//     server with `device revoke`. Forgotten here too, once, and the link screen opens with
//     the sentence that says why. Not when the environment set the key: the environment is
//     never overwritten, so it is told rather than erased.
//
// And one thing it does for another device: **relay** — `POST /link/codes`, and the link the
// server answers with, shown as it is. It used to be composed here, from the address and the
// pin this device uses — which on the server's own machine is the loopback, and through an
// SSH tunnel the tunnel's end: a person there was handed a link no other device could follow,
// with a sentence about 127.0.0.1, tunnels and sudo under it. The server knows where other
// devices reach it; this device only knows where it does. The code is asked about every two
// seconds until it is used, and withdrawn if the panel closes first: a code nobody is looking
// at any more is a key waiting for whoever finds it. Never logged, for the same reason.
//
// Its own network manager for a move, and not `Server`'s, for the reason `Linking` gives:
// trying an address this device is not linked to yet must not change the pin every request
// on screen is held to.

#include <QDeadlineTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

namespace Api {
struct IssuedCode;
}
class Linking;
class QNetworkReply;
class Server;
class Settings;

class Bond : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What went wrong, or what is happening — a refusal, a sentence from `Settings`. Empty
    /// when there is nothing to say.
    Q_PROPERTY(QString sentence READ sentence NOTIFY changed)
    /// A rename, a move or an unlink is on its way; the others wait for it.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// Where the library is, as this device reaches it.
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    /// « 22 octobre 2026 », or empty when this device was configured by hand.
    Q_PROPERTY(QString linkedOn READ linkedOn NOTIFY changed)

    // The tab's own words, held by the object the tab is named after.
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString deviceLabel READ deviceLabel CONSTANT)
    Q_PROPERTY(QString linkedLabel READ linkedLabel CONSTANT)
    Q_PROPERTY(QString renameLabel READ renameLabel CONSTANT)
    Q_PROPERTY(QString moveLabel READ moveLabel CONSTANT)
    Q_PROPERTY(QString movePlaceholder READ movePlaceholder CONSTANT)
    Q_PROPERTY(QString moveAction READ moveAction CONSTANT)
    Q_PROPERTY(QString unlinkLabel READ unlinkLabel CONSTANT)
    Q_PROPERTY(QString unlinkQuestion READ unlinkQuestion CONSTANT)
    Q_PROPERTY(QString unlinkWhat READ unlinkWhat NOTIFY changed)
    Q_PROPERTY(QString unlinkComeBack READ unlinkComeBack CONSTANT)
    Q_PROPERTY(QString unlinkIfSilent READ unlinkIfSilent NOTIFY changed)
    Q_PROPERTY(QString unlinkGo READ unlinkGo CONSTANT)
    Q_PROPERTY(QString cancelLabel READ cancelLabel CONSTANT)

    /// The link another device opens to link itself, while its code is good; empty before
    /// it is made, once it is used, once it has run out, and once the panel is closed.
    Q_PROPERTY(QString relayLink READ relayLink NOTIFY relayChanged)
    /// How long the code has left, in whole seconds; 0 when there is none.
    Q_PROPERTY(int relaySeconds READ relaySeconds NOTIFY relayChanged)
    /// The name of the device that used the code; empty until one has.
    Q_PROPERTY(QString relayed READ relayed NOTIFY relayChanged)
    /// What stopped the relay — a refusal, a code run out. Empty when there is nothing to say.
    Q_PROPERTY(QString relaySentence READ relaySentence NOTIFY relayChanged)
    /// Under a link to one of the server's own network addresses, which works from that
    /// network only; empty for a public address, and when there is no link.
    Q_PROPERTY(QString relaySameNetwork READ relaySameNetwork NOTIFY relayChanged)
    /// « Valable encore 9:42 · une seule fois », and « PC du salon relié ».
    Q_PROPERTY(QString relayTimeLeft READ relayTimeLeft NOTIFY relayChanged)
    Q_PROPERTY(QString relayedSaid READ relayedSaid NOTIFY relayChanged)
    Q_PROPERTY(QString relayLabel READ relayLabel CONSTANT)
    Q_PROPERTY(QString relayIntro READ relayIntro CONSTANT)
    Q_PROPERTY(QString copyLabel READ copyLabel CONSTANT)
    Q_PROPERTY(QString copiedLabel READ copiedLabel CONSTANT)
    Q_PROPERTY(QString anotherLinkLabel READ anotherLinkLabel CONSTANT)
    Q_PROPERTY(QString closeLabel READ closeLabel CONSTANT)

public:
    /// `linking` is told when the link is undone, so the screen it drives opens empty rather
    /// than on the link this device was made with. Null in a test that does not look at it.
    Bond(Settings *settings, Server *server, Linking *linking, QObject *parent = nullptr);

    /// Built by the engine from the `Settings`, `Server` and `Linking` singletons.
    static Bond *create(QQmlEngine *engine, QJSEngine *);

    QString sentence() const { return m_sentence; }
    bool busy() const { return m_busy; }
    QString address() const;
    QString deviceName() const;
    QString linkedOn() const;

    QString title() const;
    QString deviceLabel() const;
    QString linkedLabel() const;
    QString renameLabel() const;
    QString moveLabel() const;
    QString movePlaceholder() const;
    QString moveAction() const;
    QString unlinkLabel() const;
    QString unlinkQuestion() const;
    QString unlinkWhat() const;
    QString unlinkComeBack() const;
    QString unlinkIfSilent() const;
    QString unlinkGo() const;
    QString cancelLabel() const;

    QString relayLink() const { return m_relayLink; }
    int relaySeconds() const { return m_relaySeconds; }
    QString relayed() const { return m_relayed; }
    QString relaySentence() const { return m_relaySentence; }
    QString relaySameNetwork() const;
    QString relayTimeLeft() const;
    QString relayedSaid() const;
    QString relayLabel() const;
    QString relayIntro() const;
    QString copyLabel() const;
    QString copiedLabel() const;
    QString anotherLinkLabel() const;
    QString closeLabel() const;

    /// Asks the server for the name, and keeps the one it answers with.
    Q_INVOKABLE void rename(const QString &name);
    /// Tries the key at `address` and writes it only if it is accepted there.
    Q_INVOKABLE void move(const QString &address);
    /// Takes the key off the server, then forgets the library here — whatever the server said.
    Q_INVOKABLE void unlink();
    /// Lets go of the sentence, when the tab is left: it was about what was done there.
    Q_INVOKABLE void clearSentence();
    /// Asks the server for a code, and shows the link to it.
    Q_INVOKABLE void relay();
    /// The panel is closed: a code still waiting is withdrawn, and everything about it goes.
    Q_INVOKABLE void stopRelay();
    /// Puts the link on the clipboard, for a device the camera cannot reach.
    Q_INVOKABLE void copyRelay() const;

signals:
    void changed();
    /// The library is forgotten: the link screen is what shows now.
    void unlinked();
    /// The library answers at another address now: whatever was asked of the old one is
    /// worth asking again.
    void moved();
    void relayChanged();
    /// The server refused a code because nobody else reaches it: what the tab offers was
    /// decided on an older answer, and is worth asking again.
    void nobodyElseReaches();

private:
    /// A `403 unknown key`, from any request `Server` made.
    void revoked();
    void tried(QNetworkReply *reply, const QString &address);
    /// Lets go of the address being tried, without a word.
    void drop();
    /// Forgets the library, and opens the link screen saying `why`. False when `Settings`
    /// refused, and then `trouble` has said why.
    bool forget(const QString &why);
    void say(const QString &sentence);
    void setBusy(bool busy);
    /// The server's answer to `POST /link/codes`.
    void issued(const Api::IssuedCode &code);
    /// Asks whether the code has been used, unless the last question is still out.
    void follow();
    /// Counts the second down, and lets the link go when there are none left.
    void tick();
    /// Takes the code back on the server. Its answer changes nothing here: the link is
    /// already gone from the screen, and a code that could not be withdrawn still dies in
    /// ten minutes.
    void withdraw(const QString &id);
    /// Lets go of the code and its link here, saying `why`; nothing is sent.
    void endRelay(const QString &why);

    Settings *m_settings;
    QPointer<Server> m_server;
    QPointer<Linking> m_linking;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_trying;
    /// The pin the address being tried is held to: the one this device was linked to.
    QString m_pin;
    /// The pin to keep once the address is accepted — the same one, or empty when an
    /// authority vouched for the certificate presented there instead.
    QString m_newPin;
    QString m_sentence;
    bool m_busy = false;
    /// Said once, not at every refused request: an environment-set key refused by the server
    /// stays refused for the run.
    bool m_toldTheEnvironment = false;

    /// The code being relayed, as the server names it to follow or withdraw it — not the
    /// code itself, which lives only inside `m_relayLink`.
    QString m_relayId;
    QString m_relayLink;
    /// The link names one of the server's network addresses rather than a public one.
    bool m_relayLocal = false;
    QString m_relayed;
    QString m_relaySentence;
    int m_relaySeconds = 0;
    /// When the code runs out, on this machine's own steady clock.
    QDeadlineTimer m_relayDeadline;
    QTimer m_relayClock;
    QTimer m_relayFollow;
    /// `POST /link/codes` is out, and its answer not back.
    bool m_relayAsking = false;
    /// The panel closed while it was out: the code it brings back is withdrawn unseen.
    bool m_relayWithdrawn = false;
    /// A `GET /link/codes/{id}` is out: the next tick does not ask a second time.
    bool m_relayFollowing = false;
};
