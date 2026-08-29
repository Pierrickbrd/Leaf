#pragma once

// Where the server is, and how to prove who you are.
//
// **Read, never written.** The application is for reading comics; where its server lives is a
// deployment question, and the server itself is configured the same way — environment
// variables, no administration screen. A client that grew a connection console would be
// managing something that is not its subject.
//
// Three places, in this order:
//
//   1. `LEAF_ADDRESS` and `LEAF_KEY` in the environment. What a launcher, a script or a
//      `systemd` unit sets, and what wins for one run.
//   2. The session keyring — GNOME's or KDE's. Put there by hand, by somebody who would
//      rather their key were not on an unencrypted disk in plain text.
//   3. `~/.config/Leaf/leaf.conf`, and **refused when anybody but its owner can read it**.
//      The server refuses to start open on the network without a key; this is the same rule
//      from the other end.
//
// Nothing here offers to fix any of it. Saying plainly what is missing, and where it goes,
// is not the same as managing it.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY changed)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY changed)
    /// The key arrives asynchronously — a keyring is a service, not a file — so nothing
    /// should decide what to show before this is true.
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(Storage storage READ storage NOTIFY changed)

public:
    /// Where the key came from. Worth showing rather than hiding: somebody told their key
    /// sits in a file can decide to move it, and somebody who is not, cannot.
    enum Storage { Unknown, Environment, Keyring, ProtectedFile };
    Q_ENUM(Storage)

    explicit Settings(QObject *parent = nullptr);

    QString address() const { return m_address; }
    QString key() const { return m_key; }
    bool loaded() const { return m_loaded; }
    bool configured() const { return !m_address.isEmpty() && !m_key.isEmpty(); }
    Storage storage() const { return m_storage; }

    /// Only for tests and for whatever sets things up before an engine exists. Nothing in
    /// the interface calls these — see the note at the top.
    void setAddress(const QString &address);
    void setKey(const QString &key);

    /// What is missing, and where it goes — in words, for the one screen that shows it.
    /// Empty when there is nothing to say.
    Q_PROPERTY(QString missing READ missing NOTIFY changed)
    QString missing() const;

    /// Where the configuration file lives.
    static QString configurationFile();



signals:
    void changed();
    /// Something went wrong that a person should see — the keyring refused, the file could
    /// not be written. Never carries the key.
    void trouble(const QString &what);

private:
    void load();
    bool fromEnvironment();
    void fromFile();

    QString m_address;
    QString m_key;
    bool m_loaded = false;
    Storage m_storage = Unknown;
};
