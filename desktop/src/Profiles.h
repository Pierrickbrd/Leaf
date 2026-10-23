#pragma once

// Who reads, and what that profile may do.
//
// The server keeps the profiles and decides every right; this holds what it last said and
// turns it into the few questions a screen asks — may this profile import, is it asked who
// reads at launch, what does the code guard. Nothing here decides a right: a button that is
// hidden here is one the server refuses anyway, and the server's refusal is the one that
// counts.
//
// **The one thing a screen cannot know is when the answer changed under it.** A profile
// deleted on another device leaves this one with a seat that no longer exists, and the server
// then answers every route but two with `409 choose a profile`. Whichever request meets that
// first — a shelf, a cover, a progress write — is not the screen that will ask who reads, so
// `Server` says it once and this turns it into `asksWhoReads`.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Profiles : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Every profile as `{id, name, initial, color, kind, note, current, maxAge, rights}`,
    /// the principal first. `kind` is `principal`, `reader`, `restricted` or `other` — the
    /// last for a word this client was not taught.
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    /// The profile this device reads as, in the same shape; empty until the server has said.
    Q_PROPERTY(QVariantMap active READ active NOTIFY changed)
    Q_PROPERTY(bool isPrincipal READ isPrincipal NOTIFY changed)
    /// A restricted profile, or one of a kind this client cannot name: the narrowest reading
    /// is the safe one for a profile that is not understood.
    Q_PROPERTY(bool isRestricted READ isRestricted NOTIFY changed)
    Q_PROPERTY(bool canImport READ canImport NOTIFY changed)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY changed)
    Q_PROPERTY(bool canErase READ canErase NOTIFY changed)
    /// Taking a volume out of the library is not a right of its own: whoever is not restricted.
    Q_PROPERTY(bool canExport READ canExport NOTIFY changed)
    /// At least two profiles: with one there is nobody to choose between.
    Q_PROPERTY(bool several READ several NOTIFY changed)
    /// Whether the launch asks « Qui lit ? ». A device that must choose or is held always
    /// does; one that keeps a profile never does; and a restricted profile is not asked even
    /// when it is not kept, because leaving it needs the code and is not the launch's to offer.
    Q_PROPERTY(bool asksWhoReads READ asksWhoReads NOTIFY changed)
    /// `none`, `restricted` or `everything`: what the code is for, never the code.
    Q_PROPERTY(QString codeScope READ codeScope NOTIFY changed)
    /// A device that must choose before anything else, as the server last said it or as a
    /// refusal of any route announced it.
    Q_PROPERTY(bool choosing READ choosing NOTIFY changed)
    Q_PROPERTY(bool held READ held NOTIFY changed)
    /// A request of this model is out.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What went wrong last, in words; empty after a request that went.
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// Every device and the profile it sits on — the principal's Devices tab.
    Q_PROPERTY(QVariantList deviceList READ deviceList NOTIFY changed)
    /// The reading preferences of the profile this device reads as, as they were last written.
    Q_PROPERTY(QVariantMap readingPreferences READ readingPreferences NOTIFY changed)
    /// The words the model itself says. French lives in `Words`, and a screen never spells it.
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    explicit Profiles(Server *server, QObject *parent = nullptr);

    static Profiles *create(QQmlEngine *engine, QJSEngine *);

    QVariantList list() const;
    QVariantMap active() const;
    bool isPrincipal() const;
    bool isRestricted() const;
    bool canImport() const;
    bool canEdit() const;
    bool canErase() const;
    bool canExport() const;
    bool several() const { return m_seating.profiles.size() >= 2; }
    bool asksWhoReads() const;
    QString codeScope() const { return Api::spell(m_seating.code); }
    bool choosing() const { return m_seating.choosing || m_mustChoose; }
    bool held() const { return m_seating.held; }
    bool busy() const { return m_out > 0; }
    QString trouble() const { return m_trouble; }
    QVariantList deviceList() const { return m_devices; }
    QVariantMap readingPreferences() const { return m_reading; }
    QVariantMap words() const;

    /// Who reads, now. Asked at launch and after anything that may have changed it.
    Q_INVOKABLE void ask();
    /// Read as `id`. `code` only when the change asks for it — an empty one is a wrong one —
    /// and `keep` makes this device open on it next time.
    Q_INVOKABLE void switchTo(const QString &id, const QString &code, bool keep);
    /// `{name, color, kind, maxAge, rights: {import, edit, erase}, code, codeAgain,
    /// principalCode}`; what is absent is not sent.
    Q_INVOKABLE void add(const QVariantMap &profile);
    /// The same keys, and only what changes.
    Q_INVOKABLE void change(const QString &id, const QVariantMap &profile);
    Q_INVOKABLE void remove(const QString &id);
    /// `scope` is `restricted` or `everything`.
    Q_INVOKABLE void setCode(const QString &code, const QString &again, const QString &scope);
    Q_INVOKABLE void clearCode();
    Q_INVOKABLE void devices();
    Q_INVOKABLE void placeDevice(const QString &id, const QString &profile, bool kept);
    Q_INVOKABLE void removeDevice(const QString &id);
    Q_INVOKABLE void reading();
    /// Replaces them whole: the server keeps the object as sent and never reads it.
    Q_INVOKABLE void keepReading(const QVariantMap &preferences);

signals:
    void changed();
    /// A code was asked for and was wrong. Said apart from `trouble` so that the field can
    /// be cleared and shaken, which a sentence alone cannot do.
    void codeRefused();
    /// A profile was chosen. Everything shown under the old one is stale: the screens that
    /// hold a shelf, a resume band or a reading state ask again.
    void switched();

private:
    using Then = std::function<void(const Server::Answer &)>;

    /// Which verb, and the one place that counts a request out and back, says what went wrong
    /// and keeps the model from showing « busy » after a failure. `ok` is called only for an
    /// answer that went.
    enum class Verb { Get, Post, Patch, Put, Delete };
    void ask(Verb verb, const QString &path, const QJsonObject &body, const Then &ok);
    bool failed(const Server::Answer &answer);
    const Api::Profile *current() const;
    void take(const Api::Seating &seating);

    Server *m_server;
    Api::Seating m_seating;
    /// A refusal of some route said the device must choose; cleared by the next answer to
    /// `GET /profiles`, which says it for itself.
    bool m_mustChoose = false;
    int m_out = 0;
    int m_generation = 0;
    QString m_trouble;
    QVariantList m_devices;
    QVariantMap m_reading;
};
