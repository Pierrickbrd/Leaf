#pragma once

// Where the server is, and how to prove who you are.
//
// **Written once linked, by nothing else.** This used to be read and never written: where a
// server lives was a deployment question, set by hand like the server's own configuration.
// What changed it is that the client now links itself — it is handed a `leaf://` link, trades
// the code in it for a key, and has to keep what it was given. So `remember`, `moveTo`,
// `rename` and `forget` write; nothing else does, and there is still no screen for typing an
// address or a key in.
//
// Three places, read in this order:
//
//   1. `LEAF_ADDRESS` and `LEAF_KEY` in the environment. What a launcher, a script or a
//      `systemd` unit sets, and what wins for one run. **Never overwritten**: while either is
//      set, every write is refused and says so — a link written underneath an environment
//      would be invisible until the day the environment went away.
//   2. The session keyring — GNOME's or KDE's — for the key. Where `remember` puts it.
//   3. `~/.config/Leaf/leaf.conf` for the address, the pinned fingerprint, the device's name
//      and the date it was linked; and for the key only when the keyring refused it — in
//      which case it wins over the keyring, whose entry can only be older. Written
//      readable by its owner alone, and **refused when anybody but its owner can read it**.
//      The server refuses to start open on the network without a key; this is the same rule
//      from the other end. `~/.config/Leaf/Leaf/leaf.conf`, where the client used to look,
//      is still read when nothing is where the installer writes.

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <functional>

class QSettings;

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
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QDateTime linkedAt READ linkedAt NOTIFY changed)

public:
    /// Where the key came from. Worth showing rather than hiding: somebody told their key
    /// sits in a file can decide to move it, and somebody who is not, cannot.
    enum class Storage { Unknown, Environment, Keyring, ProtectedFile };
    Q_ENUM(Storage)

    explicit Settings(QObject *parent = nullptr);

    QString address() const { return m_address; }
    QString key() const { return m_key; }
    /// The fingerprint of the certificate this device was linked to, or empty when the
    /// system's authorities vouch for the server instead — see `Pinning`. Always lowercase,
    /// whatever was handed in or found on disk: `Pinning` compares the whole string.
    QString fingerprint() const { return m_fingerprint; }
    /// What the server calls this device, and when it was linked — for the « Bibliothèque »
    /// settings tab. Empty and invalid when it was configured by hand.
    QString deviceName() const { return m_deviceName; }
    QDateTime linkedAt() const { return m_linkedAt; }
    bool loaded() const { return m_loaded; }
    bool configured() const { return !m_address.isEmpty() && !m_key.isEmpty(); }
    Storage storage() const { return m_storage; }

    /// In memory only, never written. For tests and for whatever sets things up before an
    /// engine exists; the interface goes through `remember` and the rest below.
    void setAddress(const QString &address);
    void setKey(const QString &key);
    void setFingerprint(const QString &fingerprint);

    /// What is missing, and where it goes — in words, for the one screen that shows it.
    /// Empty when there is nothing to say.
    Q_PROPERTY(QString missing READ missing NOTIFY changed)
    /// « La clé », and where it came from. Worded here for the same reason the rest is:
    /// a screen that spells French is a screen no test reads.
    Q_PROPERTY(QString keyTitle READ keyTitle CONSTANT)
    Q_PROPERTY(QString storageLabel READ storageLabel NOTIFY changed)
    QString keyTitle() const;
    QString storageLabel() const;
    /// The settings screen's own sections, `[{ name, label }]`, for a profile of `kind` (the
    /// server's word): what that profile manages and nothing else. Held by the object the
    /// screen is named after, because they belong to no one section; asked with the kind
    /// rather than bound to it so that switching profile redraws the tabs.
    Q_INVOKABLE QVariantList sections(const QString &kind) const;
    QString missing() const;

    /// Writes everything, then emits `changed`. The key goes to the keyring; if the keyring
    /// refuses it, to `leaf.conf` (0600), and `storage()` says so. Does nothing but emit
    /// `trouble` while the environment sets the address or the key.
    void remember(const QUrl &address, const QString &fingerprint, const QString &key,
                  const QString &deviceName, const QDateTime &linkedAt);
    /// Writes the device's new name. Allowed under the environment: the environment never
    /// names a device, so there is nothing of it to overwrite.
    void rename(const QString &deviceName);
    /// Writes a new address and the pin that goes with it — empty when an authority vouches
    /// for the new address. The key stays where it is. Refused under the environment.
    void moveTo(const QUrl &address, const QString &fingerprint);
    /// Erases the key from the keyring and from `leaf.conf`, the address, the pin, the name
    /// and the date. Refused under the environment.
    void forget();

    /// Where the configuration file lives: `~/.config/Leaf/leaf.conf`, where `install.sh`
    /// writes it.
    static QString configurationFile();

    /// Where it used to be read: `~/.config/Leaf/Leaf/leaf.conf`, the application's own
    /// folder under an organisation also called Leaf. Still read when the first is absent.
    static QString formerConfigurationFile();

    /// Makes every keyring write fail, for the one test that has to see the fallback on a
    /// machine whose keyring accepts everything. Nothing else calls it, and it does nothing
    /// outside `QStandardPaths` test mode.
    static void refuseTheKeyringForTests(bool refuse);

signals:
    void changed();
    /// Something went wrong that a person should see — the keyring refused, the file could
    /// not be written. Never carries the key.
    void trouble(const QString &what);

private:
    void load();
    bool fromEnvironment();
    void fromFile();
    /// Whether the environment set this run’s address or key — see the note at the top —
    /// and if so, says through `trouble` that the write was refused.
    bool refusedByEnvironment();
    /// Where `remember` goes once the keyring has answered.
    void keyStored(bool accepted, const QUrl &address, const QString &fingerprint,
                   const QString &key, const QString &deviceName, const QDateTime &linkedAt);
    /// Writes `key` back to the keyring, or erases the entry when `key` is empty. Best effort,
    /// unwaited.
    static void putBackInTheKeyring(const QString &key);
    /// Applies `edit` to `leaf.conf`, readable by its owner alone before anything is
    /// written. Says what went wrong through `trouble`.
    bool write(const std::function<void(QSettings &)> &edit);

    QString m_address;
    QString m_key;
    QString m_fingerprint;
    QString m_deviceName;
    QDateTime m_linkedAt;
    /// Counts `remember` and `forget`, so that a keyring answering late to one that has
    /// since been overtaken writes nothing: linking and unlinking at once must end unlinked.
    quint64 m_generation = 0;
    bool m_loaded = false;
    Storage m_storage = Storage::Unknown;
};
