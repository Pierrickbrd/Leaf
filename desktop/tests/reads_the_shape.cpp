// What the shape of a dropped folder suggests each folder is, before anything is opened.
//
// `Dragon Ball/{Perfect, Original}` and `Terres d’Arran/{Elfes, Mages}` are the same shape on
// disk and opposite in meaning, which is why the scan stopped guessing on 21/09. These are the
// trees the suggestion has to tell apart, written by the test: the files are ten bytes that are
// no zip at all, because the shape reads names and never opens an archive.

#include "Shape.h"
#include "Words.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

class ReadsTheShape : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_folder;

    void wrote(const QString &relative, const QByteArray &bytes = QByteArray(10, 'x'))
    {
        const QString at = m_folder->filePath(relative);
        [&] { QVERIFY(QDir().mkpath(QFileInfo(at).absolutePath())); }();
        QFile file(at);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(bytes);
    }

    /// `Tome from.cbz` to `Tome to.cbz` in `folder`.
    void volumes(const QString &folder, int from, int to)
    {
        for (int number = from; number <= to; ++number)
            wrote(u"%1/Tome %2.cbz"_s.arg(folder).arg(number));
    }

    Shape::Drop dropped(const QString &relative)
    {
        return Shape::read(m_folder->filePath(relative));
    }

    static int ticks(const Shape::Suggestion &said, bool forIt)
    {
        return int(std::ranges::count_if(said.clues, [forIt](const Shape::Clue &clue) {
            return clue.forIt == forIt;
        }));
    }

    static bool saysAnything(const Shape::Suggestion &said, const QString &text)
    {
        return std::ranges::any_of(said.clues,
                                   [&text](const Shape::Clue &clue) { return clue.text == text; });
    }

    static QList<Shape::Role> rolesIn(const QList<Shape::Option> &options)
    {
        QList<Shape::Role> out;
        for (const Shape::Option &one : options)
            out << one.role;
        return out;
    }

    static Shape::Option optionOf(const QList<Shape::Option> &options, Shape::Role role)
    {
        const auto found = std::ranges::find(options, role, &Shape::Option::role);
        return found == options.end() ? Shape::Option{role, false, u"(absent)"_s} : *found;
    }

    /// The BD shelf every screen of the import was drawn on.
    void shelfOfThree()
    {
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"BD/Terres d’Arran/Mages"_s, 1, 3);
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        volumes(u"BD/Vrac"_s, 1, 9);
    }

