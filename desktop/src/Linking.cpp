#include "Linking.h"

#include "Api.h"
#include "Pinning.h"
#include "Settings.h"
#include "Words.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>

using namespace Qt::StringLiterals;

namespace {

/// How long an address may take to answer before it is said not to. A link to an address
/// this device cannot route to — the server's other network, a VPN that is down — is not
/// refused, it is silent, and the system's own wait for a connection runs to minutes.
constexpr int Patience = 15000;

bool sameLink(const std::optional<Link> &one, const Link &other)
{
    return one && one->address == other.address && one->code == other.code
           && one->fingerprint == other.fingerprint;
}

} // namespace

Linking::Linking(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_name(QSysInfo::machineHostName())
{
    // Asked at each handshake rather than copied: the pin is the link's, and it changes with
    // the link offered.
    Pinning::guard(m_network, [this] { return m_pin; });

    // The key is stored once `Settings` says so, and not when it was handed over: the keyring
    // answers later, and may refuse, and so may the file.
    connect(settings, &Settings::changed, this, [this] {
        if (!m_storing || !m_settings->configured()) {
            return;
        }
        m_storing = false;
        m_key.reset();
        become(Stage::Linked);
        emit linked();
    });
    // The first screen to listen: what `Settings` could not do is said here, in its words.
    // While a key waits to be stored, it is the reason that key is not — and « Relier » then
    // tries the writing again.
    connect(settings, &Settings::trouble, this, [this](const QString &what) {
        if (m_storing) {
            m_storing = false;
            m_stage = Stage::Found;
        }
        m_sentence = what;
        emit changed();
    });
}

Linking *Linking::create(QQmlEngine *engine, QJSEngine *)
{
    // Resolved here for the reason `Server::create` gives: a QML singleton is built with
    // nothing but the engine to go on.
    auto *settings =
        engine->singletonInstance<Settings *>(qmlTypeId("Leaf", 1, 0, "Settings"));
    if (!settings) {
        qWarning().noquote()
            << u"error resolving the Settings singleton — nothing can be linked"_s;
        return nullptr;
    }
    return new Linking(settings);
}

void Linking::setName(const QString &name)
{
    if (name == m_name) {
        return;
    }
    m_name = name;
    emit changed();
}

bool Linking::canLink() const
{
    if (m_key) {
        return !m_storing;
    }
    return (m_stage == Stage::Found || m_stage == Stage::NameTaken)
           && !m_name.trimmed().isEmpty();
}

QString Linking::title() const
{
    return Words::noLibraryLinked();
}

QString Linking::intro() const
{
    return Words::linkIntro();
}

QString Linking::linkField() const
{
    return Words::linkField();
}

QString Linking::linkPlaceholder() const
{
    return Words::linkPlaceholder();
}

QString Linking::nameField() const
{
    return Words::deviceNameField();
}

QString Linking::nameHelp() const
{
    return Words::deviceNameHelp();
}

QString Linking::action() const
{
    return Words::linkAction(!m_found.isEmpty());
}

void Linking::offer(const QString &typed)
{
    if (m_stage == Stage::Sending || m_storing) {
        return;
    }
    m_typed = typed;

    if (typed.trimmed().isEmpty()) {
        drop();
        m_link.reset();
        m_key.reset();
        m_found.clear();
        m_detail.clear();
        become(Stage::Empty);
        return;
    }

    const std::optional<Link> read = Link::read(typed);
    if (!read) {
        drop();
        m_link.reset();
        m_key.reset();
        m_found.clear();
        m_detail.clear();
        become(Stage::NotALink, Words::notALink());
        return;
    }

    // The same link, reformatted — a space typed, a dash added — is not a reason to knock
    // again, unless knocking is what failed.
    if (sameLink(m_link, *read) && m_stage != Stage::Unreachable) {
        emit changed();
        return;
    }
    m_link = read;
    m_key.reset();
    check();
}

void Linking::clearNotice()
{
    if (m_notice.isEmpty()) {
        return;
    }
    m_notice.clear();
    emit changed();
}

void Linking::arrive(const QString &link)
{
    // The key comes from the keyring a moment after the window does, and a linked client
    // that took the link for the first of its life would check a server it never needed
    // and then, once the key arrived, hide the answer.
    if (!m_settings->loaded()) {
        auto *wait = new QObject(this);
        connect(m_settings, &Settings::changed, wait, [this, wait, link] {
            if (!m_settings->loaded()) {
                return;
            }
            wait->deleteLater();
            arrive(link);
        });
        return;
    }
    if (m_settings->configured()) {
        m_notice = Words::alreadyLinked();
        emit changed();
        emit refusedWhileLinked();
        return;
    }
    // The latest click wins over the link on the screen — the host is shown before
    // « Relier », so nothing is accepted unseen — except once a code is on its way or its key
    // is held: a click arriving then would change what the key is stored for.
    if (m_stage == Stage::Sending || m_storing || m_key) {
        return;
    }
    offer(link);
}

void Linking::startOver(const QString &why)
{
    drop();
    m_link.reset();
    m_key.reset();
    m_storing = false;
    m_linkedName.clear();
    m_found.clear();
    m_detail.clear();
    m_typed.clear();
    become(Stage::Empty, why);
}

void Linking::lookAtTheClipboard()
{
    // Never over what is already there: a link handed over by a click wins over whatever
    // happened to be copied last.
    if (!m_typed.isEmpty()) {
        return;
    }
    const QString copied = QGuiApplication::clipboard()->text();
    if (Link::read(copied)) {
        offer(copied);
    }
}

void Linking::check()
{
    m_found.clear();
    m_detail.clear();
    become(Stage::Checking, Words::lookingForTheLibrary());
    QNetworkReply *reply = send("GET", u"/health"_s, {});
    connect(reply, &QNetworkReply::finished, this, [this, reply] { heard(reply); });
}

void Linking::heard(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    // Refused for its certificate — the pinned one, or with no pin the system's — is not an
    // address that does not answer: somebody answered, and it was not the library the link
    // names. There is no way past this from here, on purpose.
    if (Pinning::mismatched(reply) || reply->error() == QNetworkReply::SslHandshakeFailedError) {
        become(Stage::Mismatch, Words::certificateMismatch());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // Something answered, and it has to be Leaf: a router's page at that port answers 200 too.
    const Api::Read<Api::Health> health =
        Api::health(QJsonDocument::fromJson(reply->readAll()).object());
    if (status != 200 || !health.ok()) {
        become(Stage::Unreachable, Words::noLibraryAnswers());
        return;
    }
    m_found = Words::libraryFound(m_link->address.host());
    m_detail = Words::libraryFoundDetail(!m_link->fingerprint.isEmpty(), health.value->library);
    become(Stage::Found);
}

void Linking::link()
{
    if (!canLink()) {
        return;
    }
    // The code is spent and the key is in hand; only the writing failed.
    if (m_key) {
        store();
        return;
    }
    const QByteArray body = QJsonDocument(QJsonObject{
                                              {u"code"_s, m_link->code},
                                              {u"name"_s, m_name.trimmed()},
                                          })
                                .toJson(QJsonDocument::Compact);
    become(Stage::Sending);
    QNetworkReply *reply = send("POST", u"/link"_s, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { answered(reply); });
}

void Linking::answered(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    if (Pinning::mismatched(reply) || reply->error() == QNetworkReply::SslHandshakeFailedError) {
        m_found.clear();
        m_detail.clear();
        become(Stage::Mismatch, Words::certificateMismatch());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    switch (status) {
    case 201: {
        const Api::Read<Api::Linked> said = Api::linked(body);
        if (!said.ok()) {
            become(Stage::Found, Words::unreadableAnswer());
            return;
        }
        m_key = said.value->key;
        m_linkedName = said.value->device.name;
        store();
        return;
    }
    case 409:
        become(Stage::NameTaken,
               Words::nameTaken(body.value(u"name"_s).toString(m_name.trimmed())));
        return;
    case 410:
        become(Stage::CodeNotValid, Words::linkNoLongerValid());
        return;
    case 400:
        become(Stage::Found, Words::nameNotAccepted());
        return;
    case 429:
        become(Stage::Found,
               Words::tooManyTries(QString::fromUtf8(reply->rawHeader("Retry-After"))));
        return;
    case 0:
        // It answered a moment ago and does not now: the same sentence as before, and the
        // link may be offered again once the address answers.
        m_found.clear();
        m_detail.clear();
        become(Stage::Unreachable, Words::noLibraryAnswers());
        return;
    default:
        become(Stage::Found,
               Words::serverAnswered(status, body.value(u"error"_s).toString()));
        return;
    }
}

void Linking::store()
{
    m_storing = true;
    become(Stage::Sending);
    // The address and the fingerprint that were checked, and nothing re-read since: the pin
    // `Settings` keeps is the one this device has just seen the server present.
    m_settings->remember(m_link->address, m_link->fingerprint, *m_key, m_linkedName,
                         QDateTime::currentDateTimeUtc());
}

QNetworkReply *Linking::send(const QByteArray &verb, const QString &path,
                             const QByteArray &body)
{
    drop();
    QUrl url = m_link->address;
    url.setPath(path);
    QNetworkRequest request = Pinning::request(url);
    // A redirect is not followed: the code goes to the address the link names and to no
    // other, whatever that address answers.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(Patience);
    if (verb == "POST") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_ba);
    }
    m_pin = m_link->fingerprint;
    // After `drop`, so the only reply it could cancel is already let go; and before the new
    // request, which must not ride a connection judged against another link's pin.
    Pinning::renew(m_network, m_pin);
    m_reply = verb == "GET" ? m_network.get(request)
                            : m_network.sendCustomRequest(request, verb, body);
    return m_reply;
}

void Linking::drop()
{
    if (!m_reply) {
        return;
    }
    // Disconnected before it is aborted: an aborted reply finishes on the spot, and its
    // « Operation canceled » would read as the address not answering.
    QNetworkReply *going = m_reply;
    m_reply = nullptr;
    going->disconnect(this);
    going->abort();
    going->deleteLater();
}

void Linking::become(Stage stage, const QString &sentence)
{
    m_stage = stage;
    m_sentence = sentence;
    emit changed();
}
