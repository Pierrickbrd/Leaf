// The one thing Leaf writes.
//
// `Settings` is read and never written — that rule is about the deployment, and it is why
// this is a second class rather than a field on that one. How the application looks on this
// machine is the reader's own business, it is written, and nothing else in the client
// writes anything. So the whole of that behaviour is here: what a fresh install answers,
// what a choice does to the file, and what a file written by another version means.

#include "Preferences.h"

#include <QUrl>

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

class KeepsAPreference : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    /// Everything warns at the start, except the desktop for a download — a copy saved in
    /// two seconds wakes nobody. It is being pestered that makes somebody open that screen,
    /// not silence.
    void a_fresh_install_warns_about_everything_but_wakes_nobody_for_a_copy()
    {
        Preferences fresh;
        using enum Preferences::Warns;
        for (const Preferences::Warns which : {Imports, Scans, Downloads, Failures})
            QVERIFY(fresh.bubbles(which));
        QVERIFY(fresh.reachesTheDesktop(Imports));
        QVERIFY(fresh.reachesTheDesktop(Scans));
        QVERIFY(fresh.reachesTheDesktop(Failures));
        QVERIFY(!fresh.reachesTheDesktop(Downloads));

        // Where the desktop puts its own, which is the place one has already learned to look.
        QCOMPARE(fresh.corner(), Preferences::Corner::BottomRight);
        QVERIFY(fresh.anythingBubbles());
        QCOMPARE(fresh.warnings().size(), 4);
        for (const QVariant &one : fresh.warnings()) {
            QVERIFY(!one.toMap().value(u"label"_s).toString().isEmpty());
            QVERIFY(!one.toMap().value(u"detail"_s).toString().isEmpty());
        }
    }

    /// Written under its own name and read back. A file holding `warns/2=false` is a file
    /// nobody can read, and the order of an enum is not a promise made to a settings file.
    void what_warns_is_written_under_a_word_and_read_back()
    {
        using enum Preferences::Warns;
        {
            Preferences chosen;
            QSignalSpy moved(&chosen, &Preferences::changed);
            chosen.showBubble(Scans, false);
            chosen.reachTheDesktop(Imports, false);
            chosen.putBubbles(Preferences::Corner::TopLeft);
            QCOMPARE(moved.size(), 3);
            // Asking again for what is already so announces nothing.
            chosen.showBubble(Scans, false);
            chosen.putBubbles(Preferences::Corner::TopLeft);
            QCOMPARE(moved.size(), 3);
        }

        const Preferences again;
        QVERIFY(!again.bubbles(Scans));
        QVERIFY(again.bubbles(Imports));
        QVERIFY(!again.reachesTheDesktop(Imports));
        QCOMPARE(again.corner(), Preferences::Corner::TopLeft);
        QCOMPARE(again.cornerLabel(), u"En haut à gauche"_s);
    }

    /// The card that says where bubbles go is not drawn when nothing makes one: an empty
    /// heading is worse than an absent one.
    void nothing_bubbling_is_a_card_that_is_not_drawn()
    {
        Preferences quiet;
        using enum Preferences::Warns;
        for (const Preferences::Warns which : {Imports, Scans, Downloads, Failures})
            quiet.showBubble(which, false);

        QVERIFY(!quiet.anythingBubbles());
        quiet.showBubble(Failures, true);
        QVERIFY(quiet.anythingBubbles());
    }

    /// A word this file does not know is the bottom right — the same rule the appearance
    /// reads its own file by, and the one place a hand-edited file is answered for.
    void a_corner_this_version_does_not_know_is_the_one_the_desktop_uses()
    {
        {
            QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                           u"preferences"_s);
            file.setValue(u"corner"_s, u"sous le bureau"_s);
            file.sync();
        }
        const Preferences odd;
        QCOMPARE(odd.corner(), Preferences::Corner::BottomRight);
    }

    /// Lines are the default: they say the whole title, the pages and the state on one row,
    /// where a grid shows covers and cuts long titles.
    void the_volumes_of_a_series_are_lines_until_somebody_says_otherwise()
    {
        Preferences fresh;
        QVERIFY(!fresh.volumesAsGrid());

        QSignalSpy moved(&fresh, &Preferences::changed);
        fresh.showVolumesAsGrid(true);
        QVERIFY(fresh.volumesAsGrid());
        // Once, and not twice. It was said before the file was written and again after it,
        // which is one fact announced twice and everything bound to this object redrawn for
        // nothing.
        QCOMPARE(moved.size(), 1);

        // Asking again for what is already so announces nothing: a binding refreshed by a
        // change that did not happen is a binding refreshed for nothing.
        const int announced = moved.size();
        fresh.showVolumesAsGrid(true);
        QCOMPARE(moved.size(), announced);
    }

    /// Kept once and not per series — a reader's habit, not a state of one page. It therefore
    /// has to survive the object, which is what writing it to the file is for.
    void the_choice_survives_the_run()
    {
        {
            Preferences first;
            first.showVolumesAsGrid(true);
        }
        Preferences again;
        QVERIFY(again.volumesAsGrid());
    }

    /// Every slot starts from an empty file, because "what a fresh install answers" is one
    /// of the answers under test and a leftover from the slot before would hide it.
    void init()
    {
        QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                       u"preferences"_s);
        file.clear();
        file.sync();
    }

    void a_fresh_install_follows_the_desktop()
    {
        const Preferences fresh;
        QCOMPARE(fresh.appearance(), Preferences::Appearance::System);
    }

    /// Written at once and not on the way out: an application that is killed has still been
    /// told what somebody wanted.
    void a_choice_is_on_disk_before_the_application_closes()
    {
        Preferences chosen;
        QSignalSpy told(&chosen, &Preferences::appearanceChosen);
        chosen.chooseAppearance(Preferences::Appearance::Dark);
        QCOMPARE(told.size(), 1);
        QCOMPARE(chosen.appearance(), Preferences::Appearance::Dark);

        const QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                             u"preferences"_s);
        QCOMPARE(file.value(u"appearance"_s).toString(), u"dark"_s);
    }

    /// The pickers open where they were last used, and that survives the application.
    ///
    /// A library lives in one place and a reader imports from it over and over: opening on
    /// the home folder every time meant walking the same four levels down before every
    /// single import.
    void the_place_a_picker_was_last_used_outlives_the_run()
    {
        {
            Preferences first;
            QCOMPARE(first.lastPlace(), QUrl());
            first.rememberPlace(QUrl(u"file:///home/pierrick/Documents/Lecture"_s));
            QCOMPARE(first.lastPlace(),
                     QUrl(u"file:///home/pierrick/Documents/Lecture"_s));
        }

        Preferences later;
        QCOMPARE(later.lastPlace(), QUrl(u"file:///home/pierrick/Documents/Lecture"_s));
    }

    /// Nothing is remembered from a picker somebody closed without choosing.
    void a_place_that_says_nothing_is_not_remembered()
    {
        Preferences chosen;
        chosen.rememberPlace(QUrl(u"file:///somewhere"_s));
        chosen.rememberPlace(QUrl());
        QCOMPARE(chosen.lastPlace(), QUrl(u"file:///somewhere"_s));
    }

    /// A word and never a number. A file holding `2` is a file nobody can read, and an
    /// enumeration that gains a value in the middle would silently move everyone's choice.
    void the_file_holds_a_word_and_a_later_run_reads_it_back()
    {
        {
            Preferences chosen;
            chosen.chooseAppearance(Preferences::Appearance::Light);
        }
        const Preferences again;
        QCOMPARE(again.appearance(), Preferences::Appearance::Light);

        const QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                             u"preferences"_s);
        bool number = false;
        file.value(u"appearance"_s).toString().toInt(&number);
        QVERIFY2(!number, "the choice was written as a number");
    }

    /// A word this version does not know is the desktop's choice, which is the answer that
    /// is right whatever the word meant.
    void a_word_from_another_version_reads_as_the_desktop()
    {
        {
            QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                           u"preferences"_s);
            file.setValue(u"appearance"_s, u"solarized"_s);
            file.sync();
        }
        const Preferences puzzled;
        QCOMPARE(puzzled.appearance(), Preferences::Appearance::System);
    }

    /// Choosing what is already chosen writes nothing and tells nobody. Without this the
    /// theme was rebuilt on every click of the pill already lit.
    void returning_to_system_is_persisted_and_dark_is_restored()
    {
        Preferences chosen;
        chosen.chooseAppearance(Preferences::Appearance::Dark);
        const Preferences dark;
        QCOMPARE(dark.appearance(), Preferences::Appearance::Dark);
        QSignalSpy changed(&chosen, &Preferences::changed);
        chosen.chooseAppearance(Preferences::Appearance::System);
        QCOMPARE(changed.size(), 1);
        const Preferences system;
        QCOMPARE(system.appearance(), Preferences::Appearance::System);
        const QSettings file(QSettings::IniFormat, QSettings::UserScope, u"Leaf"_s,
                             u"preferences"_s);
        QCOMPARE(file.value(u"appearance"_s).toString(), u"system"_s);
    }

    void choosing_what_is_already_chosen_says_nothing()
    {
        Preferences chosen;
        chosen.chooseAppearance(Preferences::Appearance::Dark);
        QSignalSpy told(&chosen, &Preferences::appearanceChosen);
        QSignalSpy moved(&chosen, &Preferences::changed);
        chosen.chooseAppearance(Preferences::Appearance::Dark);
        QCOMPARE(told.size(), 0);
        QCOMPARE(moved.size(), 0);
    }

    /// The three answers a reader is offered, each with the word and the picto the screen
    /// draws. Worded in C++ like everything else they read.
    void the_three_answers_are_offered_with_their_words_and_pictos()
    {
        const Preferences offered;
        const QVariantList all = offered.appearances();
        QCOMPARE(all.size(), 3);
        QStringList icons;
        for (const QVariant &one : all) {
            const QVariantMap answer = one.toMap();
            QVERIFY(!answer.value(u"label"_s).toString().isEmpty());
            icons << answer.value(u"icon"_s).toString();
        }
        QCOMPARE(icons, QStringList({u"contrast"_s, u"light_mode"_s, u"dark_mode"_s}));
        QVERIFY(!offered.appearanceTitle().isEmpty());
    }
};

QTEST_MAIN(KeepsAPreference)
#include "keeps_a_preference.moc"
