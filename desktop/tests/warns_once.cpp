// What warns, and the restraint that makes it worth reading.

#include "Notifier.h"
#include "Preferences.h"
#include "Toasts.h"
#include "Words.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

class WarnsOnce : public QObject
{
    Q_OBJECT

    Preferences *m_preferences = nullptr;
    Toasts *m_bubbles = nullptr;
    bool m_awake = false;

    QString at(int row, const char *role) const
    {
        const QHash<int, QByteArray> named = m_bubbles->roleNames();
        for (auto it = named.constBegin(); it != named.constEnd(); ++it) {
            if (it.value() == role)
                return m_bubbles->data(m_bubbles->index(row, 0), it.key()).toString();
        }
        return {};
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // A fresh install for each of these. `Preferences` writes a real file, so a test that
        // turns a family off turns it off for every test that runs after it — which is how
        // « a scan warns » failed in a run where nothing about scans had changed.
        QSettings(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                  u"preferences"_s).clear();
        m_preferences = new Preferences;
        m_awake = false;
        // Sixty milliseconds where the application leaves a success up for five seconds: the
        // lifetime is given rather than fixed, so this settles what it does and not how long
        // a test is willing to sit there.
        m_bubbles = new Toasts(m_preferences, [this] { return m_awake; }, 60);
    }

    void cleanup()
    {
        delete m_bubbles;
        delete m_preferences;
    }

    /// Two lines and at most one thing to do about it.
    void an_import_that_landed_says_what_the_work_said()
    {
        QSignalSpy moved(m_bubbles, &Toasts::changed);
        m_bubbles->importSettled(true, u"Elfes"_s, u"29 tomes envoyés · 1 encore à venir"_s);

        QCOMPARE(m_bubbles->count(), 1);
        QCOMPARE(at(0, "headline"), u"Elfes"_s);
        QCOMPARE(at(0, "detail"), u"29 tomes envoyés · 1 encore à venir"_s);
        QCOMPARE(at(0, "label"), u"Voir"_s);
        QVERIFY(moved.size() >= 1);
    }

    void an_import_that_failed_names_it_and_offers_to_try_again()
    {
        m_bubbles->importSettled(false, u"Tome 7 d’Elfes"_s, u"le serveur a répondu 500"_s);

        QCOMPARE(at(0, "headline"), u"Tome 7 d’Elfes n’a pas pu être envoyé"_s);
        QCOMPARE(at(0, "label"), u"Réessayer"_s);
    }

    /// Five seconds for what succeeded, and until the click for what failed. A failure that
    /// fades on its own is a failure nobody read.
    void a_success_goes_by_itself_and_a_failure_waits_to_be_read()
    {
        m_bubbles->scanFinished(u"21 séries"_s);
        m_bubbles->scanFailed(u"le disque ne répond pas"_s);
        QCOMPARE(m_bubbles->count(), 2);

        QTRY_COMPARE_WITH_TIMEOUT(m_bubbles->count(), 1, 3000);
        // The one still there is the failure, and it stays there.
        QCOMPARE(at(0, "headline"), Words::scanStopped());
        QTest::qWait(200);
        QCOMPARE(m_bubbles->count(), 1);
    }

    /// Three at a time, and past that a count. Forty volumes that fail do not make forty
    /// bubbles.
    void past_three_the_rest_are_a_count()
    {
        for (int i = 0; i < 6; ++i)
            m_bubbles->scanFailed(u"raison %1"_s.arg(i));

        QCOMPARE(m_bubbles->count(), 3);
        QCOMPARE(m_bubbles->more(), 3);
        QCOMPARE(m_bubbles->moreLabel(), u"3 autres"_s);

        // The cross takes one away, and the one waiting behind takes its place.
        m_bubbles->dismiss(0);
        QCOMPARE(m_bubbles->count(), 3);
        QCOMPARE(m_bubbles->more(), 2);
        QCOMPARE(at(0, "detail"), u"raison 1"_s);
    }

    void the_one_thing_it_offers_is_done_and_the_bubble_goes()
    {
        m_bubbles->copySaved(u"/home/quelqu’un/Tome 5.cbz"_s);
        QCOMPARE(at(0, "headline"), Words::copyKept());
        // The file's own name, not the path it was written to: the client shows what was
        // chosen and not where it lives.
        QCOMPARE(at(0, "detail"), u"Tome 5.cbz"_s);

        QSignalSpy done(m_bubbles, &Toasts::acted);
        m_bubbles->act(0);
        QCOMPARE(done.size(), 1);
        QCOMPARE(done.first().at(0).value<Toasts::Offer>(), Toasts::Offer::OpenFolder);
        QCOMPARE(done.first().at(1).toString(), u"/home/quelqu’un/Tome 5.cbz"_s);
        QCOMPARE(m_bubbles->count(), 0);
    }

    /// A bubble with nothing to propose has no button, and pressing where one would be does
    /// nothing but close it — the cross is the way out, not an action of its own.
    void a_bubble_with_nothing_to_offer_announces_nothing_when_it_closes()
    {
        m_bubbles->scanFinished(u"21 séries"_s);
        QVERIFY(at(0, "label").isEmpty());

        QSignalSpy done(m_bubbles, &Toasts::acted);
        m_bubbles->act(0);
        QCOMPARE(done.size(), 0);
        QCOMPARE(m_bubbles->count(), 0);
    }

    /// Silent in the settings is silent here: the bubble is not raised at all rather than
    /// raised and hidden.
    void a_family_switched_off_raises_nothing()
    {
        m_preferences->showBubble(Preferences::Warns::Scans, false);
        m_bubbles->scanFinished(u"21 séries"_s);
        QCOMPARE(m_bubbles->count(), 0);

        // And a failure is not a scan: turning that line off does not silence the other.
        m_bubbles->scanFailed(u"le disque ne répond pas"_s);
        QCOMPARE(m_bubbles->count(), 1);
    }

