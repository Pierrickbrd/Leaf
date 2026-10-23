// What the client knows of who reads: the profiles, where this device sits, and what that
// profile may do.
//
// Headless, against `Pretend`. The model outlives any screen — a refusal can arrive while the
// shelf is the one on show — so it is testable without one, which is the point. Every answer
// here is the shape `contract/openapi.yaml` gives it, built as JSON objects so that a typo in
// a field name is a typo in one place.

#include "Api.h"
#include "Pretend.h"
#include "Profiles.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject rightsOf(bool import, bool edit, bool erase)
{
    return QJsonObject{{u"import"_s, import}, {u"edit"_s, edit}, {u"delete"_s, erase}};
}

QJsonObject aProfile(const QString &id, const QString &name, const QString &color,
                     const QString &kind, const QJsonObject &rights,
                     std::optional<int> maxAge = std::nullopt)
{
    QJsonObject one{{u"id"_s, id}, {u"name"_s, name}, {u"color"_s, color},
                    {u"kind"_s, kind}, {u"rights"_s, rights}};
    if (maxAge)
        one.insert(u"maxAge"_s, *maxAge);
    return one;
}

QJsonObject principal()
{
    return aProfile(u"default"_s, u"Pierre"_s, u"emeraude"_s, u"principal"_s,
                    rightsOf(true, true, true));
}

QJsonObject reader()
{
    return aProfile(u"p2"_s, u"élodie"_s, u"bleu"_s, u"reader"_s, rightsOf(true, false, false));
}

QJsonObject child()
{
    return aProfile(u"p3"_s, u"Léo"_s, u"corail"_s, u"restricted"_s,
                    rightsOf(false, false, false), 12);
}

QJsonObject seating(const QJsonArray &profiles, const QString &active, bool kept = true,
                    bool choosing = false, bool held = false,
                    const QString &code = u"restricted"_s)
{
    return QJsonObject{{u"profiles"_s, profiles}, {u"active"_s, active}, {u"kept"_s, kept},
                       {u"choosing"_s, choosing}, {u"held"_s, held}, {u"code"_s, code}};
}

