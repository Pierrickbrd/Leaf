#include "Profiles.h"

#include "Words.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

QString spell(Api::Kind kind)
{
    switch (kind) {
    case Api::Kind::Principal:
        return u"principal"_s;
    case Api::Kind::Reader:
        return u"reader"_s;
    case Api::Kind::Restricted:
        return u"restricted"_s;
    case Api::Kind::Other:
        break;
    }
    return u"other"_s;
}

/// A restricted profile, or one this client cannot name. Judged here and not by the rights
/// the server sent: an unfamiliar kind arrives with whatever rights its author gave it, and
/// the least is what an unknown profile gets.
bool narrow(const Api::Profile &profile)
{
    return profile.kind == Api::Kind::Restricted || profile.kind == Api::Kind::Other;
}

/// The first letter of the name, upper-cased — a code point and not a `QChar`, so that a name
/// opening on a letter outside the basic plane is not cut in the middle of it.
QString initialOf(const QString &name)
{
    if (name.isEmpty())
        return {};
    const QList<uint> points = name.toUcs4();
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(points.constData()), 1)
        .toUpper();
}

QVariantMap shown(const Api::Profile &profile, bool current)
{
    return {{u"id"_s, profile.id},
            {u"name"_s, profile.name},
            {u"initial"_s, initialOf(profile.name)},
            {u"color"_s, profile.color},
            {u"kind"_s, spell(profile.kind)},
            {u"note"_s, Words::profileNote(profile.kind, profile.maxAge)},
            {u"current"_s, current},
            {u"maxAge"_s, profile.maxAge.value_or(0)},
            {u"rights"_s,
             QVariantMap{{u"import"_s, !narrow(profile) && profile.rights.import},
                         {u"edit"_s, !narrow(profile) && profile.rights.edit},
                         {u"erase"_s, !narrow(profile) && profile.rights.erase}}}};
}

/// What a screen holds, as the contract spells it: the right to delete is `delete` on the
/// wire and `erase` here, because `delete` is a keyword the QML side would also trip on.
QJsonObject bodyOf(const QVariantMap &from)
{
    QJsonObject body = QJsonObject::fromVariantMap(from);
    if (from.contains(u"rights"_s)) {
        const QVariantMap rights = from.value(u"rights"_s).toMap();
        QJsonObject sent;
        for (const QString &key : {u"import"_s, u"edit"_s})
            if (rights.contains(key))
                sent.insert(key, rights.value(key).toBool());
        if (rights.contains(u"erase"_s))
            sent.insert(u"delete"_s, rights.value(u"erase"_s).toBool());
        body.insert(u"rights"_s, sent);
    }
    return body;
}

QString at(const QString &root, const QString &id)
{
    return root + QString::fromUtf8(QUrl::toPercentEncoding(id));
}

} // namespace

Profiles *Profiles::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — nobody will be asked "
                              "who reads");
    }
    return new Profiles(server);
}

Profiles::Profiles(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    if (!m_server)
        return;
    connect(m_server, &Server::mustChoose, this, [this] {
        m_mustChoose = true;
        emit changed();
    });
}

const Api::Profile *Profiles::current() const
{
    // A device that must choose or is held sits on the principal only to hold its key. Read
    // as the principal, it went to a protected principal without the code, and the server's
    // refusal had no window to be said in: touching the tile did nothing at all.
    if (choosing() || held())
        return nullptr;
    for (const Api::Profile &one : m_seating.profiles)
        if (one.id == m_seating.active)
            return &one;
    return nullptr;
}

QVariantList Profiles::list() const
{
    const Api::Profile *it = current();
    QVariantList all;
    for (const Api::Profile &one : m_seating.profiles)
        all.append(shown(one, &one == it));
    return all;
}

QVariantMap Profiles::active() const
{
    const Api::Profile *it = current();
    return it ? shown(*it, true) : QVariantMap();
}

bool Profiles::isPrincipal() const
{
    const Api::Profile *it = current();
    return it && it->kind == Api::Kind::Principal;
}

bool Profiles::isRestricted() const
{
    const Api::Profile *it = current();
    return it && narrow(*it);
}