    /// The desktop is the same event escalated, and only when the window is not active:
    /// getting both for one fact is the surest way to have notifications switched off.
    void the_desktop_is_reached_only_when_the_window_is_not()
    {
        QSignalSpy reached(m_bubbles, &Toasts::escalated);
        m_awake = true;
        m_bubbles->scanFinished(u"21 séries"_s);
        QCOMPARE(reached.size(), 0);

        m_awake = false;
        m_bubbles->scanFinished(u"21 séries"_s);
        QCOMPARE(reached.size(), 1);
        QCOMPARE(reached.first().at(0).toString(), Words::scanEnded());

        // And the reader who wants the system's notification and nothing else gets exactly
        // that: the desktop without the bubble is a sensible thing to ask for.
        m_preferences->showBubble(Preferences::Warns::Scans, false);
        const int drawn = m_bubbles->count();
        m_bubbles->scanFinished(u"21 séries"_s);
        QCOMPARE(reached.size(), 2);
        QCOMPARE(m_bubbles->count(), drawn);
    }

    void a_family_that_may_not_escalate_stays_in_the_window()
    {
        m_preferences->reachTheDesktop(Preferences::Warns::Downloads, false);
        QSignalSpy reached(m_bubbles, &Toasts::escalated);
        m_bubbles->copySaved(u"/tmp/Tome 5.cbz"_s);

        QCOMPARE(reached.size(), 0);
        QCOMPARE(m_bubbles->count(), 1);
    }

    /// A refusal with nothing said about it is still a refusal: the headline carries the
    /// news, and silence about a failure is the one thing this object exists to prevent.
    /// What is not said at all is not drawn at all.
    void it_says_nothing_about_nothing()
    {
        m_bubbles->commandRefused(QString());
        QCOMPARE(m_bubbles->count(), 1);
        QCOMPARE(at(0, "headline"), Words::commandRefused());
        QVERIFY(at(0, "detail").isEmpty());
        m_bubbles->dismiss(0);

        m_bubbles->say(Preferences::Warns::Failures, Toasts::Tone::Failed, QString(),
                       u"quelque chose"_s);
        QCOMPARE(m_bubbles->count(), 0);

        m_bubbles->dismiss(0);
        m_bubbles->dismiss(-1);
        m_bubbles->act(7);
        QCOMPARE(m_bubbles->count(), 0);
        QVERIFY(m_bubbles->moreLabel().isEmpty());

        // A row that is not there, and a role a view asks about by habit: both are ordinary
        // questions with an empty answer, not cases that should never happen.
        QVERIFY(!m_bubbles->data(m_bubbles->index(0, 0), Qt::DisplayRole).isValid());
        m_bubbles->scanFailed(u"une raison"_s);
        QVERIFY(!m_bubbles->data(m_bubbles->index(0, 0), Qt::DisplayRole).isValid());
        QVERIFY(!m_bubbles->data(m_bubbles->index(4, 0),
                                 qToUnderlying(Toasts::Role::Headline)).isValid());
    }

    /// The eight arguments the specification asks for, in its order.
    ///
    /// This test used to build a `Notifier` on the real session bus and call `show`. It
    /// passed, every time — and put « Scan terminé · 21 séries » on the desktop of whoever
    /// ran `ctest`. Nine of them arrived in forty minutes while this branch was being built,
    /// and they read as the server scanning on its own: the only thing that looked wrong was
    /// the one thing that was not. What reaches a daemon is not a fact a test may settle on
    /// somebody's desktop; the composition is, and it is the half that can be wrong.
    void the_desktop_half_composes_the_call_the_specification_asks_for()
    {
        const QDBusMessage call = Notifier::announcement(u"Scan terminé"_s, u"21 séries"_s);
        QCOMPARE(call.service(), u"org.freedesktop.Notifications"_s);
        QCOMPARE(call.path(), u"/org/freedesktop/Notifications"_s);
        QCOMPARE(call.member(), u"Notify"_s);

        const QVariantList said = call.arguments();
        QCOMPARE(said.size(), 8);
        QCOMPARE(said.at(0).toString(), u"Leaf"_s);
        // Nought, never one of ours: a bubble that replaced the last would lose what it said.
        QCOMPARE(said.at(1).toUInt(), 0U);
        QCOMPARE(said.at(3).toString(), u"Scan terminé"_s);
        QCOMPARE(said.at(4).toString(), u"21 séries"_s);
        // The daemon's own default. It knows what its reader asked for, and this does not.
        QCOMPARE(said.at(7).toInt(), -1);
    }

    /// And silent rather than noisy when there is nowhere to speak: a machine with no
    /// notification daemon is a machine that hears nothing, not a machine that breaks.
    void the_desktop_half_is_a_courtesy_and_never_a_failure()
    {
        Notifier bus(QDBusConnection(u"leaf-tests-speak-to-nobody"_s));
        QVERIFY(!bus.reachable());

        // Neither of these may do anything at all, and neither may go wrong.
        bus.show(u"Scan terminé"_s, u"21 séries"_s);
        bus.show(QString(), u"sans titre, donc rien"_s);
        QVERIFY(true);
    }

    void with_no_preferences_everything_warns()
    {
        Toasts loud(nullptr, [] { return true; }, 60);
        loud.scanFinished(u"21 séries"_s);

        QCOMPARE(loud.count(), 1);
        QCOMPARE(loud.corner(), std::to_underlying(Preferences::Corner::BottomRight));
    }
};

QTEST_MAIN(WarnsOnce)
#include "warns_once.moc"
