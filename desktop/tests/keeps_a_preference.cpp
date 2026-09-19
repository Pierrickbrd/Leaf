// The one thing Leaf writes.
//
// `Settings` is read and never written — that rule is about the deployment, and it is why
// this is a second class rather than a field on that one. How the application looks on this
// machine is the reader's own business, it is written, and nothing else in the client
// writes anything. So the whole of that behaviour is here: what a fresh install answers,
// what a choice does to the file, and what a file written by another version means.

#include "Preferences.h"

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