// Nothing is claimed before the server has said who is seated: no rights, and not « restricted »
// either, because nothing is known to restrict.
bool Profiles::canImport() const
{
    const Api::Profile *it = current();
    return it && !narrow(*it) && it->rights.import;
}

bool Profiles::canEdit() const
{
    const Api::Profile *it = current();
    return it && !narrow(*it) && it->rights.edit;
}

bool Profiles::canErase() const
{
    const Api::Profile *it = current();
    return it && !narrow(*it) && it->rights.erase;
}

bool Profiles::canExport() const
{
    const Api::Profile *it = current();
    return it && !narrow(*it);
}

bool Profiles::asksWhoReads() const
{
    return choosing() || held() || (several() && !m_seating.kept && !isRestricted());
}

QVariantMap Profiles::words() const
{
    return {{u"whoReads"_s, Words::whoReads()},
            {u"wrongCode"_s, Words::wrongCode()},
            {u"cancel"_s, Words::cancel()},
            {u"thisDevice"_s, Words::thisDevice()},
            {u"addOne"_s, Words::addOne()},
            {u"keepOnThisDevice"_s, Words::keepOnThisDevice()},
            {u"manageProfiles"_s, Words::manageProfiles()},
            {u"switchNeedsCode"_s, Words::switchNeedsCode()},
            {u"backToShelf"_s, Words::backToShelf()},
            {u"closeWhoReads"_s, Words::closeWhoReads()},
            {u"leaveNeedsCode"_s, Words::leaveNeedsCode()},
            {u"enterCode"_s, Words::enterCode()},
            {u"addAProfile"_s, Words::addAProfile()},
            {u"addNeedsCode"_s, Words::addNeedsCode()},
            {u"forgotCode"_s, Words::forgotCode()},
            {u"codeReplacedOnServer"_s, Words::codeReplacedOnServer()},
            {u"resetCodeCommand"_s, Words::resetCodeCommand()},
            {u"back"_s, Words::back()},
            {u"confirm"_s, Words::confirm()},
            {u"newProfile"_s, Words::newProfile()},
            {u"yourProfile"_s, Words::yourProfile()},
            {u"profileName"_s, Words::profileName()},
            {u"profileColour"_s, Words::profileColour()},
            {u"takenColourNote"_s, Words::takenColourNote()},
            {u"whatItMayDo"_s, Words::whatItMayDo()},
            {u"readerRole"_s, Words::readerRole()},
            {u"restrictedRole"_s, Words::restrictedRole()},
            {u"mayImport"_s, Words::mayImport()},
            {u"mayEdit"_s, Words::mayEdit()},
            {u"mayErase"_s, Words::mayErase()},
            {u"readerExplained"_s, Words::readerExplained()},
            {u"restrictedExplained"_s, Words::restrictedExplained()},
            {u"seriesUpTo"_s, Words::seriesUpTo()},
            {u"codeToCreate"_s, Words::codeToCreate()},
            {u"codeAgain"_s, Words::codeAgain()},
            {u"aCode"_s, Words::aCode()},
            {u"whenCodeAsked"_s, Words::whenCodeAsked()},
            {u"toLeaveRestricted"_s, Words::toLeaveRestricted()},
            {u"alsoToComeBack"_s, Words::alsoToComeBack()},
            {u"principalExplained"_s, Words::principalExplained()},
            {u"codeHintFree"_s, Words::codeHintFree()},
            {u"principalKind"_s, Words::principalKind()},
            {u"readerKind"_s, Words::readerKind()},
            {u"restrictedKind"_s, Words::restrictedKind()},
            {u"youMark"_s, Words::youMark()},
            {u"retainedHere"_s, Words::retainedHere()},
            {u"eraseConsequence"_s, Words::eraseConsequence()},
            {u"profilesTab"_s, Words::profilesTab()},
            {u"whatEachMayDo"_s, Words::whatEachMayDo()},
            {u"devicesTab"_s, Words::devicesTab()},
            {u"aboutTab"_s, Words::aboutTab()},
            {u"atOpening"_s, Words::atOpening()},
            {u"opensOnThisComputer"_s, Words::opensOnThisComputer()},
            {u"askWhoReads"_s, Words::askWhoReads()},
            {u"otherDevices"_s, Words::otherDevices()},
            {u"removeDevice"_s, Words::removeDevice()},
            {u"shortcuts"_s, Words::shortcuts()},
            {u"thisClient"_s, Words::thisClient()},
            {u"serverHeading"_s, Words::serverHeading()}};
}