private slots:
    void init() { m_folder = std::make_unique<QTemporaryDir>(); }

    /// Two editions of one series, and the reading that would make them two series beside it.
    /// It is « à confirmer » however strong the clues: two series taken for two editions offer
    /// each other in the edition switcher afterwards, the worse of the two mistakes.
    void dragon_ball_reads_as_one_series_in_two_editions_to_confirm()
    {
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Dragon Ball"_s);

        QCOMPARE(said.at, u"Dragon Ball"_s);
        QCOMPARE(said.readings.size(), 2);
        const Shape::Reading &first = said.readings.at(0);
        QCOMPARE(first.roles.size(), 3);
        QCOMPARE(first.roles.value(u"Dragon Ball"_s), Shape::Role::Work);
        QCOMPARE(first.roles.value(u"Dragon Ball/Perfect"_s), Shape::Role::Edition);
        QCOMPARE(first.roles.value(u"Dragon Ball/Original"_s), Shape::Role::Edition);
        QCOMPARE(first.universes, 0);
        QCOMPARE(first.works, 1);
        QCOMPARE(first.editions, 2);
        QCOMPARE(first.volumes, 84);
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
        QCOMPARE(ticks(said, true), 2);
        QCOMPARE(ticks(said, false), 1);
        QVERIFY(saysAnything(said, Words::clueEditionWords({u"Original"_s, u"Perfect"_s})));
        QVERIFY(saysAnything(said, Words::clueSameNumbers(2, 1, 42)));
        QVERIFY(saysAnything(said, Words::clueProperNames()));

        const Shape::Reading &other = said.readings.at(1);
        QCOMPARE(other.universes, 1);
        QCOMPARE(other.works, 2);
        QCOMPARE(other.editions, 0);
        QCOMPARE(other.volumes, 84);

        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).origin, Shape::Origin::Guessed);
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect"_s).role, Shape::Role::Edition);
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect/Tome 1.cbz"_s).role,
                 Shape::Role::Volume);
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect/Tome 1.cbz"_s).origin,
                 Shape::Origin::Evident);
        // The editions are part of their series' reading, not questions of their own.
        QVERIFY(!drop.suggestions.contains(u"Dragon Ball/Perfect"_s));
    }

    /// Two proper names and no edition word: two series, and the folder that holds them a
    /// universe — « probable », since nothing points the other way.
    void terres_d_arran_reads_as_a_universe_of_two_series_likely()
    {
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"BD/Terres d’Arran/Mages"_s, 1, 3);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Terres d’Arran"_s);

        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).origin, Shape::Origin::Guessed);
        QCOMPARE(drop.roles.value(u"Terres d’Arran/Elfes"_s).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(u"Terres d’Arran/Elfes"_s).origin, Shape::Origin::Evident);
        QCOMPARE(said.confidence, Shape::Confidence::Likely);
        QCOMPARE(ticks(said, false), 0);
        QVERIFY(saysAnything(said, Words::clueDistinctNames({u"Elfes"_s, u"Mages"_s})));
        QCOMPARE(said.readings.size(), 2);
        QCOMPARE(said.readings.at(0).universes, 1);
        QCOMPARE(said.readings.at(0).works, 2);
        QCOMPARE(said.readings.at(0).volumes, 7);
        QCOMPARE(said.readings.at(1).works, 1);
        QCOMPARE(said.readings.at(1).editions, 2);
    }

    /// A universe holds only series: the folder a universe sits in is a shelf, and nothing
    /// about it is left to confirm.
    void a_folder_holding_a_universe_is_a_shelf_for_certain()
    {
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"BD/Terres d’Arran/Mages"_s, 1, 3);
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        volumes(u"BD/Vrac"_s, 1, 9);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(said.confidence, Shape::Confidence::Certain);
        QVERIFY(saysAnything(said, Words::clueHoldsUniverse(u"Terres d’Arran"_s)));
        QCOMPARE(said.readings.size(), 1);
        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(u"Vrac"_s).role, Shape::Role::Work);
    }

    /// Archives sitting in a folder make it a series — the scan's own rule — and a name like
    /// « Vrac » is no title, so it asks for one rather than becoming a series called « Vrac ».
    void a_folder_of_archives_is_a_series_whatever_its_name()
    {
        volumes(u"Vrac"_s, 1, 9);

        const Shape::Drop drop = dropped(u"Vrac"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(QString()).origin, Shape::Origin::Evident);
        QVERIFY(said.titleMissing);
        QCOMPARE(said.confidence, Shape::Confidence::Certain);
        QVERIFY(saysAnything(said, Words::clueArchivesHere(1, 9)));
        QVERIFY(saysAnything(said, Words::clueNoSubfolder()));
        QVERIFY(saysAnything(said, Words::clueNameToGive(u"Vrac"_s)));
        QCOMPARE(ticks(said, false), 1);
        QCOMPARE(said.readings.constFirst().volumes, 9);
    }

    /// « Mangas » is where series are put away, not the name of a world they share.
    void a_generic_name_makes_a_shelf_not_a_universe()
    {
        volumes(u"Mangas/Bleach"_s, 1, 3);
        volumes(u"Mangas/Naruto"_s, 1, 2);

        const Shape::Drop drop = dropped(u"Mangas"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(said.confidence, Shape::Confidence::Likely);
        QVERIFY(saysAnything(said, Words::clueGenericName(u"Mangas"_s)));
        QCOMPARE(said.readings.size(), 2);
        QCOMPARE(said.readings.at(1).roles.value(QString()), Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Bleach"_s).role, Shape::Role::Work);
    }

    /// Two series numbered alike under « Mangas » are two series put away there: the same
    /// numbers read them as two editions of one work, named after the shelf.
    void a_generic_name_outweighs_the_same_numbers()
    {
        volumes(u"Mangas/Bleach"_s, 1, 3);
        volumes(u"Mangas/Naruto"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Mangas"_s);

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(drop.roles.value(u"Bleach"_s).role, Shape::Role::Work);
        QCOMPARE(Shape::cards(drop), (QStringList{u"Bleach"_s, u"Naruto"_s}));
    }

    /// Three proper names numbered alike are three series: editions of one work come in
    /// twos far more often than threes, and three trilogies were read as one work.
    void three_proper_names_outweigh_the_same_numbers()
    {
        for (const QString &name : {u"Blacksad"_s, u"Sillage"_s, u"Undertaker"_s})
            volumes(u"Trilogies/"_s + name, 1, 3);

        const Shape::Drop drop = dropped(u"Trilogies"_s);

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Universe);
        for (const QString &name : {u"Blacksad"_s, u"Sillage"_s, u"Undertaker"_s})
            QCOMPARE(drop.roles.value(name).role, Shape::Role::Work);
        QCOMPARE(drop.suggestions.value(QString()).confidence, Shape::Confidence::ToConfirm);
    }

    /// One work in three editions with the same numbers, named by their publishers, reads
    /// as three series of a universe: three proper names outweigh the same numbers. It is
    /// the less serious of the two mistakes — three series in one universe are merged in a
    /// click, editions taken for series are not seen — and it is asked about, the editions
    /// offered beside it as the other reading.
    void three_publishers_of_one_work_read_as_a_universe_to_confirm_with_editions_beside()
    {
        for (const QString &name : {u"Glénat"_s, u"Delcourt"_s, u"Kana"_s})
            volumes(u"Akira/"_s + name, 1, 6);

        const Shape::Drop drop = dropped(u"Akira"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Glénat"_s).role, Shape::Role::Work);
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
        QCOMPARE(said.readings.size(), 2);
        QCOMPARE(said.readings.at(0).roles.value(QString()), Shape::Role::Universe);
        QCOMPARE(said.readings.at(1).roles.value(QString()), Shape::Role::Work);
        QCOMPARE(said.readings.at(1).editions, 3);
    }

    /// A thousand series numbered 1 to 50 under « BD » are a library, not one work in a
    /// thousand editions: past the series a universe holds, the shelf wins whatever the numbers.
    void more_series_than_a_universe_holds_outweigh_the_same_numbers()
    {
        constexpr int Series = 1000;
        for (int one = 0; one < Series; ++one) {
            for (int number = 1; number <= 50; ++number)
                wrote(u"BD/Série %1/Tome %2.cbz"_s.arg(one).arg(number), QByteArray());
        }

        const Shape::Drop drop = dropped(u"BD"_s);

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(drop.roles.value(u"Série 0"_s).role, Shape::Role::Work);
        QCOMPARE(Shape::cards(drop).size(), Series);
    }

    /// Each card of a drop of a thousand universes is given its tree and its roles in a few
    /// looks at what it holds, and not at the whole drop: a walk of every role per card read
    /// the 53 001 roles a thousand times, 9,6 s at the drop and again at every role changed.
    void each_card_is_given_its_roles_without_walking_the_whole_drop()
    {
        constexpr int Universes = 1000;
        constexpr int Volumes = 25;
        Shape::Drop drop;
        drop.roles.insert(QString(), {Shape::Role::Shelf, Shape::Origin::Guessed});
        qint64 held = 1;
        for (int one = 0; one < Universes; ++one) {
            Manifest::Node universe;
            universe.at = u"Monde %1"_s.arg(one);
            universe.name = universe.at;
            drop.roles.insert(universe.at, {Shape::Role::Universe, Shape::Origin::Guessed});
            for (const QString &side : {u"Est"_s, u"Ouest"_s}) {
                Manifest::Node series;
                series.at = universe.at + u'/' + side;
                series.name = side;
                drop.roles.insert(series.at, {Shape::Role::Work, Shape::Origin::Evident});
                for (int number = 1; number <= Volumes; ++number) {
                    Manifest::Entry volume;
                    volume.path = series.at + u"/Tome %1.cbz"_s.arg(number);
                    drop.roles.insert(volume.path, {Shape::Role::Volume, Shape::Origin::Evident});
                    series.files << volume;
                }
                held += 1 + Volumes;
                universe.children << series;
            }
            held += 1;
            drop.whole.children << universe;
        }
        QCOMPARE(drop.roles.size(), Universes * (1 + 2 * (1 + Volumes)) + 1);

        const QStringList cards = Shape::cards(drop);
        QCOMPARE(cards.size(), Universes);
        const Shape::Cost before = Shape::cost();
        qint64 roles = 0;
        for (const QString &card : cards) {
            roles += Shape::rolesOf(drop, card).size();
            QCOMPARE(Shape::cardNode(drop, card).children.size(), 2);
        }
        const qint64 looks = Shape::cost().looks - before.looks;
        QCOMPARE(roles, qint64(Universes) * (1 + 2 * (1 + Volumes)));
        // The drop's folders are indexed once, and each card then looks at what it holds and
        // at the few steps down to it: a bounded number of looks per thing in the drop.
        QVERIFY2(looks <= 4 * (held + Universes),
                 qPrintable(u"%1 looks for %2 folders and archives"_s.arg(looks).arg(held)));
    }

    /// Nine series under one name is a publisher, a collection or a whole library more often
    /// than a universe.
    void more_series_than_a_universe_holds_make_a_shelf()
    {
        const QStringList names{u"Akira"_s, u"Blame"_s, u"Claymore"_s, u"Dorohedoro"_s,
                                u"Eden"_s, u"Freesia"_s, u"Gantz"_s, u"Hellsing"_s,
                                u"Ikigami"_s};
        QCOMPARE(names.size(), Shape::MostUniverseSeries + 1);
        // Each its own length: nine series all numbered 1 to 2 would be the same numbers in
        // every folder, which is what editions look like.
        for (int i = 0; i < names.size(); ++i)
            volumes(u"Glénat/"_s + names.at(i), 1, i + 1);

        const Shape::Drop drop = dropped(u"Glénat"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QVERIFY(saysAnything(said, Words::clueManySeries(9)));
        QCOMPARE(said.readings.at(1).roles.value(QString()), Shape::Role::Universe);
    }

    /// Dropping the folder that holds every series already made one card named after it: a
    /// universe that is the dropped folder itself is always asked about.
    void the_dropped_folder_itself_is_never_a_likely_universe()
    {
        volumes(u"Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"Terres d’Arran/Mages"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Terres d’Arran"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Universe);
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
        QVERIFY(saysAnything(said, Words::clueDropped()));
    }

    /// Without case, without accents, and as a whole word: « Perfectionniste » is somebody's
    /// series, not a Perfect edition.
    void edition_words_are_read_without_case_or_accents_and_as_whole_words()
    {
        QVERIFY(Shape::isEditionWord(u"INTÉGRALE"_s));
        QVERIFY(Shape::isEditionWord(u"integrale"_s));
        QVERIFY(Shape::isEditionWord(u"Perfect Edition"_s));
        QVERIFY(Shape::isEditionWord(u"Aizoban"_s));
        QVERIFY(!Shape::isEditionWord(u"Perfectionniste"_s));
        QVERIFY(!Shape::isEditionWord(u"Originaux"_s));
        QVERIFY(Shape::isGenericName(u"Nouveau dossier (2)"_s));
        QVERIFY(Shape::isGenericName(u"À TRIER"_s));
        QVERIFY(!Shape::isGenericName(u"BD de papa"_s));
    }

    /// 1 to 40 inside 1 to 42 is what an edition with two volumes missing looks like, and what
    /// two series both counting from 1 look like too: it says neither. Counted as « two
    /// series », it tied the edition words and turned Dragon Ball into a universe.
    void numbers_that_only_overlap_say_nothing()
    {
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 40);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Dragon Ball"_s);

        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).role, Shape::Role::Work);
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
        QVERIFY(!saysAnything(said, Words::clueSameNumbers(2, 1, 42)));
        QVERIFY(!saysAnything(said, Words::clueSameNumbers(2, 1, 40)));
        QVERIFY(!saysAnything(said, Words::clueOtherNumbers({42, 40})));
        QVERIFY(saysAnything(said, Words::clueEditionWords({u"Original"_s, u"Perfect"_s})));
    }

    /// A folder that says what it is is not asked — and neither are the folders whose role
    /// follows from it.
    void a_declared_folder_is_never_suggested_anything()
    {
        wrote(u"Dragon Ball/work.json"_s, "{\"leaf\":1}");
        volumes(u"Dragon Ball/Perfect"_s, 1, 3);
        volumes(u"Dragon Ball/Original"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Dragon Ball"_s);

        QVERIFY(drop.suggestions.isEmpty());
        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(QString()).origin, Shape::Origin::Said);
        QCOMPARE(drop.roles.value(u"Perfect"_s).role, Shape::Role::Edition);
    }

    /// No universe inside a universe: under one that declares itself, a folder of folders can
    /// only be a series, and the reading that would make it a universe is not offered.
    void inside_a_declared_universe_a_folder_of_folders_can_only_be_a_series()
    {
        wrote(u"Monde d’Aquilon/universe.json"_s, "{\"leaf\":1}");
        volumes(u"Monde d’Aquilon/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"Monde d’Aquilon/Terres d’Arran/Mages"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Monde d’Aquilon"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Terres d’Arran"_s);

        QCOMPARE(drop.roles.value(QString()).origin, Shape::Origin::Said);
        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(u"Terres d’Arran/Elfes"_s).role, Shape::Role::Edition);
        QCOMPARE(drop.roles.value(u"Terres d’Arran/Mages"_s).role, Shape::Role::Edition);
        QVERIFY(!said.readings.isEmpty());
        for (const Shape::Reading &reading : said.readings)
            QCOMPARE(reading.universes, 0);
        QVERIFY(saysAnything(said, Words::clueNoUniverseInUniverse()));
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
    }

    /// An archive at a universe's root without `oneshot.json` is one the scan reads nobody
    /// into: it has no role until somebody gives it one.
    void archives_at_a_universe_root_have_no_role_until_given_one()
    {
        wrote(u"Monde d’Aquilon/universe.json"_s, "{\"leaf\":1}");
        wrote(u"Monde d’Aquilon/Le Combat.cbz"_s);
        volumes(u"Monde d’Aquilon/Elfes"_s, 1, 2);

        const Shape::Drop drop = dropped(u"Monde d’Aquilon"_s);

        QCOMPARE(drop.roles.value(u"Le Combat.cbz"_s).role, Shape::Role::Unread);
        QCOMPARE(drop.roles.value(u"Le Combat.cbz"_s).origin, Shape::Origin::Guessed);
        QCOMPARE(drop.roles.value(u"Elfes/Tome 1.cbz"_s).role, Shape::Role::Volume);
        QCOMPARE(drop.roles.value(u"Elfes"_s).role, Shape::Role::Work);
    }

    /// The model goes universe, series, edition, files, and stops there: an edition holds
    /// archives and nothing else. `Bonus/Extra` under a series came back as an edition inside
    /// an edition, and the folder over it as a universe one level deeper than one can sit.
    void an_edition_never_contains_an_edition_and_a_universe_never_sits_too_deep()
    {
        wrote(u"X/Naruto/Tome 1.cbz"_s);
        wrote(u"X/Naruto/Bonus/Extra/Tome 2.cbz"_s);

        const Shape::Drop drop = dropped(u"X"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(said.confidence, Shape::Confidence::Certain);
        QVERIFY(saysAnything(said, Words::clueTooDeep()));
        QCOMPARE(drop.roles.value(u"Naruto"_s).role, Shape::Role::Work);
        const auto noEditionInAnEdition = [&drop](const auto &self,
                                                  const Manifest::Node &node) -> bool {
            const bool edition = drop.roles.value(node.at).role == Shape::Role::Edition;
            return std::ranges::all_of(node.children, [&](const Manifest::Node &child) {
                return !(edition && drop.roles.value(child.at).role == Shape::Role::Edition)
                       && self(self, child);
            });
        };
        QVERIFY(noEditionInAnEdition(noEditionInAnEdition, drop.whole));
        QVERIFY(drop.roles.value(u"Naruto/Bonus"_s).role != Shape::Role::Edition);
        QVERIFY(drop.roles.value(u"Naruto/Bonus/Extra"_s).role != Shape::Role::Edition);
        // What the scan will not read is said as such, never filed as a volume.
        QCOMPARE(drop.roles.value(u"Naruto/Bonus/Extra/Tome 2.cbz"_s).role, Shape::Role::Unread);
    }

    /// A universe of series that are themselves only guessed is no surer than they are, and
    /// with no clue of its own it is asked about: « probable » with an empty « Pourquoi » is
    /// a confidence nothing earned.
    void a_universe_is_never_surer_than_the_series_it_holds()
    {
        volumes(u"BD/Toriyama/Dragon Ball/Perfect"_s, 1, 3);
        volumes(u"BD/Toriyama/Dragon Ball/Original"_s, 1, 3);

        const Shape::Drop drop = dropped(u"BD"_s);

        QCOMPARE(drop.roles.value(u"Toriyama"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.suggestions.value(u"Toriyama"_s).confidence,
                 Shape::Confidence::ToConfirm);
    }

    /// Two publishers of one series, one two volumes short: the numbers say neither, so the
    /// two names decide — two series, « probable ». Pinned as it is, on purpose: the frame
    /// shows the other reading one click away.
    void numbers_that_only_overlap_leave_the_names_to_decide()
    {
        volumes(u"BD/Naruto/Kana"_s, 1, 42);
        volumes(u"BD/Naruto/Glénat"_s, 1, 40);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Naruto"_s);

        QCOMPARE(drop.roles.value(u"Naruto"_s).role, Shape::Role::Universe);
        QCOMPARE(said.confidence, Shape::Confidence::Likely);
        QVERIFY(!saysAnything(said, Words::clueOtherNumbers({40, 42})));
    }

    /// The same numbers outweigh two proper names, and the edition reading they make is asked
    /// about like every edition reading.
    void the_same_numbers_outweigh_two_proper_names_and_are_asked_about()
    {
        volumes(u"BD/Arran/Elfes"_s, 1, 3);
        volumes(u"BD/Arran/Mages"_s, 1, 3);

        const Shape::Drop drop = dropped(u"BD"_s);
        const Shape::Suggestion said = drop.suggestions.value(u"Arran"_s);

        QCOMPARE(drop.roles.value(u"Arran"_s).role, Shape::Role::Work);
        QCOMPARE(said.readings.constFirst().editions, 2);
        QCOMPARE(said.confidence, Shape::Confidence::ToConfirm);
    }

    /// « Intégrale » names the presentation, not the work: a series called that is one nobody
    /// finds again. « Dragon Ball Perfect » is a title with an edition word in it, and keeps it.
    void a_folder_named_only_by_its_edition_asks_for_a_title()
    {
        volumes(u"Intégrale"_s, 1, 3);
        volumes(u"Dragon Ball Perfect"_s, 1, 3);

        const Shape::Suggestion bare = dropped(u"Intégrale"_s).suggestions.value(QString());
        QVERIFY(bare.titleMissing);
        QVERIFY(saysAnything(bare, Words::clueNameToGive(u"Intégrale"_s)));
        QVERIFY(!dropped(u"Dragon Ball Perfect"_s).suggestions.value(QString()).titleMissing);
    }

    /// The rail shows the roles as the folder lands: opening fifty-seven thousand archives
    /// to say so is the wait the instant tree was built to avoid.
    void nothing_opens_an_archive_to_suggest()
    {
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 2);
        volumes(u"BD/Vrac"_s, 1, 2);

        const Shape::Drop drop = dropped(u"BD"_s);

        QCOMPARE(drop.token, quint64(0));
        QCOMPARE(drop.path, m_folder->filePath(u"BD"_s));
        QCOMPARE(drop.whole.volumes(), 4);
        // Four folders — the shelf, the universe, its series, Vrac — and four archives.
        QCOMPARE(drop.roles.size(), 4 + 4);
        const auto opensNothing = [](const auto &self, const Manifest::Node &node) -> bool {
            for (const Manifest::Entry &file : node.files) {
                if (!file.members.entry.opened() || !file.members.comicInfo.opened()
                    || !file.members.oneShot.opened() || !file.members.entry.sidecar.isEmpty())
                    return false;
            }
            return std::ranges::all_of(node.children, [&self](const Manifest::Node &child) {
                return self(self, child);
            });
        };
        QVERIFY(opensNothing(opensNothing, drop.whole));
    }

    /// The menu on « Vrac », a folder of scans: a series is all it can be, and every other
    /// role says why not. A role greyed with no reason reads as a menu that is broken.
    void the_role_menu_greys_what_the_model_forbids_and_says_why()
    {
        volumes(u"Vrac"_s, 1, 9);

        const QList<Shape::Option> options = Shape::options(dropped(u"Vrac"_s), QString());

        QCOMPARE(rolesIn(options), (QList<Shape::Role>{Shape::Role::Work, Shape::Role::Edition,
                                                      Shape::Role::Universe, Shape::Role::Shelf,
                                                      Shape::Role::Book}));
        QVERIFY(optionOf(options, Shape::Role::Work).possible);
        QCOMPARE(optionOf(options, Shape::Role::Work).detail,
                 Words::roleDetail(Shape::Role::Work));
        QVERIFY(!optionOf(options, Shape::Role::Universe).possible);
        QCOMPARE(optionOf(options, Shape::Role::Universe).detail,
                 Words::impossibleNoSubfolder());
        QVERIFY(!optionOf(options, Shape::Role::Shelf).possible);
        QCOMPARE(optionOf(options, Shape::Role::Shelf).detail, Words::impossibleHoldsFiles());
        QVERIFY(!optionOf(options, Shape::Role::Edition).possible);
        QCOMPARE(optionOf(options, Shape::Role::Edition).detail,
                 Words::impossibleEditionAlone());
    }

    /// Changing a declared role would mean taking a sidecar out of the library, which the
    /// import cannot do — and a second sidecar beside the first is read after it.
    void a_declared_folder_offers_only_what_it_declares()
    {
        wrote(u"Dragon Ball/work.json"_s, "{\"leaf\":1}");
        volumes(u"Dragon Ball/Perfect"_s, 1, 3);
        volumes(u"Dragon Ball/Original"_s, 1, 3);

        Shape::Drop drop = dropped(u"Dragon Ball"_s);
        const QList<Shape::Option> options = Shape::options(drop, QString());

        QCOMPARE(options.size(), 5);
        for (const Shape::Option &one : options) {
            if (one.role == Shape::Role::Work) {
                QVERIFY(one.possible);
                QCOMPARE(one.detail, Words::roleDetail(Shape::Role::Work));
            } else {
                QVERIFY(!one.possible);
                QCOMPARE(one.detail, Words::saidBy(u"work.json"_s));
            }
        }
        QVERIFY(!Shape::apply(drop, QString(), Shape::Role::Universe));
        QCOMPARE(drop.roles.value(QString()).origin, Shape::Origin::Said);
    }

    /// The shelf holding Terres d’Arran cannot be a universe while Terres d’Arran is one, and
    /// can as soon as Terres d’Arran is something else.
    void no_universe_inside_a_universe()
    {
        shelfOfThree();
        Shape::Drop drop = dropped(u"BD"_s);

        const Shape::Option before = optionOf(Shape::options(drop, QString()),
                                              Shape::Role::Universe);
        QVERIFY(!before.possible);
        QCOMPARE(before.detail, Words::impossibleUniverseInUniverse());

        QVERIFY(Shape::apply(drop, u"Terres d’Arran"_s, Shape::Role::Work));
        QVERIFY(optionOf(Shape::options(drop, QString()), Shape::Role::Universe).possible);
    }

    /// An archive at a universe's root can be a book or nothing; filing it in a series moves
    /// it, which this step does not do. A folder is never a book.
    void an_archive_is_never_a_folder_and_a_folder_never_a_book()
    {
        wrote(u"Monde d’Aquilon/universe.json"_s, "{\"leaf\":1}");
        wrote(u"Monde d’Aquilon/Le Combat.cbz"_s);
        volumes(u"Monde d’Aquilon/Elfes"_s, 1, 2);
        Shape::Drop drop = dropped(u"Monde d’Aquilon"_s);

        const QList<Shape::Option> atRoot = Shape::options(drop, u"Le Combat.cbz"_s);
        QCOMPARE(rolesIn(atRoot),
                 (QList<Shape::Role>{Shape::Role::Book, Shape::Role::Volume, Shape::Role::Unread,
                                     Shape::Role::Work, Shape::Role::Universe,
                                     Shape::Role::Shelf, Shape::Role::Edition}));
        QVERIFY(optionOf(atRoot, Shape::Role::Book).possible);
        QCOMPARE(optionOf(atRoot, Shape::Role::Book).detail, Words::roleDetail(Shape::Role::Book));
        QVERIFY(!optionOf(atRoot, Shape::Role::Volume).possible);
        QCOMPARE(optionOf(atRoot, Shape::Role::Volume).detail, Words::impossibleFilingMoves());
        QVERIFY(optionOf(atRoot, Shape::Role::Unread).possible);
        for (const Shape::Role folder : {Shape::Role::Work, Shape::Role::Universe,
                                         Shape::Role::Shelf, Shape::Role::Edition}) {
            QVERIFY(!optionOf(atRoot, folder).possible);
            QCOMPARE(optionOf(atRoot, folder).detail, Words::impossibleNotAFolder());
        }

        const QList<Shape::Option> inSeries = Shape::options(drop, u"Elfes/Tome 1.cbz"_s);
        QCOMPARE(rolesIn(inSeries).mid(0, 2),
                 (QList<Shape::Role>{Shape::Role::Volume, Shape::Role::Book}));
        QVERIFY(!rolesIn(inSeries).contains(Shape::Role::Unread));
        QVERIFY(optionOf(inSeries, Shape::Role::Volume).possible);

        const Shape::Option book = optionOf(Shape::options(drop, u"Elfes"_s), Shape::Role::Book);
        QVERIFY(!book.possible);
        QCOMPARE(book.detail, Words::impossibleBookIsAnArchive());

        QVERIFY(Shape::apply(drop, u"Le Combat.cbz"_s, Shape::Role::Book));
        QCOMPARE(drop.roles.value(u"Le Combat.cbz"_s).role, Shape::Role::Book);
        QCOMPARE(drop.roles.value(u"Le Combat.cbz"_s).origin, Shape::Origin::Chosen);
        QVERIFY(!Shape::apply(drop, u"Elfes/Tome 1.cbz"_s, Shape::Role::Unread));
    }

    /// Dragon Ball made a universe: its two editions are read again as the two series a
    /// universe holds — by the rule, not by a click, so still a guess.
    void choosing_a_universe_rereads_its_editions_as_series()
    {
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        Shape::Drop drop = dropped(u"BD"_s);

        QVERIFY(Shape::apply(drop, u"Dragon Ball"_s, Shape::Role::Universe));

        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Dragon Ball"_s).origin, Shape::Origin::Chosen);
        for (const QString &edition : {u"Dragon Ball/Perfect"_s, u"Dragon Ball/Original"_s}) {
            QCOMPARE(drop.roles.value(edition).role, Shape::Role::Work);
            QCOMPARE(drop.roles.value(edition).origin, Shape::Origin::Guessed);
        }
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect/Tome 1.cbz"_s).role, Shape::Role::Volume);
    }

    /// « L’autre lecture » is one click for the whole folder, and lands where choosing its
    /// role by hand lands.
    void choosing_the_other_reading_takes_all_its_roles()
    {
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        Shape::Drop byHand = dropped(u"BD"_s);
        Shape::Drop byReading = byHand;

        QVERIFY(Shape::apply(byHand, u"Dragon Ball"_s, Shape::Role::Universe));
        QVERIFY(Shape::applyReading(byReading, u"Dragon Ball"_s, 1));

        QCOMPARE(byReading.roles.size(), byHand.roles.size());
        for (auto it = byHand.roles.cbegin(); it != byHand.roles.cend(); ++it) {
            QCOMPARE(byReading.roles.value(it.key()).role, it->role);
            QCOMPARE(byReading.roles.value(it.key()).origin, it->origin);
        }
        QVERIFY(!Shape::applyReading(byReading, u"Dragon Ball"_s, 2));
        // A folder the drop holds, but whose role follows from its series: it has no reading.
        QVERIFY(byReading.roles.contains(u"Dragon Ball/Perfect"_s));
        QVERIFY(!Shape::applyReading(byReading, u"Dragon Ball/Perfect"_s, 0));
    }

    /// A role somebody chose below is theirs: a change above keeps it while it still fits,
    /// and only reads it again when the model no longer has room for it.
    void a_chosen_role_below_survives_a_change_above_when_still_possible()
    {
        volumes(u"BD/Mangas/Bleach"_s, 1, 3);
        volumes(u"BD/Mangas/Naruto"_s, 1, 2);
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"BD/Terres d’Arran/Mages"_s, 1, 3);
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        Shape::Drop drop = dropped(u"BD"_s);
        QCOMPARE(drop.roles.value(u"Mangas"_s).role, Shape::Role::Shelf);

        QVERIFY(Shape::apply(drop, u"Mangas"_s, Shape::Role::Universe));
        QVERIFY(Shape::apply(drop, QString(), Shape::Role::Shelf));
        QCOMPARE(drop.roles.value(u"Mangas"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Mangas"_s).origin, Shape::Origin::Chosen);

        QVERIFY(Shape::apply(drop, u"Dragon Ball"_s, Shape::Role::Universe));
        QVERIFY(Shape::apply(drop, u"Dragon Ball/Perfect"_s, Shape::Role::Work));
        QVERIFY(Shape::apply(drop, u"Dragon Ball"_s, Shape::Role::Work));
        // A series inside a series has no room in the model: read again, as an edition.
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect"_s).role, Shape::Role::Edition);
        QCOMPARE(drop.roles.value(u"Dragon Ball/Perfect"_s).origin, Shape::Origin::Guessed);
    }

    /// A card is what `Imports::offer` announces: each universe and each series that nothing
    /// above holds, in the order the walk meets them. The shelf is no card.
    void the_cards_are_the_universes_and_series_under_shelves()
    {
        shelfOfThree();

        QCOMPARE(Shape::cards(dropped(u"BD"_s)),
                 (QStringList{u"Dragon Ball"_s, u"Terres d’Arran"_s, u"Vrac"_s}));
    }

    /// « Mangas » made a universe: Bleach and Naruto stop being two cards and become the two
    /// series of one.
    void a_shelf_turned_universe_becomes_one_card()
    {
        volumes(u"Mangas/Bleach"_s, 1, 3);
        volumes(u"Mangas/Naruto"_s, 1, 2);
        Shape::Drop drop = dropped(u"Mangas"_s);
        QCOMPARE(Shape::cards(drop), (QStringList{u"Bleach"_s, u"Naruto"_s}));

        QVERIFY(Shape::apply(drop, QString(), Shape::Role::Universe));

        QCOMPARE(Shape::cards(drop), QStringList{QString()});
    }

    /// Where every folder says what it is, the roles change nothing about the cards: the same
    /// ones `Manifest::found` has always given, in its order — « Rangement » included, a
    /// folder no sidecar can declare and whose series all declare themselves.
    void a_fully_declared_drop_gives_the_cards_found_gives()
    {
        wrote(u"Bibliothèque/Terres d’Arran/universe.json"_s, "{\"leaf\":1}");
        wrote(u"Bibliothèque/Terres d’Arran/Elfes/work.json"_s, "{\"leaf\":1}");
        volumes(u"Bibliothèque/Terres d’Arran/Elfes"_s, 1, 2);
        wrote(u"Bibliothèque/Dragon Ball/work.json"_s, "{\"leaf\":1}");
        wrote(u"Bibliothèque/Dragon Ball/Perfect/edition.json"_s, "{\"leaf\":1}");
        volumes(u"Bibliothèque/Dragon Ball/Perfect"_s, 1, 2);
        wrote(u"Bibliothèque/Rangement/Naruto/work.json"_s, "{\"leaf\":1}");
        volumes(u"Bibliothèque/Rangement/Naruto"_s, 1, 2);

        QStringList found;
        for (const Manifest::Node &card : Manifest::found(m_folder->filePath(u"Bibliothèque"_s)))
            found << card.at;
        QCOMPARE(found.size(), 3);

        QCOMPARE(Shape::cards(dropped(u"Bibliothèque"_s)), found);
    }

    /// A card's tree is rooted at itself, as `Imports::offer` roots it, and its levels are
    /// the ones its roles say — not the `Work` an undeclared folder falls back to.
    void a_card_node_carries_the_levels_its_roles_say()
    {
        volumes(u"BD/Dragon Ball/Perfect"_s, 1, 42);
        volumes(u"BD/Dragon Ball/Original"_s, 1, 42);
        Shape::Drop drop = dropped(u"BD"_s);

        const Manifest::Node card = Shape::cardNode(drop, u"Dragon Ball"_s);
        QCOMPARE(card.at, QString());
        QCOMPARE(card.level, Manifest::Level::Work);
        QCOMPARE(card.children.size(), 2);
        for (const Manifest::Node &edition : card.children) {
            QCOMPARE(edition.level, Manifest::Level::Edition);
            QVERIFY(edition.at == u"Perfect"_s || edition.at == u"Original"_s);
            QVERIFY(edition.files.constFirst().path.startsWith(edition.at + u"/Tome "_s));
        }

        const Shape::Roles roles = Shape::rolesOf(drop, u"Dragon Ball"_s);
        QCOMPARE(roles.size(), 1 + 2 + 84);
        QCOMPARE(roles.value(QString()).role, Shape::Role::Work);
        QCOMPARE(roles.value(u"Perfect"_s).role, Shape::Role::Edition);
        QCOMPARE(roles.value(u"Perfect/Tome 1.cbz"_s).role, Shape::Role::Volume);

        QVERIFY(Shape::apply(drop, u"Dragon Ball"_s, Shape::Role::Universe));
        const Manifest::Node universe = Shape::cardNode(drop, u"Dragon Ball"_s);
        QCOMPARE(universe.level, Manifest::Level::Universe);
        QCOMPARE(universe.children.constFirst().level, Manifest::Level::Work);
    }

    /// A refused role leaves the drop exactly as it was: half a change is a tree nobody chose.
    void an_impossible_role_changes_nothing()
    {
        volumes(u"Vrac"_s, 1, 9);
        Shape::Drop drop = dropped(u"Vrac"_s);
        const Shape::Roles before = drop.roles;

        QVERIFY(!Shape::apply(drop, QString(), Shape::Role::Universe));
        QVERIFY(!Shape::apply(drop, QString(), Shape::Role::Volume));
        QVERIFY(!Shape::apply(drop, u"Nulle part"_s, Shape::Role::Work));

        QCOMPARE(drop.roles.size(), before.size());
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            QCOMPARE(drop.roles.value(it.key()).role, it->role);
            QCOMPARE(drop.roles.value(it.key()).origin, it->origin);
        }
    }

    /// Under a series, a folder with no archive of its own is nothing the scan reads, and
    /// neither is what it holds: it stays the shelf it was given, and its archives unread.
    void what_sits_deeper_than_the_model_can_only_stay_unread()
    {
        wrote(u"X/Naruto/Tome 1.cbz"_s);
        wrote(u"X/Naruto/Bonus/Extra/Tome 2.cbz"_s);
        const Shape::Drop drop = dropped(u"X"_s);

        const QList<Shape::Option> folder = Shape::options(drop, u"Naruto/Bonus"_s);
        QVERIFY(optionOf(folder, Shape::Role::Shelf).possible);
        for (const Shape::Role other :
             {Shape::Role::Work, Shape::Role::Edition, Shape::Role::Universe}) {
            QVERIFY(!optionOf(folder, other).possible);
            QCOMPARE(optionOf(folder, other).detail, Words::impossibleBeyondTheModel());
        }

        const QList<Shape::Option> file =
            Shape::options(drop, u"Naruto/Bonus/Extra/Tome 2.cbz"_s);
        QVERIFY(optionOf(file, Shape::Role::Unread).possible);
        for (const Shape::Role other : {Shape::Role::Volume, Shape::Role::Book}) {
            QVERIFY(!optionOf(file, other).possible);
            QCOMPARE(optionOf(file, other).detail, Words::impossibleBeyondTheModel());
        }
    }

    /// The scan walks a folder of declared series through and records nothing: guessed a
    /// universe instead, an untouched library would have had a `universe.json` written into
    /// it that nobody asked for.
    void a_folder_of_declared_series_is_a_shelf_not_a_guessed_universe()
    {
        wrote(u"Rangement/Naruto/work.json"_s, "{\"leaf\":1}");
        volumes(u"Rangement/Naruto"_s, 1, 2);
        wrote(u"Rangement/Bleach/work.json"_s, "{\"leaf\":1}");
        volumes(u"Rangement/Bleach"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Rangement"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Shelf);
        QCOMPARE(said.confidence, Shape::Confidence::Certain);
        QVERIFY(saysAnything(said, Words::clueAllDeclared()));
        QCOMPARE(Shape::cards(drop), (QStringList{u"Bleach"_s, u"Naruto"_s}));
    }

    /// One series declared and one not is a folder somebody began to describe: the shape
    /// still reads the universe they share.
    void a_folder_of_declared_and_undeclared_series_is_still_guessed_a_universe()
    {
        wrote(u"BD/Terres d’Arran/Elfes/work.json"_s, "{\"leaf\":1}");
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 4);
        volumes(u"BD/Terres d’Arran/Mages"_s, 1, 3);

        const Shape::Drop drop = dropped(u"BD"_s);

        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).role, Shape::Role::Universe);
        QCOMPARE(drop.roles.value(u"Terres d’Arran"_s).origin, Shape::Origin::Guessed);
    }

    /// Under a universe, a folder whose nearest archive is further down than a series and its
    /// editions holds nothing the scan reads: read as a series, it had a role its own menu
    /// greyed. It is out of the model, and so is everything in it.
    void a_folder_under_a_universe_with_every_archive_too_deep_is_out_of_the_model()
    {
        wrote(u"Monde d’Aquilon/universe.json"_s, "{\"leaf\":1}");
        volumes(u"Monde d’Aquilon/Elfes"_s, 1, 2);
        wrote(u"Monde d’Aquilon/Archives/2019/Scans/Tome 1.cbz"_s);
        Shape::Drop drop = dropped(u"Monde d’Aquilon"_s);

        for (const QString &folder : {u"Archives"_s, u"Archives/2019"_s,
                                      u"Archives/2019/Scans"_s})
            QCOMPARE(drop.roles.value(folder).role, Shape::Role::Shelf);
        QCOMPARE(drop.roles.value(u"Archives/2019/Scans/Tome 1.cbz"_s).role,
                 Shape::Role::Unread);
        const QList<Shape::Option> menu = Shape::options(drop, u"Archives"_s);
        QVERIFY(optionOf(menu, Shape::Role::Shelf).possible);
        QCOMPARE(optionOf(menu, Shape::Role::Work).detail, Words::impossibleBeyondTheModel());

        // Read again the same way when the universe is chosen rather than declared.
        wrote(u"Arran/Elfes/Tome 1.cbz"_s);
        // Names that guess nothing below: a folder guessed a universe down there would grey
        // the universe above, and rightly.
        wrote(u"Arran/Archives/Scans/Vieux/Tome 1.cbz"_s);
        Shape::Drop chosen = dropped(u"Arran"_s);
        QVERIFY(Shape::apply(chosen, QString(), Shape::Role::Universe));
        QCOMPARE(chosen.roles.value(u"Archives"_s).role, Shape::Role::Shelf);
        QCOMPARE(chosen.roles.value(u"Archives/Scans/Vieux/Tome 1.cbz"_s).role,
                 Shape::Role::Unread);
    }

    /// An edition somebody chose needs a series over it: the folder above, read again under a
    /// shelf, was read by the disk alone and lost the series that edition was chosen in.
    void a_chosen_edition_keeps_its_series_when_the_folder_above_is_read_again()
    {
        volumes(u"X/Mangas/Elfes"_s, 1, 4);
        volumes(u"X/Mangas/Mages"_s, 1, 3);
        Shape::Drop drop = dropped(u"X"_s);

        QVERIFY(Shape::apply(drop, QString(), Shape::Role::Universe));
        QCOMPARE(drop.roles.value(u"Mangas"_s).role, Shape::Role::Work);
        QVERIFY(Shape::apply(drop, u"Mangas/Elfes"_s, Shape::Role::Edition));
        QVERIFY(Shape::apply(drop, QString(), Shape::Role::Shelf));

        QCOMPARE(drop.roles.value(u"Mangas"_s).role, Shape::Role::Work);
        QCOMPARE(drop.roles.value(u"Mangas/Elfes"_s).role, Shape::Role::Edition);
        QCOMPARE(drop.roles.value(u"Mangas/Elfes"_s).origin, Shape::Origin::Chosen);
    }

    /// Editions that declare themselves need a series, and the folder holding them is the only
    /// one they can have: read as a shelf « whose contents already declare themselves », it
    /// left two editions with no series, which the scan files nowhere.
    void a_folder_of_declared_editions_is_their_series()
    {
        wrote(u"Dragon Ball/Perfect/edition.json"_s, "{\"leaf\":1}");
        volumes(u"Dragon Ball/Perfect"_s, 1, 3);
        wrote(u"Dragon Ball/Original/edition.json"_s, "{\"leaf\":1}");
        volumes(u"Dragon Ball/Original"_s, 1, 3);

        const Shape::Drop drop = dropped(u"Dragon Ball"_s);
        const Shape::Suggestion said = drop.suggestions.value(QString());

        QCOMPARE(drop.roles.value(QString()).role, Shape::Role::Work);
        QCOMPARE(said.confidence, Shape::Confidence::Certain);
        QVERIFY(!saysAnything(said, Words::clueAllDeclared()));
        QVERIFY(saysAnything(said, Words::clueEditionsDeclared()));
        QCOMPARE(drop.roles.value(u"Perfect"_s).origin, Shape::Origin::Said);
        QCOMPARE(Shape::cards(drop), QStringList{QString()});
    }

    /// A universe holds files down to its editions' and no further — `layout::MODEL_DEPTH`
    /// on the server. One whose nearest archive is deeper would hold nothing the scan reads.
    void a_universe_is_greyed_when_its_nearest_archive_is_deeper_than_the_model()
    {
        wrote(u"X/A/B/C/Tome 1.cbz"_s);
        const Shape::Drop drop = dropped(u"X"_s);

        const Shape::Option universe =
            optionOf(Shape::options(drop, QString()), Shape::Role::Universe);
        QVERIFY(!universe.possible);
        QCOMPARE(universe.detail, Words::impossibleFilesTooDeepForAUniverse());
    }

    /// Out of the model, the one role left is no shelf anybody made: its line says nothing is
    /// read there, not that the library walks through it.
    void the_one_role_out_of_the_model_says_nothing_is_read_there()
    {
        wrote(u"X/Naruto/Tome 1.cbz"_s);
        wrote(u"X/Naruto/Bonus/Extra/Tome 2.cbz"_s);
        const Shape::Drop drop = dropped(u"X"_s);

        const Shape::Option shelf =
            optionOf(Shape::options(drop, u"Naruto/Bonus"_s), Shape::Role::Shelf);
        QVERIFY(shelf.possible);
        QCOMPARE(shelf.detail, Words::roleBeyondTheModel());
    }

    /// A role above a declared folder that the model could not hold it under would have the
    /// server refuse the whole import (`coherent_declarations`): it is greyed here instead,
    /// naming the folder that says otherwise.
    void a_role_that_would_contradict_a_declaration_below_is_greyed()
    {
        wrote(u"BD/Terres d’Arran/universe.json"_s, "{\"leaf\":1}");
        volumes(u"BD/Terres d’Arran/Elfes"_s, 1, 2);
        volumes(u"BD/Vrac"_s, 1, 2);
        Shape::Drop drop = dropped(u"BD"_s);

        const Shape::Option work = optionOf(Shape::options(drop, QString()), Shape::Role::Work);
        QVERIFY(!work.possible);
        QCOMPARE(work.detail,
                 Words::impossibleDeclaredBelow(u"Terres d’Arran"_s, u"universe.json"_s));
        QVERIFY(!Shape::apply(drop, QString(), Shape::Role::Work));
    }
};

QTEST_GUILESS_MAIN(ReadsTheShape)
#include "reads_the_shape.moc"