QByteArray asJson(const QJsonObject &body)
{
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray reply(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\nContent-Type: application/json\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

} // namespace

class HoldsTheProfiles : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Profiles *m_profiles = nullptr;

    void settle(const std::function<bool()> &until)
    {
        for (int i = 0; i < 500 && !until(); ++i)
            QTest::qWait(10);
    }

    /// The server answers `GET /profiles` with `body`, and anything else with nothing.
    void serving(const QJsonObject &body)
    {
        m_pretend->answerFor = [body](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? reply(200, asJson(body))
                                                        : QByteArray();
        };
    }

    void asked()
    {
        QSignalSpy changed(m_profiles, &Profiles::changed);
        m_profiles->ask();
        QVERIFY(changed.wait(5000));
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(
            QStringLiteral("http://127.0.0.1:%1").arg(m_pretend->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings);
        m_profiles = new Profiles(m_server);
    }

    void cleanup()
    {
        delete m_profiles;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void reads_who_is_seated_and_what_they_may_do()
    {
        serving(seating({principal(), reader(), child()}, u"p2"_s));
        asked();

        const QVariantList list = m_profiles->list();
        QCOMPARE(list.size(), 3);
        const QVariantMap first = list.at(0).toMap();
        QCOMPARE(first.value(u"id"_s).toString(), u"default"_s);
        QCOMPARE(first.value(u"initial"_s).toString(), u"P"_s);
        QCOMPARE(first.value(u"kind"_s).toString(), u"principal"_s);
        QCOMPARE(first.value(u"note"_s).toString(), Words::profileNote(Api::Kind::Principal, {}));
        QVERIFY(!first.value(u"current"_s).toBool());

        const QVariantMap second = list.at(1).toMap();
        // Uppercased, accent kept: « élodie » is read as « É », not left in lower case.
        QCOMPARE(second.value(u"initial"_s).toString(), u"É"_s);
        QCOMPARE(second.value(u"color"_s).toString(), u"bleu"_s);
        QVERIFY(second.value(u"current"_s).toBool());

        const QVariantMap third = list.at(2).toMap();
        QCOMPARE(third.value(u"kind"_s).toString(), u"restricted"_s);
        QCOMPARE(third.value(u"note"_s).toString(), Words::profileNote(Api::Kind::Restricted, 12));

        QCOMPARE(m_profiles->active().value(u"id"_s).toString(), u"p2"_s);
        QVERIFY(!m_profiles->isPrincipal());
        QVERIFY(!m_profiles->isRestricted());
        QVERIFY(m_profiles->canImport());
        QVERIFY(!m_profiles->canEdit());
        QVERIFY(!m_profiles->canErase());
        QVERIFY(m_profiles->canExport());
        QVERIFY(m_profiles->several());
        QCOMPARE(m_profiles->codeScope(), u"restricted"_s);
        QVERIFY(m_profiles->trouble().isEmpty());
    }

    void the_principal_may_do_everything()
    {
        serving(seating({principal(), reader()}, u"default"_s));
        asked();

        QVERIFY(m_profiles->isPrincipal());
        QVERIFY(m_profiles->canImport());
        QVERIFY(m_profiles->canEdit());
        QVERIFY(m_profiles->canErase());
        QVERIFY(m_profiles->canExport());
    }

    /// A device that must choose, or is held, sits on the principal only to hold its key.
    /// Read as the principal, it skipped the code on the way to a protected principal — and
    /// met the server's refusal with no window open to say it.
    void a_device_that_must_choose_or_is_held_reads_as_nobody()
    {
        for (const bool held : {false, true}) {
            serving(seating({principal(), reader()}, u"default"_s, false, !held, held,
                            u"everything"_s));
            asked();

            QVERIFY(!m_profiles->isPrincipal());
            QVERIFY(m_profiles->active().isEmpty());
            QVERIFY(!m_profiles->canImport());
            QVERIFY(!m_profiles->list().at(0).toMap().value(u"current"_s).toBool());
        }
    }

    void an_empty_name_has_no_initial()
    {
        serving(seating({aProfile(u"default"_s, u""_s, u"emeraude"_s, u"principal"_s,
                                  rightsOf(true, true, true))},
                        u"default"_s));
        asked();

        QCOMPARE(m_profiles->list().at(0).toMap().value(u"initial"_s).toString(), QString());
    }

    void an_unknown_kind_is_treated_as_restricted()
    {
        // The server sends every right true here on purpose: the word is not one this client
        // was taught, and handing such a profile more than the least would be the one mistake
        // that cannot be taken back.
        serving(seating({principal(),
                         aProfile(u"p9"_s, u"Invité"_s, u"ocre"_s, u"guest"_s,
                                  rightsOf(true, true, true))},
                        u"p9"_s));
        asked();

        QVERIFY(m_profiles->isRestricted());
        QVERIFY(!m_profiles->isPrincipal());
        QVERIFY(!m_profiles->canImport());
        QVERIFY(!m_profiles->canEdit());
        QVERIFY(!m_profiles->canErase());
        QVERIFY(!m_profiles->canExport());
        QCOMPARE(m_profiles->list().at(1).toMap().value(u"kind"_s).toString(), u"other"_s);
        // The profile stands in the list: a word the client lacks is not a reason to lose it.
        QCOMPARE(m_profiles->list().size(), 2);
    }

    void a_restricted_profile_may_not_export()
    {
        serving(seating({principal(), child()}, u"p3"_s));
        asked();

        QVERIFY(m_profiles->isRestricted());
        QVERIFY(!m_profiles->canExport());
        QVERIFY(!m_profiles->canImport());
    }

    void a_wrong_code_is_said_and_nothing_moves()
    {
        serving(seating({principal(), reader(), child()}, u"p2"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("PUT /profiles/active ")
                       ? reply(403, R"({"error":"code"})")
                       : QByteArray();
        };

        QSignalSpy refused(m_profiles, &Profiles::codeRefused);
        QSignalSpy switched(m_profiles, &Profiles::switched);
        m_profiles->switchTo(u"default"_s, u"nope"_s, false);
        settle([&] { return !refused.isEmpty(); });

        QCOMPARE(refused.size(), 1);
        QCOMPARE(switched.size(), 0);
        // Said in the profile's own words, not as « La clé a été refusée ».
        QCOMPARE(m_profiles->trouble(), Words::wrongCode());
        QCOMPARE(m_profiles->active().value(u"id"_s).toString(), u"p2"_s);
        QVERIFY(!m_profiles->busy());
        // A document, and not octets: `PUT` in this client was a file until now.
        QVERIFY(m_pretend->heard.contains("application/json"));
        QVERIFY(m_pretend->heard.contains(R"("code":"nope")"));
        QVERIFY(m_pretend->heard.contains(R"("id":"default")"));
    }

    /// The screens reach their French through the model: the map for the texts that stand alone,
    /// one call for each that carries a name, and the line under a profile from its own map.
    void the_screens_are_handed_their_words()
    {
        serving(seating({principal(), reader(), child()}, u"p2"_s));
        asked();

        const QVariantMap said = m_profiles->words();
        QCOMPARE(said.value(u"whoReads"_s).toString(), Words::whoReads());
        QCOMPARE(said.value(u"addAProfile"_s).toString(), Words::addAProfile());
        QCOMPARE(said.value(u"forgotCode"_s).toString(), Words::forgotCode());
        QCOMPARE(m_profiles->saySwitchToProfile(u"Élodie"_s), u"Passer à Élodie"_s);
        QCOMPARE(m_profiles->saySeriesForYou(3), u"3 séries pour vous"_s);

        const QVariantList list = m_profiles->list();
        QCOMPARE(m_profiles->sayProfileLine(list.at(0).toMap(), {Words::youMark()}),
                 u"Profil principal · vous"_s);
        QCOMPARE(m_profiles->sayProfileLine(list.at(2).toMap(), {}),
                 Words::profileLine(Api::Kind::Restricted, 12, {}));
    }

    /// Keeping the profile already reading changes where this device opens, not who reads:
    /// nothing on screen is stale, and a « switched » here reloaded the whole shelf.
    void keeping_the_same_profile_is_not_a_switch()
    {
        serving(seating({principal(), reader()}, u"default"_s));
        asked();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("PUT /profiles/active ")
                       ? reply(200, asJson(seating({principal(), reader()}, u"default"_s,
                                                   true, false, false, u"none"_s)))
                       : QByteArray();
        };

        QSignalSpy switched(m_profiles, &Profiles::switched);
        QSignalSpy changed(m_profiles, &Profiles::changed);
        m_profiles->switchTo(u"default"_s, QString(), true);
        settle([&] { return !m_profiles->busy() && changed.size() >= 2; });

        QVERIFY(m_pretend->heard.contains(R"("keep":true)"));
        QCOMPARE(switched.size(), 0);
    }

    void a_switch_is_applied_and_said()
    {
        serving(seating({principal(), reader()}, u"p2"_s));
        asked();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("PUT /profiles/active ")
                       ? reply(200, asJson(seating({principal(), reader()}, u"default"_s,
                                                   true, false, false, u"none"_s)))
                       : QByteArray();
        };

        QSignalSpy switched(m_profiles, &Profiles::switched);
        m_profiles->switchTo(u"default"_s, QString(), true);
        settle([&] { return !switched.isEmpty(); });

        QCOMPARE(switched.size(), 1);
        QVERIFY(m_profiles->isPrincipal());
        QCOMPARE(m_profiles->codeScope(), u"none"_s);
        QVERIFY(m_pretend->heard.contains(R"("keep":true)"));
        // No code typed, none sent: an empty one would be a wrong one.
        QVERIFY(!m_pretend->heard.contains(R"("code")"));
    }

    void asks_who_reads_only_when_it_should()
    {
        // Alone: nothing to choose between.
        serving(seating({principal()}, u"default"_s, false));
        asked();
        QVERIFY(!m_profiles->several());
        QVERIFY(!m_profiles->asksWhoReads());

        // Several, and the device does not keep one.
        serving(seating({principal(), reader()}, u"default"_s, false));
        asked();
        QVERIFY(m_profiles->asksWhoReads());

        // Several, and it keeps its own.
        serving(seating({principal(), reader()}, u"default"_s, true));
        asked();
        QVERIFY(!m_profiles->asksWhoReads());

        // Restricted and not kept: a child's tablet is not asked, and cannot change who it is.
        serving(seating({principal(), child()}, u"p3"_s, false));
        asked();
        QVERIFY(m_profiles->isRestricted());
        QVERIFY(!m_profiles->asksWhoReads());

        // Held, whatever else is true: the restricted profile it sat on is gone.
        serving(seating({principal(), reader()}, u"default"_s, true, false, true));
        asked();
        QVERIFY(m_profiles->asksWhoReads());

        // Choosing: the profile it sat on was deleted.
        serving(seating({principal(), reader()}, u"default"_s, true, true, false));
        asked();
        QVERIFY(m_profiles->asksWhoReads());
    }

    void refuses_a_profile_without_its_name_and_names_the_field()
    {
        QJsonObject nameless = reader();
        nameless.remove(u"name"_s);
        serving(seating({principal(), nameless}, u"default"_s));
        asked();

        QVERIFY(m_profiles->trouble().contains(u"name"_s));
        QVERIFY(m_profiles->trouble().contains(u"profiles[1]"_s));
        // Nothing half-read stands in the list.
        QVERIFY(m_profiles->list().isEmpty());
    }

    void refuses_a_profile_without_one_of_its_rights()
    {
        const auto read = Api::profile(
            aProfile(u"p2"_s, u"Élodie"_s, u"bleu"_s, u"reader"_s,
                     QJsonObject{{u"import"_s, true}, {u"edit"_s, false}}));
        QVERIFY(!read.ok());
        QVERIFY(read.trouble.contains(u"delete"_s));
    }

    void a_choose_a_profile_refusal_from_any_request_asks_who_reads()
    {
        serving(seating({principal(), reader()}, u"default"_s, true));
        asked();
        QVERIFY(!m_profiles->asksWhoReads());

        // Not `GET /profiles`: the shelf, a cover, a progress write — whichever the server
        // refuses first. The model never asked for anything of its own.
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("GET /profile/reading ")
                       ? reply(409, R"({"error":"choose a profile"})")
                       : QByteArray();
        };
        QSignalSpy changed(m_profiles, &Profiles::changed);
        m_server->get(u"/profile/reading"_s, this, [](const Server::Answer &) {});
        settle([&] { return m_profiles->asksWhoReads(); });

        QVERIFY(m_profiles->asksWhoReads());
        QVERIFY(!changed.isEmpty());

        // And the server's next word on who is seated is the truth again.
        serving(seating({principal(), reader()}, u"default"_s, true));
        asked();
        QVERIFY(!m_profiles->asksWhoReads());
    }

    void another_409_does_not_ask_who_reads()
    {
        serving(seating({principal(), reader()}, u"default"_s, true));
        asked();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("GET /profile/reading ")
                       ? reply(409, R"({"error":"name taken"})")
                       : QByteArray();
        };
        bool answered = false;
        m_server->get(u"/profile/reading"_s, this,
                      [&](const Server::Answer &) { answered = true; });
        settle([&] { return answered; });

        QVERIFY(answered);
        QVERIFY(!m_profiles->asksWhoReads());
    }

    void sets_a_code_and_asks_again()
    {
        serving(seating({principal()}, u"default"_s, true, false, false, u"none"_s));
        asked();
        m_pretend->heard.clear();
        int asks = 0;
        m_pretend->answerFor = [&asks](const QByteArray &request) {
            if (request.startsWith("PUT /profiles/code "))
                return reply(204, {});
            if (request.startsWith("POST /profiles/code/recovery "))
                return reply(200, R"({"recoveryCode":"K7QM-2XWD-HP9T-C4VN"})");
            if (request.startsWith("GET /profiles ")) {
                ++asks;
                return reply(200, asJson(seating({principal()}, u"default"_s, true, false,
                                                 false, u"everything"_s)));
            }
            return QByteArray();
        };

        QSignalSpy drawn(m_profiles, &Profiles::recoveryDrawn);
        m_profiles->setCode(u"abcd"_s, u"abcd"_s, u"everything"_s, QString());
        settle([&] { return m_profiles->codeScope() == u"everything"_s && !drawn.isEmpty(); });

        QCOMPARE(m_profiles->codeScope(), u"everything"_s);
        QCOMPARE(asks, 1);
        // The first code of a household owes it a way back from forgetting it, said once.
        QCOMPARE(drawn.size(), 1);
        QCOMPARE(drawn.constFirst().constFirst().toString(), u"K7QM-2XWD-HP9T-C4VN"_s);
        QVERIFY(m_pretend->heard.contains(R"("codeAgain":"abcd")"));
        QVERIFY(m_pretend->heard.contains(R"("scope":"everything")"));
    }

    /// Changing a code that already exists draws nothing: the recovery code stands for the one
    /// the household has, and a new one on every change would revoke the page it was written on.
    void changing_a_code_that_exists_draws_no_recovery_code()
    {
        serving(seating({principal()}, u"default"_s, true, false, false, u"restricted"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("PUT /profiles/code "))
                return reply(204, {});
            if (request.startsWith("GET /profiles ")) {
                return reply(200, asJson(seating({principal()}, u"default"_s, true, false,
                                                 false, u"everything"_s)));
            }
            return QByteArray();
        };

        QSignalSpy drawn(m_profiles, &Profiles::recoveryDrawn);
        m_profiles->setCode(u"abcd"_s, u"abcd"_s, u"everything"_s, QString());
        settle([&] { return m_profiles->codeScope() == u"everything"_s; });

        QCOMPARE(drawn.size(), 0);
        QVERIFY(!m_pretend->heard.contains("/profiles/code/recovery"));
    }

    void a_recovery_code_replaces_the_code_and_says_the_next()
    {
        serving(seating({principal(), child()}, u"p2"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("POST /profiles/code/recover ")
                       ? reply(200, R"({"recoveryCode":"AAAA-BBBB-CCCC-DDDD"})")
                       : QByteArray();
        };

        QSignalSpy recovered(m_profiles, &Profiles::codeRecovered);
        m_profiles->recover(u"k7qm 2xwd"_s, u"97531"_s, u"97531"_s);
        settle([&] { return !recovered.isEmpty(); });

        QCOMPARE(recovered.constFirst().constFirst().toString(), u"AAAA-BBBB-CCCC-DDDD"_s);
        QVERIFY(m_pretend->heard.contains(R"("recoveryCode":"k7qm 2xwd")"));
        QVERIFY(m_pretend->heard.contains(R"("codeAgain":"97531")"));
    }

    /// A refused recovery code is a wrong recovery code, said as one: « Ce code n’est pas le
    /// bon » would send the person looking for the code they have forgotten.
    void a_wrong_recovery_code_is_said_as_one_and_changes_nothing()
    {
        serving(seating({principal(), child()}, u"p2"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("POST /profiles/code/recover ")
                       ? reply(403, R"({"error":"code"})")
                       : QByteArray();
        };

        QSignalSpy refused(m_profiles, &Profiles::codeRefused);
        QSignalSpy recovered(m_profiles, &Profiles::codeRecovered);
        m_profiles->recover(u"nope"_s, u"97531"_s, u"97531"_s);
        settle([&] { return !refused.isEmpty(); });

        QCOMPARE(m_profiles->trouble(), Words::wrongRecovery());
        QCOMPARE(recovered.size(), 0);
        // And the next wrong code is a wrong code again.
        m_pretend->answerFor = [](const QByteArray &request) {
            return request.startsWith("PUT /profiles/active ")
                       ? reply(403, R"({"error":"code"})")
                       : QByteArray();
        };
        m_profiles->switchTo(u"default"_s, u"nope"_s, false);
        settle([&] { return refused.size() == 2; });
        QCOMPARE(m_profiles->trouble(), Words::wrongCode());
    }

    void adds_a_profile_and_sends_the_right_to_delete_as_the_contract_spells_it()
    {
        serving(seating({principal()}, u"default"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /profiles "))
                return reply(201, asJson(reader()));
            if (request.startsWith("GET /profiles "))
                return reply(200, asJson(seating({principal(), reader()}, u"default"_s)));
            return QByteArray();
        };

        m_profiles->add({{u"name"_s, u"Élodie"_s},
                         {u"color"_s, u"bleu"_s},
                         {u"kind"_s, u"reader"_s},
                         {u"rights"_s, QVariantMap{{u"import"_s, true},
                                                   {u"edit"_s, false},
                                                   {u"erase"_s, true}}}});
        settle([&] { return m_profiles->list().size() == 2; });

        QCOMPARE(m_profiles->list().size(), 2);
        QVERIFY(m_pretend->heard.contains(R"("delete":true)"));
        QVERIFY(!m_pretend->heard.contains(R"("erase")"));
    }

    void reads_the_devices_and_seats_one()
    {
        serving(seating({principal(), reader()}, u"default"_s));
        asked();
        m_pretend->heard.clear();
        const QJsonObject device{{u"id"_s, u"d1"_s}, {u"name"_s, u"Tablette"_s},
                                 {u"profile"_s, u"default"_s}, {u"kept"_s, false},
                                 {u"here"_s, true}, {u"seenAt"_s, 1760000000000.0}};
        m_pretend->answerFor = [device](const QByteArray &request) {
            if (request.startsWith("PATCH /devices/d1 "))
                return reply(200, asJson(device));
            if (request.startsWith("GET /devices "))
                return reply(200, asJson(QJsonObject{{u"devices"_s, QJsonArray{device}}}));
            return QByteArray();
        };

        m_profiles->devices();
        settle([&] { return !m_profiles->deviceList().isEmpty(); });
        QCOMPARE(m_profiles->deviceList().size(), 1);
        const QVariantMap one = m_profiles->deviceList().at(0).toMap();
        QCOMPARE(one.value(u"name"_s).toString(), u"Tablette"_s);
        QVERIFY(one.value(u"here"_s).toBool());

        m_pretend->heard.clear();
        m_profiles->placeDevice(u"d1"_s, u"p2"_s, true);
        settle([&] { return m_pretend->heard.contains("GET /devices "); });
        QVERIFY(m_pretend->heard.contains("PATCH /devices/d1 "));
        QVERIFY(m_pretend->heard.contains(R"("profile":"p2")"));
        QVERIFY(m_pretend->heard.contains(R"("kept":true)"));
    }

    /// The principal seating *this* device on another profile is a switch: the server reads
    /// as that profile from the answer on, and a shelf drawn for the old one would show what
    /// the new one may not see. Another device seated elsewhere changes nothing here.
    void seating_this_device_on_another_profile_reads_as_it_now()
    {
        serving(seating({principal(), child()}, u"default"_s));
        asked();
        const auto device = [](const QString &id, const QString &profile, bool here) {
            return QJsonObject{{u"id"_s, id}, {u"name"_s, u"Ordinateur"_s},
                               {u"profile"_s, profile}, {u"kept"_s, true}, {u"here"_s, here}};
        };
        QString seat = u"default"_s;
        m_pretend->answerFor = [&](const QByteArray &request) {
            if (request.startsWith("PATCH /devices/d2 "))
                return reply(200, asJson(device(u"d2"_s, u"p3"_s, false)));
            if (request.startsWith("PATCH /devices/d1 ")) {
                seat = u"p3"_s;
                return reply(200, asJson(device(u"d1"_s, u"p3"_s, true)));
            }
            if (request.startsWith("GET /devices "))
                return reply(200, asJson(QJsonObject{{u"devices"_s, QJsonArray{}}}));
            if (request.startsWith("GET /profiles "))
                return reply(200, asJson(seating({principal(), child()}, seat)));
            return QByteArray();
        };
        QSignalSpy switched(m_profiles, &Profiles::switched);

        m_pretend->heard.clear();
        m_profiles->placeDevice(u"d2"_s, u"p3"_s, true);
        settle([&] { return m_pretend->heard.contains("GET /devices "); });
        QTest::qWait(100);
        QVERIFY(!m_pretend->heard.contains("GET /profiles "));
        QCOMPARE(switched.size(), 0);

        m_profiles->placeDevice(u"d1"_s, u"p3"_s, true);
        settle([&] { return !switched.isEmpty(); });
        QCOMPARE(switched.size(), 1);
        QVERIFY(m_profiles->isRestricted());
    }

    void keeps_the_reading_preferences_as_they_were_sent()
    {
        serving(seating({principal()}, u"default"_s));
        asked();
        m_pretend->heard.clear();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("PUT /profile/reading "))
                return reply(204, {});
            if (request.startsWith("GET /profile/reading "))
                return reply(200, R"({"fit":"width","pages":2})");
            return QByteArray();
        };

        m_profiles->keepReading({{u"fit"_s, u"width"_s}, {u"pages"_s, 2}});
        settle([&] { return m_pretend->heard.contains("PUT /profile/reading "); });
        QVERIFY(m_pretend->heard.contains(R"("fit":"width")"));

        m_profiles->reading();
        settle([&] { return !m_profiles->readingPreferences().isEmpty(); });
        QCOMPARE(m_profiles->readingPreferences().value(u"fit"_s).toString(), u"width"_s);
        QCOMPARE(m_profiles->readingPreferences().value(u"pages"_s).toInt(), 2);
    }

    void removing_a_profile_asks_again()
    {
        serving(seating({principal(), reader()}, u"default"_s));
        asked();
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE /profiles/p2 "))
                return reply(204, {});
            if (request.startsWith("GET /profiles "))
                return reply(200, asJson(seating({principal()}, u"default"_s)));
            return QByteArray();
        };

        m_profiles->remove(u"p2"_s);
        settle([&] { return m_profiles->list().size() == 1; });
        QCOMPARE(m_profiles->list().size(), 1);
        QVERIFY(!m_profiles->several());
    }
};

QTEST_MAIN(HoldsTheProfiles)
#include "holds_the_profiles.moc"