QString Profiles::saySwitchToProfile(const QString &name) const
{
    return Words::switchToProfile(name);
}

QString Profiles::sayEditProfile(const QString &name) const
{
    return Words::editProfile(name);
}

QString Profiles::sayYearsOld(int age) const
{
    return Words::yearsOld(age);
}

QString Profiles::sayCodeHintHeld(const QString &name) const
{
    return Words::codeHintHeld(name);
}

QString Profiles::sayRetainedOn(const QString &device) const
{
    return Words::retainedOn(device);
}

QString Profiles::saySeriesUpToYears(int age) const
{
    return Words::seriesUpToYears(age);
}

QString Profiles::sayEraseProfile(const QString &name) const
{
    return Words::eraseProfile(name);
}

QString Profiles::sayOpensOn(const QString &name) const
{
    return Words::opensOn(name);
}

QString Profiles::saySeriesForYou(int count) const
{
    return Words::seriesForYou(count);
}

QString Profiles::sayProfileLine(const QVariantMap &profile, const QStringList &marks) const
{
    const int age = profile.value(u"maxAge"_s).toInt();
    const Api::Kind said = Api::kind(profile.value(u"kind"_s).toString());
    return Words::profileLine(said, age > 0 ? std::optional<int>(age) : std::nullopt, marks);
}

void Profiles::take(const Api::Seating &seating)
{
    m_seating = seating;
    // The server's own word on whether this device must choose is the truth again.
    m_mustChoose = false;
    if (!m_seated) {
        m_seated = true;
        m_unproven = asksWhoReads() && isPrincipal()
                     && m_seating.code == Api::CodeScope::Everything;
    }
}

bool Profiles::failed(const Server::Answer &answer)
{
    if (answer.went()) {
        m_trouble.clear();
        return false;
    }
    // `code` is the server's word for a missing or wrong code on any route that takes one.
    // `Server` words a 403 as the key's refusal, which is a different sentence about a
    // different thing.
    if (answer.status == 403
        && answer.body.object().value(u"error"_s).toString() == u"code"_s) {
        m_trouble = Words::wrongCode();
        emit codeRefused();
    } else if (answer.status == 429) {
        // The pause after ten wrong codes. `Server` words a 429 as wrong keys, which is not
        // what was typed here.
        m_trouble = Words::tooManyTries(answer.retryAfter);
    } else {
        m_trouble = answer.trouble;
    }
    return true;
}

void Profiles::ask(Verb verb, const QString &path, const QJsonObject &body, const Then &ok)
{
    if (!m_server) {
        m_trouble = Words::notSetUp(Words::Asking::State);
        emit changed();
        return;
    }
    ++m_out;
    emit changed();

    const Then then = [this, ok](const Server::Answer &answer) {
        --m_out;
        if (failed(answer)) {
            emit changed();
            return;
        }
        ok(answer);
    };
    const QByteArray bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    switch (verb) {
    case Verb::Get:
        m_server->get(path, this, then);
        break;
    case Verb::Post:
        m_server->post(path, bytes, this, then);
        break;
    case Verb::Patch:
        m_server->patch(path, bytes, this, then);
        break;
    case Verb::Put:
        m_server->putJson(path, bytes, this, then);
        break;
    case Verb::Delete:
        m_server->remove(path, this, then);
        break;
    }
}

void Profiles::ask()
{
    ++m_generation;
    const int mine = m_generation;
    ask(Verb::Get, u"/profiles"_s, {}, [this, mine](const Server::Answer &answer) {
        // A later question has been asked since: its answer is the newer one.
        if (mine != m_generation) {
            emit changed();
            return;
        }
        const Api::Read<Api::Seating> read = Api::seating(answer.body.object());
        if (!read.ok()) {
            m_trouble = read.trouble;
            emit changed();
            return;
        }
        take(*read.value);
        emit changed();
    });
}

