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
#include <QStringList>
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
    /// Whether this device opens on the profile it reads as.
    Q_PROPERTY(bool kept READ kept NOTIFY changed)
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
    /// The launch found this device on the principal without keeping it, while the code guards
    /// the way back to the principal: nobody has shown yet that the principal is the one
    /// reading. Until a profile is chosen, « Qui lit ? » cannot be left — no cross, no Escape,
    /// no adding, no settings — which is what the code, asked of the principal's tile on any
    /// seat not retained, cannot cover by itself.
    Q_PROPERTY(bool unproven READ unproven NOTIFY changed)
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
    bool kept() const { return m_seating.kept; }
    bool several() const { return m_seating.profiles.size() >= 2; }
    bool asksWhoReads() const;
    QString codeScope() const { return Api::spell(m_seating.code); }
    bool choosing() const { return m_seating.choosing || m_mustChoose; }
    bool held() const { return m_seating.held; }
    bool unproven() const { return m_unproven; }
    bool busy() const { return m_out > 0; }
    QString trouble() const { return m_trouble; }
    QVariantList deviceList() const { return m_devices; }
    QVariantMap readingPreferences() const { return m_reading; }
    QVariantMap words() const;
    /// The texts that carry a name or a number are asked one by one: a map of finished strings
    /// cannot hold a hole.
    Q_INVOKABLE QString saySwitchToProfile(const QString &name) const;
    Q_INVOKABLE QString sayEditProfile(const QString &name) const;
    Q_INVOKABLE QString sayYearsOld(int age) const;
    Q_INVOKABLE QString sayCodeHintHeld(const QString &name) const;
    Q_INVOKABLE QString sayRetainedOn(const QString &device) const;
    Q_INVOKABLE QString saySeriesUpToYears(int age) const;
    Q_INVOKABLE QString sayEraseProfile(const QString &name) const;
    Q_INVOKABLE QString sayEraseProfileButton(const QString &name) const;
    Q_INVOKABLE QString sayAddProfileButton(const QString &name) const;
    Q_INVOKABLE QString sayPrincipalManages(const QString &principal) const;
    Q_INVOKABLE QString sayColourName(const QString &id, const QString &owner) const;
    Q_INVOKABLE QString sayOpensOn(const QString &name) const;
    Q_INVOKABLE QString saySeriesForYou(int count) const;
    Q_INVOKABLE QString sayManagedByPrincipal(const QString &principal) const;
    Q_INVOKABLE QString sayOnlyReads(const QString &name) const;
    Q_INVOKABLE QString sayRemoveDevice(const QString &name) const;
    /// The line under a profile's name, from the map `profiles` holds for it: its kind, its age,
    /// and the marks the screen knows (`you`, a device it is kept on).
    Q_INVOKABLE QString sayProfileLine(const QVariantMap &profile, const QStringList &marks) const;

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
    /// `scope` is `restricted` or `everything`. `current` is the code the household has now,
    /// which the server asks whenever there is one: empty for the first, which has none.
    Q_INVOKABLE void setCode(const QString &code, const QString &again, const QString &scope,
                             const QString &current);
    /// Takes the code off; `current` is the code being removed, asked like for a change.
    Q_INVOKABLE void clearCode(const QString &current);
    /// Draws the recovery code, the way back from a forgotten code, and says it with
    /// `recoveryDrawn`: the only time the server ever sends it. Drawing another revokes it.
    /// `current` is the code the household has now: a recovery code is a code replaced without
    /// the old one, so the server asks for it as it does for a change.
    Q_INVOKABLE void drawRecovery(const QString &current);
    /// The same, made right after the household's first code: a refusal then is said in the
    /// recovery window (« the code is saved, its recovery code is not »), not in the card.
    void drawRecoveryAfterNewCode(const QString &current);
    /// A new code with the recovery code in place of the old one, from any device. Said with
    /// `codeRecovered`, carrying the recovery code that replaces the one just spent.
    Q_INVOKABLE void recover(const QString &recoveryCode, const QString &code,
                             const QString &again);
    Q_INVOKABLE void devices();
    Q_INVOKABLE void placeDevice(const QString &id, const QString &profile, bool kept);
    Q_INVOKABLE void removeDevice(const QString &id);
    Q_INVOKABLE void reading();
    /// Replaces them whole: the server keeps the object as sent and never reads it.
    Q_INVOKABLE void keepReading(const QVariantMap &preferences);

signals:
    void changed();
    /// The recovery code, once. A window shows it and nothing keeps it: not this object, not
    /// the log, not the clipboard.
    void recoveryDrawn(const QString &code);
    /// The recovery code could not be drawn, in a sentence that says the code itself is kept.
    void recoveryFailed(const QString &sentence);
    /// A recovery code asked for from the settings card was refused, in the card's own words
    /// (wrong code, too many tries): nothing was saved or lost, there is no window to open.
    void recoveryRefused(const QString &sentence);
    /// The code was replaced by the recovery code; this is the next one, once.
    void codeRecovered(const QString &next);
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
    /// Set for the one request that sends a recovery code, so that its refusal is worded as
    /// one and not as a wrong code.
    bool m_recovering = false;
    /// Set for the request that draws a recovery code: its refusal is not the profile window's
    /// trouble — the window that made the code has done its job — but the recovery window's.
    bool m_drawing = false;
    /// Whether the draw in flight follows the first code, which decides where a refusal is said.
    bool m_afterNewCode = false;
    void ask(Verb verb, const QString &path, const QJsonObject &body, const Then &ok);
    bool failed(const Server::Answer &answer);
    /// `GET /profiles`; `announce` says `switched` when the profile read as has changed.
    void reseat(bool announce);
    const Api::Profile *current() const;
    void take(const Api::Seating &seating);

    Server *m_server;
    Api::Seating m_seating;
    /// A refusal of some route said the device must choose; cleared by the next answer to
    /// `GET /profiles`, which says it for itself.
    bool m_mustChoose = false;
    /// Decided by the first seating heard, and only by it: a principal who unticks « Retenir »
    /// mid-session has shown who they are, and is not locked out by their own change.
    bool m_seated = false;
    bool m_unproven = false;
    int m_out = 0;
    int m_generation = 0;
    QString m_trouble;
    QVariantList m_devices;
    QVariantMap m_reading;
};
