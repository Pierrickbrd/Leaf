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
    return {{u"whoReads"_s, Words::whoReads()}, {u"wrongCode"_s, Words::wrongCode()}};
}

void Profiles::take(const Api::Seating &seating)
{
    m_seating = seating;
    // The server's own word on whether this device must choose is the truth again.
    m_mustChoose = false;
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
    ask(Verb::Put, u"/profiles/active"_s, body, [this](const Server::Answer &answer) {
        const Api::Read<Api::Seating> read = Api::seating(answer.body.object());
        if (!read.ok()) {
            m_trouble = read.trouble;
            emit changed();
            return;
        }
        take(*read.value);
        emit changed();
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