void Profiles::switchTo(const QString &id, const QString &code, bool keep)
{
    QJsonObject body{{u"id"_s, id}, {u"keep"_s, keep}};
    if (!code.isEmpty())
        body.insert(u"code"_s, code);
    ask(Verb::Put, u"/profiles/active"_s, body, [this, code](const Server::Answer &answer) {
        const Api::Read<Api::Seating> read = Api::seating(answer.body.object());
        if (!read.ok()) {
            m_trouble = read.trouble;
            emit changed();
            return;
        }
        // Keeping the profile already reading is not a switch: nothing shown under it is
        // stale, and saying so reloaded the whole shelf for a checkbox. Compared with the
        // profile read as, not the seat: a device that must choose sits on the principal, and
        // choosing the principal from there left the code window and « Qui lit ? » open.
        // A switch the code allowed is said even onto the profile already read as: a principal
        // not retained is asked the code to stay, and the code window and « Qui lit ? » wait
        // for it to let go.
        const bool another =
            read.value->active != active().value(u"id"_s).toString() || !code.isEmpty();
        m_unproven = false;
        take(*read.value);
        emit changed();
        if (another)
            emit switched();
    });
}

void Profiles::add(const QVariantMap &profile)
{
    ask(Verb::Post, u"/profiles"_s, bodyOf(profile), [this](const Server::Answer &) { ask(); });
}

void Profiles::change(const QString &id, const QVariantMap &profile)
{
    ask(Verb::Patch, at(u"/profiles/"_s, id), bodyOf(profile),
        [this](const Server::Answer &) { ask(); });
}

void Profiles::remove(const QString &id)
{
    ask(Verb::Delete, at(u"/profiles/"_s, id), {}, [this](const Server::Answer &) { ask(); });
}

void Profiles::setCode(const QString &code, const QString &again, const QString &scope)
{
    ask(Verb::Put, u"/profiles/code"_s,
        {{u"code"_s, code}, {u"codeAgain"_s, again}, {u"scope"_s, scope}},
        [this](const Server::Answer &) { ask(); });
}

void Profiles::clearCode()
{
    ask(Verb::Delete, u"/profiles/code"_s, {}, [this](const Server::Answer &) { ask(); });
}

void Profiles::devices()
{
    ask(Verb::Get, u"/devices"_s, {}, [this](const Server::Answer &answer) {
        const Api::Read<QList<Api::DeviceSeat>> read = Api::deviceSeats(answer.body.object());
        if (!read.ok()) {
            m_trouble = read.trouble;
            emit changed();
            return;
        }
        m_devices.clear();
        for (const Api::DeviceSeat &one : *read.value) {
            m_devices.append(QVariantMap{{u"id"_s, one.id},
                                         {u"name"_s, one.name},
                                         {u"profile"_s, one.profile},
                                         {u"kept"_s, one.kept},
                                         {u"here"_s, one.here},
                                         {u"seenAt"_s, one.seenAt ? QVariant(*one.seenAt)
                                                                  : QVariant()}});
        }
        emit changed();
    });
}

void Profiles::placeDevice(const QString &id, const QString &profile, bool kept)
{
    ask(Verb::Patch, at(u"/devices/"_s, id),
        {{u"profile"_s, profile}, {u"kept"_s, kept}},
        [this](const Server::Answer &) { devices(); });
}

void Profiles::removeDevice(const QString &id)
{
    ask(Verb::Delete, at(u"/devices/"_s, id), {}, [this](const Server::Answer &) { devices(); });
}

void Profiles::reading()
{
    ask(Verb::Get, u"/profile/reading"_s, {}, [this](const Server::Answer &answer) {
        m_reading = answer.body.object().toVariantMap();
        emit changed();
    });
}

void Profiles::keepReading(const QVariantMap &preferences)
{
    ask(Verb::Put, u"/profile/reading"_s, QJsonObject::fromVariantMap(preferences),
        [this, preferences](const Server::Answer &) {
            m_reading = preferences;
            emit changed();
        });
}
