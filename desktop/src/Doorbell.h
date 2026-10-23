#pragma once

// The one door a second launch has to the first.
//
// Clicking a `leaf://` link starts `leaf-desktop leaf://…` whether or not Leaf is already
// open, and two windows on one library is not what anybody clicking wants. So a launch that
// carries a link first rings here: if a Leaf is listening, it is handed the link and the new
// process leaves; if nobody answers, it is the first, and it listens itself.
//
// A local socket named after the user, so that two people on one machine each have their
// own Leaf and neither is handed the other's link. What comes through it is untrusted text
// like anything else a link is made of: it goes to `Link::read` on the other side and
// nowhere before.

#include <QLocalServer>
#include <QObject>
#include <QString>

class Doorbell : public QObject
{
    Q_OBJECT

public:
    /// `name` is the socket's name; the default is this user's, and only a test says another.
    explicit Doorbell(const QString &name = defaultName(), QObject *parent = nullptr);

    /// An absolute path in the runtime folder (`$XDG_RUNTIME_DIR`, which is this user's alone
    /// and mode 0700). A bare name would put the socket in `/tmp`, shared and predictable:
    /// another user could make it first and be handed the clicked link. The temporary folder
    /// only when there is no runtime folder, with the peer checks as what is left.
    static QString defaultName();

    /// Whether the process at the other end of a connected local socket runs as `user`. Both
    /// sides ask it: a socket in a shared folder can be made by anybody, and the link a click
    /// hands over carries a one-time code that only this user's own Leaf may be given.
    static bool peerIs(qintptr descriptor, unsigned user);

    /// Hands `link` to the Leaf listening on `name`, and returns true once it has said it
    /// has it. False — and nothing sent that anyone kept — when nobody answers in time.
    static bool ring(const QString &link, const QString &name = defaultName());

    /// Starts listening. A name left behind by a process that died is taken over: a socket
    /// file outlives the crash that left it, and « address in use » would then keep every
    /// later launch from being the first one.
    bool listen();

signals:
    void rang(const QString &link);

private:
    void accept();

    QString m_name;
    QLocalServer m_server;
};
