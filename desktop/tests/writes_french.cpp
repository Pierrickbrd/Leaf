// The three things French asks for that are invisible when present and obvious when missing.
//
// A test rather than care, because care does not survive the twentieth label. The non-breaking
// space in particular cannot be seen in a diff, cannot be seen in the source, and can only be
// seen on screen once the window is narrow enough to break the line in front of the colon.

#include "Manifest.h"
#include "Words.h"

#include <QSet>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

class WritesFrench : public QObject
{
    Q_OBJECT

private slots:
    // ——— La fiche d'une série ————————————————————————————————————————————————

    void the_three_tabs_are_worded_once_and_in_order()
    {
        QCOMPARE(Words::tab(Words::Tab::Volumes), u"Tomes"_s);
        QCOMPARE(Words::tab(Words::Tab::Description), u"Description"_s);
        QCOMPARE(Words::tab(Words::Tab::Elsewhere), u"Voir aussi"_s);
    }

    /// Every label of the description, because a switch that grows a case and forgets its
    /// word answers an empty string — which draws a value with nothing in front of it.
    void every_label_of_the_description_has_a_word()
    {
        using enum Words::Fact;
        const QList<Words::Fact> all = {Writers, Artists, Publisher, Collection, Language,
                                        Kind, Direction, Status, Age, Colour, Held, Read,
                                        FirstReceived, LastReceived};
        QSet<QString> said;
        for (const Words::Fact one : all) {
            const QString word = Words::fact(one);
            QVERIFY2(!word.isEmpty(), qPrintable(QString::number(int(one))));
            said.insert(word);
        }
        // And no two of them are the same word: a description with two « Type » rows is a
        // description nobody can read.
        QCOMPARE(said.size(), all.size());
        QCOMPARE(Words::fact(Writers), u"Scénario"_s);
        QCOMPARE(Words::fact(Held), u"Tomes détenus"_s);
    }

    /// A range in its own unit, and never inferred from where the next separator falls: an
    /// arc does not end where the one below it starts.
    void an_arc_writes_its_range_in_its_own_unit()
    {
        using enum Api::Arc::Unit;
        QCOMPARE(Words::arcRange(Volume, 1, 4), u"tomes 1 à 4"_s);
        QCOMPARE(Words::arcRange(Chapter, 42, 68), u"chapitres 42 à 68"_s);
        // A range of one is a range all the same, and says so once rather than twice.
        QCOMPARE(Words::arcRange(Volume, 7, 7), u"tome 7"_s);
        QCOMPARE(Words::arcRange(Chapter, 12.5, 12.5), u"chapitre 12,5"_s);
    }

    /// The two halves of a file an arc runs through — and the open one, for the last file of
    /// an edition, which has no neighbour to bound it.
    void the_stretches_inside_a_crossed_volume_are_written_open_at_the_end()
    {
        QCOMPARE(Words::chapterRange(64, 68), u"chapitres 64 à 68"_s);
        QCOMPARE(Words::fromChapter(69), u"à partir du chapitre 69"_s);
    }

    /// Nine entries and two lists: a series is not a file, and « enregistrer une copie » of
    /// thirty volumes is not a thing. Each names what it acts on, because the menu is opened
    /// from a tile among fifty and the pointer has moved by the time it is read.
    void the_menu_names_what_each_entry_acts_on()
    {
        using enum Words::Command;
        QCOMPARE(Words::command(MarkSeriesRead), u"Marquer toute la série comme lue"_s);
        QCOMPARE(Words::command(MarkEntryRead), u"Marquer comme lu"_s);
        QCOMPARE(Words::command(ReimportEntry), u"Réimporter ce tome…"_s);
        QCOMPARE(Words::command(EraseSeries), u"Supprimer la série…"_s);
        // The ellipsis is the promise that something else opens, and it is the real one and
        // not three dots: three dots break across a line.
        for (const Words::Command one : {ReimportSeries, ReimportEntry, SaveACopy, EraseSeries,
                                         EraseEntry}) {
            QVERIFY2(Words::command(one).endsWith(u"…"_s),
                     qPrintable(Words::command(one)));
        }
    }

    /// The sentence that changes with the volume, and the whole reason it is composed rather
    /// than written once.
    void a_deletion_says_what_it_leaves_behind()
    {
        // In the middle: a hole, and the numbering that does not move with it.
        const QString middle =
            Words::whatWouldRemain(28, Api::Medium::Bd, 23, true, std::optional<double>(24));
        QCOMPARE(middle, u"Il restera 28 albums, et le 23 rejoindra les manquants."
                         " La numérotation ne bouge pas : le 24 reste le 24."_s);
        // At an end: the ceiling comes down and nothing is missing that was not before.
        QCOMPARE(Words::whatWouldRemain(28, Api::Medium::Bd, 29, false, std::nullopt),
                 u"Il restera 28 albums."_s);
    }

    /// What a whole edition takes with it. A deletion that speaks only of files hides half of
    /// what it carries away.
    void a_whole_edition_says_what_goes_besides_the_files()
    {
        QCOMPARE(Words::whatAWholeEditionTakes(29, 1503238553, 4, true, 5),
                 u"29 fichiers, 1,4 Gio. Les 4 tomes lus et la position dans le 5ᵉ seront "
                 "oubliés avec eux."_s);
        // Nothing read: the files and nothing else, rather than a sentence about no readings.
        QCOMPARE(Words::whatAWholeEditionTakes(29, 1503238553, 0, false, 1),
                 u"29 fichiers, 1,4 Gio."_s);
        // One file, and French keeps its singular.
        QVERIFY(Words::whatAWholeEditionTakes(1, 4800, 0, false, 1).startsWith(u"1 fichier,"_s));
    }

    void the_words_a_deletion_uses_are_the_ones_it_means()
    {
        QCOMPARE(Words::eraseEntryQuestion(23), u"Supprimer le tome 23 ?"_s);
        QCOMPARE(Words::eraseSeriesQuestion(u"Elfes"_s),
                 u"Supprimer « Elfes » ?"_s);
        QCOMPARE(Words::whatGoes(u"La Dryade"_s, 54, 48000000),
                 u"La Dryade — 54 pages, 45,8 Mio"_s);
        // A volume with no title of its own says what it weighs, not a dash leading nowhere.
        QCOMPARE(Words::whatGoes(QString(), 54, 48000000), u"54 pages, 45,8 Mio"_s);
        QCOMPARE(Words::whichFile(u"Tome 23.cbz"_s), u"Fichier : Tome 23.cbz"_s);
        QCOMPARE(Words::typeToConfirm(u"Elfes"_s), u"Tapez Elfes pour confirmer"_s);
        QVERIFY(Words::noTrash(true).contains(u"les ramènera pas"_s));
        QVERIFY(Words::noTrash(false).contains(u"le ramènera pas"_s));
        QCOMPARE(Words::wouldNotGo({u"Tome 4.cbz"_s}),
                 u"Un fichier n’a pas pu être supprimé : Tome 4.cbz"_s);
        QCOMPARE(Words::wouldNotGo({u"Tome 4.cbz"_s, u"Tome 9.cbz"_s}),
                 u"2 fichiers n’ont pas pu être supprimés : Tome 4.cbz, Tome 9.cbz"_s);
        // Nothing refused is nothing said, not « 0 fichiers ».
        QVERIFY(Words::wouldNotGo({}).isEmpty());
        QCOMPARE(Words::couldNotWrite(u"/home/quelqu'un/Tome 5.cbz"_s),
                 u"Tome 5.cbz n’a pas pu être écrit."_s);
    }

    /// The two blocks of the last tab, titled apart because they are not the same intention.
    void the_last_tab_titles_its_two_blocks_apart()
    {
        QCOMPARE(Words::sameWorkOtherwise(), u"La même œuvre, autrement"_s);
        QCOMPARE(Words::inTheUniverse(), u"Dans l’univers"_s);
        QCOMPARE(Words::outsideTheOrder(), u"Hors parcours"_s);
        QCOMPARE(Words::seriesCount(1), u"1 série"_s);
        QCOMPARE(Words::seriesCount(4), u"4 séries"_s);
        // Nothing to count is no line, rather than « 0 série » under a heading that is not
        // drawn either.
        QVERIFY(Words::seriesCount(0).isEmpty());
    }

    /// The name of the universe, and how many others it holds — the count drops with its
    /// separator when a walk is being drawn, because the walk counts its own steps.
    void the_universe_line_counts_the_others_and_never_itself()
    {
        QCOMPARE(Words::universeLine(u"Terres d'Arran"_s, 3), u"Terres d'Arran · 3 autres"_s);
        QCOMPARE(Words::universeLine(u"Terres d'Arran"_s, 1), u"Terres d'Arran · 1 autre"_s);
        QCOMPARE(Words::universeLine(u"Terres d'Arran"_s, 0), u"Terres d'Arran"_s);
        QVERIFY(Words::universeLine(QString(), 3).isEmpty());
    }

    /// « ici » goes on the tile's own grey line: a tile has one, and a mark floating over a
    /// cover would have to be placed again at every size.
    void the_tile_being_read_says_so_on_the_line_it_already_has()
    {
        QCOMPARE(Words::hereToo(u"29 albums"_s), u"29 albums · ici"_s);
        // A step covering a whole work of an edition nobody holds has no count to carry, and
        // « ici » still has to be said.
        QCOMPARE(Words::hereToo(QString()), u"ici"_s);
    }

    void a_number_is_written_the_way_french_writes_it()
    {
        // A 3.5 is a side story, and « Tome 3.5 » is English.
        QCOMPARE(Words::number(3.5), u"3,5"_s);
        QCOMPARE(Words::number(12.0), u"12"_s);
        QCOMPARE(Words::number(-1), u"-1"_s);
    }

    void the_three_reading_directions_and_the_two_colours()
    {
        QCOMPARE(Words::readingDirection(Api::ReadingDirection::LeftToRight),
                 u"Gauche à droite"_s);
        QCOMPARE(Words::readingDirection(Api::ReadingDirection::RightToLeft),
                 u"Droite à gauche"_s);
        QCOMPARE(Words::readingDirection(Api::ReadingDirection::Vertical), u"Verticale"_s);
        // Positive on both sides: « pas en couleur » is a double negative nobody reads twice.
        QCOMPARE(Words::colour(true), u"Couleur"_s);
        QCOMPARE(Words::colour(false), u"Noir et blanc"_s);
    }

    /// Never below two: there is nothing to choose between when a work has one edition.
    void the_switcher_says_nothing_below_two_editions()
    {
        QVERIFY(Words::editions(0).isEmpty());
        QVERIFY(Words::editions(1).isEmpty());
        QCOMPARE(Words::editions(2), u"2 éditions"_s);
        QVERIFY(Words::arcs(0).isEmpty());
        QCOMPARE(Words::arcs(1), u"1 arc"_s);
        QCOMPARE(Words::arcs(5), u"5 arcs"_s);
    }

    /// From two. A finished series would otherwise carry « ×1 » on every line for no news.
    void how_often_a_volume_was_finished_is_said_from_two()
    {
        QVERIFY(Words::timesFinished(0).isEmpty());
        QVERIFY(Words::timesFinished(1).isEmpty());
        QCOMPARE(Words::timesFinished(6), u"×6"_s);
    }

    /// The label agrees and the value enumerates — commas to the end and no « et », because
    /// this is a list of identifiers and not a sentence.
    void the_missing_volumes_agree_and_enumerate()
    {
        QCOMPARE(Words::missingLabel(1), u"Manquant"_s);
        QCOMPARE(Words::missingLabel(4), u"Manquants"_s);
        QVERIFY(Words::missingVolumes({}).isEmpty());
        QCOMPARE(Words::missingVolumes({7}), u"Tome 7"_s);
        QCOMPARE(Words::missingVolumes({7, 9, 12, 18}), u"Tomes 7, 9, 12, 18"_s);
    }

    void what_a_library_holds_of_an_edition_is_said_out_of_what_it_runs_to()
    {
        QCOMPARE(Words::heldOutOf(29, 30), u"29 sur 30"_s);
        // Nothing declared is nothing to be out of.
        QCOMPARE(Words::heldOutOf(29, 0), u"29"_s);
    }

    /// The ordinal is worth the trouble: « le 5 en cours » reads as a quantity.
    void how_many_are_read_says_whether_one_is_open()
    {
        QCOMPARE(Words::readEntries(4, false, 5), u"4"_s);
        QCOMPARE(Words::readEntries(4, true, 5), u"4 · le 5ᵉ en cours"_s);
        QCOMPARE(Words::readEntries(0, true, 1), u"0 · le 1ᵉʳ en cours"_s);
    }

    void the_page_has_words_for_what_it_has_none_of()
    {
        QCOMPARE(Words::neverRead(), u"Non lu"_s);
        QCOMPARE(Words::inThisLibrary(), u"Dans cette bibliothèque"_s);
        QCOMPARE(Words::volumesAxis(), u"les tomes"_s);
        QVERIFY(Words::noVolumeByThatName().endsWith(u"."_s));
    }

    /// A BD comes in albums. That is the shelf's whole reason for reading the medium.
    void a_bd_comes_in_albums_and_everything_else_in_volumes()
    {
        QCOMPARE(Words::volumes(21, Api::Medium::Manga), u"21 tomes"_s);
        QCOMPARE(Words::volumes(7, Api::Medium::Bd), u"7 albums"_s);
        QCOMPARE(Words::volumes(11, Api::Medium::Comics), u"11 tomes"_s);
        QCOMPARE(Words::volumes(8, std::nullopt), u"8 tomes"_s);
    }

    /// French keeps the singular at zero as well as at one, which English does not.
    void zero_and_one_are_both_singular()
    {
        QCOMPARE(Words::volumes(1, Api::Medium::Manga), u"1 tome"_s);
        QCOMPARE(Words::volumes(0, Api::Medium::Manga), u"0 tome"_s);
        QCOMPARE(Words::volumes(1, Api::Medium::Bd), u"1 album"_s);
        QCOMPARE(Words::volumes(2, Api::Medium::Manga), u"2 tomes"_s);
    }

    /// « BD », never « Bd ». An acronym is why none of this can be a capitalise-the-first
    /// -letter helper run over the contract's spelling.
    void an_acronym_keeps_its_case()
    {
        QCOMPARE(Words::medium(Api::Medium::Bd), u"BD"_s);
        QCOMPARE(Words::medium(Api::Medium::Manga), u"Manga"_s);
        QCOMPARE(Words::medium(Api::Medium::Comics), u"Comics"_s);
    }

    /// One capital, at the start. « Non Lues » is the habit that arrives with every interface
    /// copied from an American one.
    void a_label_carries_exactly_one_capital()
    {
        for (const QString &label : {Words::readStatus(Api::ReadStatus::Unread),
                                     Words::readStatus(Api::ReadStatus::InProgress),
                                     Words::readStatus(Api::ReadStatus::Read)}) {
            QVERIFY2(!label.isEmpty(), "a status with no wording");
            QVERIFY2(label.at(0).isUpper(), qPrintable(label));
            const QString rest = label.mid(1);
            QVERIFY2(rest == rest.toLower(), qPrintable(label));
        }
        QCOMPARE(Words::readStatus(Api::ReadStatus::Unread), u"Non lues"_s);
    }

    /// The one that cannot be seen anywhere but on screen, and only once the line breaks.
    void a_colon_is_preceded_by_a_space_that_will_not_break()
    {
        const QString sorted = Words::labelled(u"Trier"_s, u"Nom"_s);
        QCOMPARE(sorted, u"Trier : Nom"_s);
        QVERIFY2(!sorted.contains(u" :"_s), "a breaking space before the colon");
        QVERIFY(sorted.contains(Words::Nbsp));
    }

    void the_band_reads_its_three_facts_in_order()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 12.0, 47, 190, u"Chapitre 98"_s),
                 u"Tome 12 · Page 47/190 · Chapitre 98"_s);
    }

    /// « Non lues 12 » — the count belongs to the pill, and the pill is one string.
    void a_pill_carries_its_count()
    {
        QCOMPARE(Words::pill(Words::readStatus(Api::ReadStatus::Unread), 12),
                 u"Non lues 12"_s);
        QCOMPARE(Words::pill(Words::medium(Api::Medium::Bd), 12), u"BD 12"_s);
        // A count of zero is still a count: the server does not offer a value nothing
        // matches, so this can only arrive from a client that made one up.
        QCOMPARE(Words::pill(u"Comics"_s, 0), u"Comics 0"_s);
    }

    /// « Trier : Nom », with the space that will not break before the colon — and the four
    /// orders the bar offers, worded here and nowhere else.
    void the_bar_names_the_order_in_full()
    {
        QCOMPARE(Words::sortOrder(Api::Sort::Name), u"Nom"_s);
        // "Ajout" and not "Ajout récent": the direction is now said beside the criterion, and
        // a criterion that carries one of them in its name contradicts the other half.
        QCOMPARE(Words::sortOrder(Api::Sort::Added), u"Ajout"_s);
        QCOMPARE(Words::sortOrder(Api::Sort::Read), u"Dernière lecture"_s);
        QCOMPARE(Words::sortOrder(Api::Sort::Volumes), u"Nombre de tomes"_s);
        QCOMPARE(Words::labelled(u"Trier"_s, Words::sortOrder(Api::Sort::Name)),
                 u"Trier"_s + Words::Nbsp + u": Nom"_s);
    }

    /// What the settings screen says. Written here and nowhere else, for the reason this
    /// file exists: a string spelled in a screen is a string no test ever reads.
    void the_settings_screen_says_where_the_key_came_from()
    {
        // A reader does not have a server, they have a library that answers or does not.
        QCOMPARE(Words::theServer(), u"Connexion"_s);
        QCOMPARE(Words::connected(true), u"Bibliothèque connectée"_s);
        QCOMPARE(Words::connected(false), u"Pas de connexion"_s);
        QCOMPARE(Words::appearanceChoice(Words::Looks::System), u"Système"_s);
        QCOMPARE(Words::appearanceChoice(Words::Looks::Light), u"Clair"_s);
        QCOMPARE(Words::appearanceChoice(Words::Looks::Dark), u"Sombre"_s);
        // Named after where it goes, not after itself: a tooltip repeating the arrow it
        // sits on says nothing the arrow did not.
        QVERIFY(Words::backTo(u"L’étagère"_s).contains(u"étagère"_s));
        QCOMPARE(Words::backTo(QString()), u"Retour"_s);

        using enum Words::KeyFrom;
        QCOMPARE(Words::keyStorage(Environment), u"Dans l’environnement"_s);
        QCOMPARE(Words::keyStorage(Keyring), u"Dans le trousseau de la session"_s);
        QCOMPARE(Words::keyStorage(ProtectedFile), u"Dans un fichier protégé"_s);
        // Not "no key": the key may be there and its origin unknown, and the screen must
        // not accuse a working setup of being broken.
        QCOMPARE(Words::keyStorage(Unknown), u"Introuvable"_s);
    }

    void the_settings_screen_says_where_the_scan_is()
    {
        using enum Words::Scanning;
        QCOMPARE(Words::scanState(Running), u"En cours…"_s);
        QCOMPARE(Words::scanState(Done), u"Terminé"_s);
        QCOMPARE(Words::scanState(Idle), u"À l’arrêt"_s);
        // A state the server grew and this client was not taught is said, not guessed at.
        QVERIFY(!Words::scanState(Other).isEmpty());
        QVERIFY(Words::scanState(Other) != Words::scanState(Idle));

        // Never run is not a failure, and an empty line would read as a missing answer.
        QCOMPARE(Words::lastScan(0), u"Jamais lancé"_s);
        QVERIFY(Words::lastScan(1788463365455LL).startsWith(u"Dernier scan"_s));
    }

    /// Absolute and never « il y a trois heures »: the question asked of a scan is whether
    /// it ran since you changed something, and only a date answers that.
    void a_moment_is_a_date_and_not_a_distance()
    {
        // 3 May 2026, 12:02:45 UTC — read back in this machine's own zone, so the day is
        // asserted and the hour is not.
        const QString said = Words::moment(1777809765000LL);
        QVERIFY2(said.contains(u"2026"_s), qPrintable(said));
        QVERIFY2(said.contains(u"mai"_s), qPrintable(said));
        QVERIFY2(!said.contains(u"May"_s), qPrintable(said));

        // No scan has a date of zero; an empty string is what an absent one looks like.
        QCOMPARE(Words::moment(0), QString());
        QCOMPARE(Words::moment(-1), QString());
    }

    void the_settings_screen_counts_what_the_server_holds()
    {
        QCOMPARE(Words::libraryHolds(0), u"Aucune série"_s);
        QCOMPARE(Words::libraryHolds(1), u"1 série"_s);
        QCOMPARE(Words::libraryHolds(6), u"6 séries"_s);
        QCOMPARE(Words::apiVersion(1, 1), u"API 1 · format 1"_s);
        // A server that answers and one with no address at all are different troubles, and
        // the screen says which.
        QVERIFY(Words::answering(true) != Words::answering(false));
        QVERIFY(Words::sharedFolder(true) != Words::sharedFolder(false));
    }

    /// The eight axes a library can be narrowed by. The contract's spelling goes in, because
    /// that is what the server answers with and what goes back on the wire.
    void the_panel_names_every_axis_it_offers()
    {
        QCOMPARE(Words::axis(u"read"_s), u"Lecture"_s);
        QCOMPARE(Words::axis(u"medium"_s), u"Type"_s);
        QCOMPARE(Words::axis(u"universe"_s), u"Univers"_s);
        QCOMPARE(Words::axis(u"genre"_s), u"Genre"_s);
        QCOMPARE(Words::axis(u"author"_s), u"Auteur"_s);
        QCOMPARE(Words::axis(u"publisher"_s), u"Éditeur"_s);
        QCOMPARE(Words::axis(u"language"_s), u"Langue"_s);
        QCOMPARE(Words::axis(u"status"_s), u"Statut"_s);

        // An axis this client has no word for is not drawn rather than drawn nameless: the
        // server may grow a ninth before this client is taught it.
        QCOMPARE(Words::axis(u"colour"_s), QString());
    }

    /// Whether the work is still being published, which is not whether you have read it.
    /// « En cours » under two headings would be one word doing two jobs.
    void a_work_still_being_published_is_not_a_work_you_are_partway_through()
    {
        QCOMPARE(Words::editionStatus(u"ongoing"_s), u"En parution"_s);
        QCOMPARE(Words::editionStatus(u"completed"_s), u"Terminée"_s);
        QVERIFY(Words::editionStatus(u"ongoing"_s) != Words::readStatus(Api::ReadStatus::InProgress));

        // A word this client has not been taught is shown as it stands: the reader can still
        // choose it, and it is their own library saying it.
        QCOMPARE(Words::editionStatus(u"hiatus"_s), u"hiatus"_s);
    }

    void a_language_tag_becomes_a_word_for_it()
    {
        QCOMPARE(Words::language(u"fr"_s), u"Français"_s);
        QCOMPARE(Words::language(u"ja"_s), u"日本語"_s);
        // Not a tag anybody speaks: shown as it stands rather than filed under whatever the
        // system happens to answer, which would put every foreign edition under « Français ».
        QCOMPARE(Words::language(u"zzz"_s), u"zzz"_s);
        QCOMPARE(Words::language(QString()), QString());
    }

    /// The order and the way it runs, in the one string the bar shows and the menu repeats on
    /// the criterion in force — which is what makes the second click on it have a target.
    void the_order_says_which_way_it_runs()
    {
        QCOMPARE(Words::sortValue(Api::Sort::Name, false), u"Nom · A → Z"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Name, true), u"Nom · Z → A"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Added, false), u"Ajout · récent → ancien"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Added, true), u"Ajout · ancien → récent"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Read, false), u"Lecture · récente → ancienne"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Read, true), u"Lecture · ancienne → récente"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Volumes, false), u"Tomes · plus → moins"_s);
        QCOMPARE(Words::sortValue(Api::Sort::Volumes, true), u"Tomes · moins → plus"_s);

        // Every one of the eight is a sentence, never an empty string from a value the
        // enumeration grew and this file was not told about.
        using enum Api::Sort;
        for (const Api::Sort order : {Name, Added, Volumes, Read}) {
            for (const bool reversed : {false, true})
                QVERIFY2(!Words::sortValue(order, reversed).isEmpty(), "un tri sans mots");
        }
    }

    /// What a search says about what it found, and about what it did not. Written here rather
    /// than where they are shown, for the reason this whole file exists: a string spelled in a
    /// screen is a string no test ever reads.
    void a_search_says_what_it_found()
    {
        QCOMPARE(Words::overview(), u"Aperçu"_s);
        QCOMPARE(Words::series(6), u"Séries · 6"_s);
        QCOMPARE(Words::files(17), u"Fichiers · 17"_s);
        QCOMPARE(Words::files(1), u"Fichiers · 1"_s);

        QCOMPARE(Words::seeAllSeries(6), u"Voir les 6 séries"_s);
        QCOMPARE(Words::seeAllSeries(1), u"Voir la série"_s);
        QCOMPARE(Words::seeAllFiles(60), u"Voir les 60 fichiers"_s);
        QCOMPARE(Words::seeAllFiles(1), u"Voir le fichier"_s);

        // One is not "les 1 autres".
        QCOMPARE(Words::seeTheOthers(12), u"Voir les 12 autres"_s);
        QCOMPARE(Words::seeTheOthers(1), u"Voir l’autre"_s);

        // A guess is asked, not stated — and French puts a space before the mark.
        QCOMPARE(Words::didYouMean(u"Tsugumi Ōba"_s),
                 u"Vouliez-vous dire Tsugumi Ōba"_s + Words::Nbsp + u"?"_s);
        QVERIFY(Words::didYouMean(u"x"_s).contains(Words::Nbsp));

        QCOMPARE(Words::noSeriesByThatName(), u"Aucune série ne porte ce nom."_s);
        QVERIFY(Words::searchHint().endsWith(u"…"_s));
        QVERIFY2(!Words::searchHint().contains(u"..."_s), "three dots are not an ellipsis");
        QCOMPARE(Words::filter(), u"Filtrer"_s);
        QCOMPARE(Words::clearTheSearch(), u"Effacer la recherche"_s);
    }

    void a_file_says_which_volume_contains_it()
    {
        Api::Hit chapter;
        chapter.kind = Api::Hit::Kind::Chapter;
        chapter.label = u"Assaut"_s;
        chapter.seriesName = u"Parasite · Édition Deluxe"_s;
        chapter.entryKind = Api::UpNext::Kind::Volume;
        chapter.entryNumber = 8.0;
        chapter.entryTitle = u"Invasion"_s;
        chapter.entryPageCount = 190;
        QCOMPARE(Words::fileContext(chapter),
                 u"Parasite · Édition Deluxe · Tome 8 · Invasion · 190 pages"_s);

        Api::Hit volume;
        volume.kind = Api::Hit::Kind::Entry;
        volume.label = u"Tome 1"_s;
        volume.seriesName = u"Death Note · Black Edition"_s;
        volume.entryNumber = 1.0;
        volume.entryPageCount = 1;
        QCOMPARE(Words::fileContext(volume), u"Death Note · Black Edition · 1 page"_s);

        // A volume nobody titled carries its own label as its title. Said once: the line
        // read « Parasite Reversi · Tome 2 · Tome 2 · 187 pages » on a real library.
        Api::Hit untitled;
        untitled.kind = Api::Hit::Kind::Chapter;
        untitled.label = u"Amour d’été, premier émoi"_s;
        untitled.seriesName = u"Parasite Reversi"_s;
        untitled.entryKind = Api::UpNext::Kind::Volume;
        untitled.entryNumber = 2.0;
        untitled.entryTitle = u"Tome 2"_s;
        untitled.entryPageCount = 187;
        QCOMPARE(Words::fileContext(untitled),
                 u"Parasite Reversi · Tome 2 · 187 pages"_s);
    }

    void the_band_distinguishes_resuming_from_the_next_entry()
    {
        QCOMPARE(Words::resumeAction(Api::UpNext::Reason::InProgress), u"Reprendre"_s);
        QCOMPARE(Words::resumeAction(Api::UpNext::Reason::NextUp), u"Continuer"_s);
    }

    /// Half a screen leaves no room for the long line. Only the volume abbreviates: the
    /// chapter segment goes rather than turn into an abbreviation nobody has agreed on.
    void a_band_at_half_a_screen_shortens_the_volume_and_drops_the_chapter()
    {
        QCOMPARE(Words::whereShort(Api::UpNext::Kind::Volume, 12.0, 47, 190),
                 u"T12 · Page 47/190"_s);
        QCOMPARE(Words::whereShort(Api::UpNext::Kind::Chapter, 98.0, 47, 190),
                 u"Chapitre 98 · Page 47/190"_s);
    }

    /// A chapter entry has already said which chapter it is. Saying it twice reads as two
    /// different chapters.
    void a_chapter_entry_does_not_name_its_chapter_twice()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Chapter, 98.0, 47, 190, u"Chapitre 98"_s),
                 u"Chapitre 98 · Page 47/190"_s);
    }

    /// Nothing started, nothing to say about a page. A card reading "Page /" is worse than a
    /// shorter card.
    void a_segment_with_nothing_to_say_is_left_out()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 1.0, std::nullopt, 190, std::nullopt),
                 u"Tome 1"_s);
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, std::nullopt, 47, 190, std::nullopt),
                 u"Page 47/190"_s);
    }

    /// A half is a side story, and French writes it with a comma. A whole one carries no
    /// decimal at all — « Tome 12,0 » is neither language.
    void a_half_volume_is_written_with_a_comma()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 3.5, std::nullopt, 0, std::nullopt),
                 u"Tome 3,5"_s);
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 12.0, std::nullopt, 0, std::nullopt),
                 u"Tome 12"_s);
    }

    /// The sentence that stops the shelf looking broken when a lit pill hides everything the
    /// search found. It names the pills exactly as the bar words them.
    void a_search_behind_a_lit_pill_says_so()
    {
        QCOMPARE(Words::nothingHere({u"Manga"_s}, 3),
                 u"Aucun résultat dans Manga · 3 sans les filtres"_s);
        QCOMPARE(Words::nothingHere({u"BD"_s, u"Non lues"_s}, 12),
                 u"Aucun résultat dans BD, Non lues · 12 sans les filtres"_s);
    }

    /// A straight apostrophe is the third of the three, and the easiest to type by accident.
    /// Nothing produced here has one today; this is what keeps it that way.
    void nothing_carries_a_straight_apostrophe()
    {
        QStringList every{Words::labelled(u"Trier"_s, u"Nom"_s),
                          Words::nothingHere({u"BD"_s}, 1),
                          Words::overview(),         Words::series(3),
                          Words::sortValue(Api::Sort::Name, false),
                          Words::sortValue(Api::Sort::Added, true),
                          Words::files(17),          Words::seeTheOthers(1),
                          Words::seeTheOthers(12),   Words::seeAllSeries(3),
                          Words::seeAllFiles(12),    Words::didYouMean(u"Tsugumi Ōba"_s),
                          Words::searchHint(),       Words::searchHintShort(),
                          Words::clearTheSearch(),   Words::filter(),
                          Words::noSeriesByThatName(),
                          Words::noLibrary(),
                          Words::unreachable(u"Connection refused"_s),
                          Words::unreadableAnswer(),
                          Words::keyRefused({}),
                          Words::keyRefused(u"expirée"_s),
                          Words::tooManyWrongKeys(u"30"_s),
                          Words::noSuchThing(),
                          Words::serverAnswered(500, {}),
                          Words::serverAnswered(500, u"en panne"_s),
                          Words::waitingBeforeAsking(12),
                          Words::tooManyWaiting(),
                          Words::queryBelongsApart(),
                          Words::nothingConfigured(u"leaf.conf"_s),
                          Words::noAddress(u"leaf.conf"_s),
                          Words::noKey(u"leaf.conf"_s),
                          Words::readableByOthers(u"leaf.conf"_s),
                          Words::importing(),
                          Words::dropFilesHere(),
                          Words::dropHow(),
                          Words::cancel(),
                          Words::goBack(),
                          Words::next(),
                          Words::startImport(),
                          Words::pauseIt(),
                          Words::resumeIt(),
                          Words::abandonIt(),
                          Words::noSeriesForThisFile(),
                          Words::decisionsWaiting(1),
                          Words::decisionsWaiting(4),
                          Words::howFar(12 * 1024 * 1024, 134217728),
                          Words::tryingAgainIn(8),
                          Words::couldNotBeRead(u"Tome 7.cbz"_s),
                          Words::couldNotCleanUp(u"Koro"_s),
                          Words::sameDestination(u"Vieux/Death Note"_s, u"Death Note"_s),
                          Words::size(4404019200LL),
                          Words::nodeHolds(34, 4404019200LL),
                          Words::checking(12, 68),
                          Words::checking(0, 1),
                          Words::checking(0, 0),
                          Words::waitingToBeChecked(),
                          Words::willBeCreated(Manifest::Level::Universe),
                          Words::willBeCreated(Manifest::Level::Work),
                          Words::willBeMoved(Manifest::Level::Universe),
                          Words::willBeMoved(Manifest::Level::Work),
                          Words::alreadyInTheLibrary(),
                          Words::willBeSent(),
                          Words::beingSent(),
                          Words::wasFiled(Manifest::Level::Universe),
                          Words::wasFiled(Manifest::Level::Work),
                          Words::failedToSend(),
                          Words::announcing(),
                          Words::nothingToSend(),
                          Words::dropSomethingElse(),
                          Words::declarationDiffers(u"Death Note"_s, {u"résumé"_s}),
                          Words::declarationDiffers(QString(), {}),
                          Words::fileNamed(u"Assassinat"_s, u"Tome 1.cbz"_s),
                          Words::holdsReplacements(1, 0),
                          Words::holdsReplacements(6, 0),
                          Words::holdsReplacements(6, 2),
                          Words::insteadOf(u"Assassinat"_s, 1024, true),
                          Words::insteadOf(QString(), 1024, false),
                          Words::willBeReplaced(Manifest::Level::Universe),
                          Words::willBeReplaced(Manifest::Level::Work),
                          Words::levelTitle(Manifest::Level::Universe),
                          Words::levelTitle(Manifest::Level::Work),
                          Words::levelTitle(Manifest::Level::Edition),
                          Words::levelAnd(Manifest::Level::Work,
                                          Words::willBeCreated(Manifest::Level::Work)),
                          Words::levelAnd(Manifest::Level::Edition, QString())};
        for (int i = 0; i <= int(Manifest::Level::Volume); ++i)
            every << Words::level(Manifest::Level(i));
        for (int i = 0; i <= int(Words::Importing::Failed); ++i)
            every << Words::importStage(Words::Importing(i));
        for (int i = 0; i <= int(Words::Asking::State); ++i)
            every << Words::notSetUp(Words::Asking(i));
        for (int i = 0; i <= int(Api::Medium::Other); ++i)
            every << Words::medium(Api::Medium(i));
        for (int i = 0; i <= int(Api::ReadStatus::Read); ++i)
            every << Words::readStatus(Api::ReadStatus(i));
        for (int i = 0; i <= int(Navigation::Destination::Settings); ++i)
            every << Words::destination(Navigation::Destination(i));
        for (int i = 0; i <= int(Widths::Band::Wide); ++i)
            every << Words::band(Widths::Band(i));
        for (int i = 0; i <= int(Api::UpNext::Reason::NextUp); ++i)
            every << Words::resumeAction(Api::UpNext::Reason(i));

        for (const QString &one : std::as_const(every))
            QVERIFY2(!one.contains(u'\''), qPrintable(u"straight apostrophe in: "_s + one));
    }

    /// The placeholder card's own name for each screen — the five strings first written
    /// straight into `Main.qml`, where nothing tests them. `Navigation::label` returns exactly
    /// this, so this is what a QML change to that property would actually be checked against.
    void each_destination_names_itself()
    {
        QCOMPARE(Words::destination(Navigation::Destination::Shelf), u"Étagère"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Series), u"Série"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Reader), u"Lecteur"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Health), u"Santé"_s);
        QCOMPARE(Words::destination(Navigation::Destination::Settings), u"Réglages"_s);
    }

    /// The card's second line — one label per band, the same three `Widths::bandFor` names.
    void each_band_is_named_in_french()
    {
        QCOMPARE(Words::band(Widths::Band::Wide), u"Large"_s);
        QCOMPARE(Words::band(Widths::Band::Medium), u"Moyenne"_s);
        QCOMPARE(Words::band(Widths::Band::Narrow), u"Étroite"_s);
    }

    /// « Étagère » and « Étroite » are the two of these eight new words that open on an
    /// accented capital. Nothing else here guards that: a straight "E" compiles, links, and
    /// is wrong to nobody but a reader looking at the actual screen. Pinned to the code point
    /// itself, not to another literal, since a copy-pasted "E" would match a wrong literal
    /// just as happily as the right one.
    void the_capitals_that_take_an_accent_keep_it()
    {
        QCOMPARE(Words::destination(Navigation::Destination::Shelf).at(0).unicode(),
                 char16_t(0x00C9)); // É
        QCOMPARE(Words::band(Widths::Band::Narrow).at(0).unicode(), char16_t(0x00C9)); // É
    }

    /// Each of `readStatus`, `medium`, `destination` and `band` switches exhaustively over its
    /// own enum; the trailing `return {}` after every one of them is what an out-of-range
    /// value — the shape an untrusted `int` cast from QML would take — reaches instead of
    /// undefined behaviour. `Navigation::required()` is guarded the same way, for the same
    /// reason: a switch with no `default:` is deliberate, and this is what the compiler-visible
    /// gap after it is for.
    void a_value_outside_its_enumeration_returns_nothing_rather_than_crashing()
    {
        QVERIFY(Words::readStatus(static_cast<Api::ReadStatus>(99)).isEmpty());
        QVERIFY(Words::medium(static_cast<Api::Medium>(99)).isEmpty());
        QVERIFY(Words::destination(static_cast<Navigation::Destination>(99)).isEmpty());
        QVERIFY(Words::band(static_cast<Widths::Band>(99)).isEmpty());
        QVERIFY(Words::resumeAction(static_cast<Api::UpNext::Reason>(99)).isEmpty());
        QVERIFY(Words::notSetUp(static_cast<Words::Asking>(99)).isEmpty());
        QVERIFY(Words::importStage(static_cast<Words::Importing>(99)).isEmpty());
    }

    /// Every sentence Leaf says when something is wrong was a `tr(...)` literal at its call
    /// site, and no catalogue is installed — so `tr` handed its own English back and the
    /// settings screen read « The server could not be reached ». These are the words a
    /// reader meets on their worst day; they are the last place to leave in English.
    void what_goes_wrong_is_said_in_french_too()
    {
        QCOMPARE(Words::notSetUp(Words::Asking::Shelf),
                 u"Leaf n’a pas pu s’installer"_s + Words::Nbsp
                         + u": il n’y a rien à afficher."_s);
        QCOMPARE(Words::unreachable(u"Connexion refusée"_s),
                 u"Le serveur est injoignable — Connexion refusée"_s);
        QCOMPARE(Words::serverAnswered(404, {}), u"Le serveur a répondu 404."_s);
        QCOMPARE(Words::keyRefused(u"périmée"_s),
                 u"La clé a été refusée"_s + Words::Nbsp + u": périmée"_s);
    }

    /// The nine kinds of trouble a scan reports, each said in French. They were worded and
    /// nothing read them: a kind the server grows and this file does not know comes back
    /// empty, and the card above it would then carry a heading of nothing at all.
    void every_kind_of_trouble_a_scan_reports_has_a_french_heading()
    {
        const QStringList kinds{u"ERRORS"_s,           u"MISSING_METADATA"_s,
                                u"DISREGARDED"_s,      u"CONTRADICTIONS"_s,
                                u"IDENTITY"_s,         u"WITHOUT_METADATA"_s,
                                u"DUPLICATE_NUMBERS"_s, u"DUPLICATE_PAGES"_s,
                                u"DERIVED_ARCS"_s};
        QStringList said;
        for (const QString &kind : kinds) {
            const QString heading = Words::finding(kind);
            QVERIFY2(!heading.isEmpty(), qPrintable(u"no heading for "_s + kind));
            QVERIFY2(!heading.contains(u'\''), qPrintable(heading));
            said << heading;
        }
        // Nine kinds and nine headings: two kinds sharing one would read as one card.
        QCOMPARE(QSet<QString>(said.constBegin(), said.constEnd()).size(), kinds.size());
        // And a word this version has not learned says nothing rather than guessing.
        QVERIFY(Words::finding(u"SOMETHING_ELSE"_s).isEmpty());
    }

    /// The counted sentences all have a singular and a plural, and say nothing at zero —
    /// a card that reads « 0 tome relu » is a card about nothing.
    void what_a_scan_counted_is_said_once_or_not_at_all()
    {
        QVERIFY(Words::reanalysed(0).isEmpty());
        QCOMPARE(Words::reanalysed(1), u"1 tome relu"_s);
        QCOMPARE(Words::reanalysed(7), u"7 tomes relus"_s);
        // Silent at nought on both halves, because on every scan but one both are nought
        // and a line of two zeroes teaches a reader to stop reading the card.
        // The folder is named, not described « ailleurs »: a reader has to recognise the
        // place before agreeing to leave it.
        QCOMPARE(Words::alreadyElsewhere(u"Elfes"_s, u"Mangas"_s),
                 u"« Elfes » est déjà dans « Mangas » — le ranger ici l’y "
                 u"déplacera."_s);
        QVERIFY(Words::alreadyElsewhere(u"Elfes"_s, QString())
                    .contains(u"déjà dans la bibliothèque"_s));
        QVERIFY(Words::placesCarried(0, 0).isEmpty());
        QCOMPARE(Words::placesCarried(1, 0), u"1 reprise de lecture conservée"_s);
        QCOMPARE(Words::placesCarried(42, 0), u"42 reprises de lecture conservées"_s);
        QCOMPARE(Words::placesCarried(42, 1),
                 u"42 reprises de lecture conservées · 1 perdue"_s);
        QCOMPARE(Words::placesCarried(0, 2), u"2 perdues"_s);
        QVERIFY(Words::withoutStartPage(0).isEmpty());
        QCOMPARE(Words::withoutStartPage(1), u"1 chapitre sans page de départ"_s);
        QCOMPARE(Words::withoutStartPage(3), u"3 chapitres sans page de départ"_s);
        QVERIFY(Words::andMore(0).isEmpty());
        QCOMPARE(Words::andMore(1), u"et un autre"_s);
        QCOMPARE(Words::andMore(12), u"et 12 autres"_s);
        QVERIFY(Words::scanFailed(u"le disque est plein"_s).contains(u"le disque est plein"_s));
    }

    /// A page number with nothing behind it — `pageCount` at zero — is left out exactly like
    /// no page at all, not printed as "Page 3/0".
    void a_page_with_no_page_count_is_left_out_too()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 5.0, 3, 0, std::nullopt), u"Tome 5"_s);
    }

    /// A chapter recorded as an empty string is the same fact as no chapter at all — the field
    /// was present in the JSON and empty, not absent, and the two must read the same.
    void a_blank_chapter_is_left_out_like_a_missing_one()
    {
        QCOMPARE(Words::where(Api::UpNext::Kind::Volume, 12.0, 47, 190,
                              std::optional<QString>(QString())),
                 u"Tome 12 · Page 47/190"_s);
    }

    /// The levels of the model, said in the reader's language — and their icon, which is
    /// the same sign everywhere the application names them.
    void every_level_of_the_model_is_said_and_marked()
    {
        using enum Manifest::Level;

        QCOMPARE(Words::level(Universe), u"univers"_s);
        QCOMPARE(Words::level(Work), u"série"_s);
        QCOMPARE(Words::level(Edition), u"édition"_s);
        QCOMPARE(Words::level(Chapter), u"chapitre"_s);
        QCOMPARE(Words::level(Volume), u"tome"_s);

        QCOMPARE(Words::levelIcon(Universe), u"public"_s);
        QCOMPARE(Words::levelIcon(Work), u"collections_bookmark"_s);
        QCOMPARE(Words::levelIcon(Edition), u"book_2"_s);
        QCOMPARE(Words::levelIcon(Chapter), u"bookmark"_s);
        QCOMPARE(Words::levelIcon(Volume), u"book"_s);
    }

    /// What becomes of a node, in seven sentences and not one more — and the two forms of
    /// the three that end in a past participle. A tree of sixty lines each worded
    /// differently is a tree nobody reads: the same thing has to be said with the same word
    /// wherever it arrives, and « série · sera créé » is the wrong word repeated once per
    /// row.
    void what_becomes_of_a_node_is_said_the_same_way_everywhere()
    {
        using enum Manifest::Level;

        QCOMPARE(Words::willBeCreated(Universe), u"sera créé"_s);
        QCOMPARE(Words::willBeCreated(Work), u"sera créée"_s);
        QCOMPARE(Words::willBeMoved(Universe), u"sera déplacé"_s);
        QCOMPARE(Words::willBeMoved(Work), u"sera déplacée"_s);
        QCOMPARE(Words::wasFiled(Universe), u"envoyé"_s);
        QCOMPARE(Words::wasFiled(Work), u"envoyée"_s);
        QCOMPARE(Words::willBeReplaced(Universe), u"sera remplacé"_s);
        QCOMPARE(Words::willBeReplaced(Work), u"sera remplacée"_s);
        // And the past tense of the same two, which is what a node reads once the commit
        // has run. Said apart from `willBe…` because a tree that still promised « sera
        // créée » over a series that had just been created was the one line on the screen
        // saying the import had not happened.
        QCOMPARE(Words::wasCreated(Universe), u"créé"_s);
        QCOMPARE(Words::wasCreated(Work), u"créée"_s);
        QCOMPARE(Words::wasMoved(Universe), u"déplacé"_s);
        QCOMPARE(Words::wasMoved(Work), u"déplacée"_s);

        // A tome and a chapter never disagree: both are masculine.
        QCOMPARE(Words::wasFiled(Chapter), u"envoyé"_s);
        QCOMPARE(Words::wasFiled(Volume), u"envoyé"_s);

        QCOMPARE(Words::alreadyInTheLibrary(), u"déjà là"_s);
        QCOMPARE(Words::willBeSent(), u"à envoyer"_s);
        QCOMPARE(Words::beingSent(), u"envoi"_s);
        QCOMPARE(Words::failedToSend(), u"échec"_s);
        // A container says what will really happen under it, and counts it.
        // A question while nobody has answered it, an answer once somebody has — the two
        // are not the same fact, and saying the second in both cases made the box under it
        // look like it did nothing.
        // A volume says what it declares itself to be, beside what it is called on disk —
        // and its file name alone when it declares nothing, because an archive without a
        // title is not one titled after its own file name.
        QCOMPARE(Words::fileNamed(u"Assassinat"_s, u"Tome 1.cbz"_s),
                 u"Assassinat · Tome 1.cbz"_s);
        QCOMPARE(Words::fileNamed(QString(), u"Tome 1.cbz"_s), u"Tome 1.cbz"_s);

        // A declaration says what the two disagree about, by field name — the values do
        // not fit on a line and are not the question anyway.
        QVERIFY(Words::declarationDiffers(u"Death Note"_s, {u"résumé"_s, u"arcs"_s})
                    .contains(u"résumé, arcs"_s));
        QVERIFY(Words::declarationDiffers(u"Death Note"_s, {u"résumé"_s})
                    .contains(u"Death Note"_s));
        // One that names itself nothing still says that it differs.
        QVERIFY(!Words::declarationDiffers(QString(), {u"titre"_s}).isEmpty());

        QCOMPARE(Words::holdsReplacements(1, 0), u"1 tome remplaçable"_s);
        QCOMPARE(Words::holdsReplacements(6, 0), u"6 tomes remplaçables"_s);
        QCOMPARE(Words::holdsReplacements(1, 1), u"1 tome sera remplacé"_s);
        QCOMPARE(Words::holdsReplacements(6, 2), u"2 tomes seront remplacés"_s);
        // And a volume says what it would land on — the title it declares, or the fact
        // that the server did not open it, which is not the same as having none.
        QVERIFY(Words::insteadOf(u"Assassinat"_s, 1024, true).contains(u"Assassinat"_s));
        QVERIFY(Words::insteadOf(u"Assassinat"_s, 1024, true).contains(u"1 Kio"_s));
        QVERIFY(Words::insteadOf(QString(), 1024, false).contains(u"non relu"_s));
    }

    /// The level and the state joined the one way French joins them — and the level alone
    /// when there is no state yet, because a trailing « · » on nothing reads as broken
    /// rather than as not yet known.
    void a_node_says_its_level_and_what_it_becomes_together()
    {
        using enum Manifest::Level;

        QCOMPARE(Words::levelAnd(Work, Words::willBeCreated(Work)), u"Série · sera créée"_s);
        QCOMPARE(Words::levelAnd(Universe, Words::willBeCreated(Universe)),
                 u"Univers · sera créé"_s);
        // The level alone still begins like a label: it is one.
        QCOMPARE(Words::levelAnd(Edition, QString()), u"Édition"_s);
        // And the lower-case form is still what a sentence inside another one needs.
        QCOMPARE(Words::level(Edition), u"édition"_s);
    }

    /// One vocabulary for sizes, in binary units correctly named — not the lambda inside
    /// `howFar` that divided by 1024² and called the result « Mo »: wrong by five percent,
    /// and this tree would have said « Gio » right beside it.
    void sizes_share_one_binary_vocabulary()
    {
        QCOMPARE(Words::size(0), u"0 o"_s);
        QCOMPARE(Words::size(512), u"512 o"_s);
        QCOMPARE(Words::size(1048576), u"1,0 Mio"_s);
        QCOMPARE(Words::size(4404019200LL), u"4,1 Gio"_s);
        // A whole library dropped in one folder crosses Gio before a single volume does —
        // the drop this screen exists to accept, and `size` stopped one unit short of it.
        QCOMPARE(Words::size(3298534883328LL), u"3,0 Tio"_s);
        QCOMPARE(Words::size(2251799813685248LL), u"2,0 Pio"_s);

        // `howFar` says the same thing about the same bytes now, in `size`'s own units.
        QCOMPARE(Words::howFar(12 * 1024 * 1024, 134217728), u"12,0 Mio sur 128,0 Mio"_s);
    }

    /// A card once read « 1024 Kio »: comparing the raw byte count to a threshold and
    /// rounding for display afterwards left a window of about a thousand bytes below each
    /// power of 1024 where the value had already rounded up to it but the unit had not
    /// promoted yet.
    void a_value_just_under_a_power_of_1024_promotes_rather_than_rounds_up_to_it()
    {
        QCOMPARE(Words::size(1048575), u"1,0 Mio"_s);              // one byte under 1 Mio
        QCOMPARE(Words::size(1073741823), u"1,0 Gio"_s);           // one byte under 1 Gio
        QCOMPARE(Words::size(1099511627775LL), u"1,0 Tio"_s);      // one byte under 1 Tio
        QCOMPARE(Words::size(1125899906842623LL), u"1,0 Pio"_s);   // one byte under 1 Pio
    }

    /// What a node holds, and how far its checking has got. The count is what tells a wait
    /// from a freeze: « Vérification » alone, fixed for thirty seconds, has already made
    /// the application look crashed.
    void what_a_node_holds_and_where_its_checking_is_are_counted()
    {
        QCOMPARE(Words::nodeHolds(34, 4404019200LL), u"34 tomes · 4,1 Gio"_s);
        QCOMPARE(Words::nodeHolds(1, 1048576), u"1 tome · 1,0 Mio"_s);
        QVERIFY(Words::nodeHolds(0, 0).isEmpty());

        QCOMPARE(Words::checking(12, 68), u"Vérification · 12/68 tomes"_s);
        // A single volume does not pluralise — the file already has what it takes to
        // accord a count, and "0/1 tomes" was the one line here that never asked it to.
        QCOMPARE(Words::checking(0, 1), u"Vérification · 0/1 tome"_s);
        // Nothing to check is not "0/0": it is the word alone.
        QCOMPARE(Words::checking(0, 0), u"Vérification"_s);

        QCOMPARE(Words::waitingToBeChecked(), u"En attente"_s);
        // Three moments, three words. « En attente » used to say all of them, so nothing
        // told a reader whether a verification had finished — the one thing they watch for
        // on a folder of twenty-seven gigabytes.
        QCOMPARE(Words::announcing(), u"Vérifié…"_s);
        QCOMPARE(Words::importStage(Words::Importing::Ready), u"Prêt"_s);
        QVERIFY(Words::announcing() != Words::waitingToBeChecked());
        QVERIFY(Words::importStage(Words::Importing::Ready) != Words::waitingToBeChecked());
    }

    /// What a commit landed, counted kind by kind and never as a bare number.
    ///
    /// A commit that could not install everything is not a failure: the card says which of
    /// the four things happened to how many. Only the volumes actually installed are always
    /// named — the other three appear when they are not zero, because « 0 mal arrivé » on
    /// every successful import is the sentence that teaches a reader to stop reading.
    void what_a_commit_landed_is_said_kind_by_kind()
    {
        QCOMPARE(Words::whatLanded(1, 0, 0, 0), u"1 tome envoyé"_s);
        QCOMPARE(Words::whatLanded(3, 0, 0, 0), u"3 tomes envoyés"_s);

        const QString all = Words::whatLanded(2, 1, 1, 4);
        QVERIFY2(all.contains(u"encore à venir"_s), qPrintable(all));
        QVERIFY2(all.contains(u"mal arrivé"_s), qPrintable(all));
        // Never deleted, only reported — and the word has to say so, rather than leaving a
        // count a reader takes for something lost.
        QVERIFY2(all.contains(u"déjà là et non annoncés"_s), qPrintable(all));
        QCOMPARE(all.count(u"·"_s), 3);
    }

    /// The three things a folder can create, each with the article French gives it.
    ///
    /// The elision is the whole reason this is not in the QML: « créera le UNIVERSE » is
    /// what a binding joining two strings produces, and no `.qml` file can know that
    /// « univers » takes « l’ » and « série » takes « la ».
    void the_word_for_what_a_folder_creates_carries_its_article()
    {
        QVERIFY(Words::willCreate(u"UNIVERSE"_s, u"Terres"_s).startsWith(u"créera l’univers"_s));
        QVERIFY(Words::willCreate(u"WORK"_s, u"Elfes"_s).startsWith(u"créera la série"_s));
        QVERIFY(Words::willCreate(u"EDITION"_s, u"Perfect"_s).startsWith(u"créera l’édition"_s));
        // A kind this client has not learnt stands as it came. The server may name one
        // before the client does — `Api.h`'s own rule — and a hole where the word should be
        // says less than the word nobody translated.
        QVERIFY2(Words::willCreate(u"ARC"_s, u"x"_s).contains(u"ARC"_s),
                 qPrintable(Words::willCreate(u"ARC"_s, u"x"_s)));
    }

    /// Every way an archive can fail to hold together, in French.
    ///
    /// These are the `concerns` the server and the client both produce — the reading a scan
    /// does, run on the file in your hand. They reach a card as a list under its name, and
    /// each one is the only thing that will ever explain why a volume the reader can see on
    /// disk is not the volume they think it is.
    void an_archive_that_does_not_hold_together_says_which_way()
    {
        const QList<QString> said{Words::archiveTooBig(), Words::catalogueMissing(),
                                  Words::sidecarTooBig(),
                                  Words::sidecarCompressedInAnUnknownWay(),
                                  Words::sidecarCouldNotBeInflated()};
        for (const QString &one : said) {
            QVERIFY(!one.isEmpty());
            QVERIFY2(one.endsWith(u'.'), qPrintable(one));
        }
        // Five sentences and five meanings: a list where two of them read the same is a
        // list that says nothing on the line that matters.
        QCOMPARE(QSet<QString>(said.begin(), said.end()).size(), said.size());

        // One of a list, and it is the mark that makes it one — the `.qml` file draws the
        // rows and never puts the bullet there itself.
        QVERIFY(Words::concern(Words::catalogueMissing()).endsWith(Words::catalogueMissing()));
        QVERIFY(Words::concern(u"x"_s).startsWith(u"·"_s));
    }

    /// The stage and how far it has got, joined here — and the stage alone when there is no
    /// « how far » yet, because a trailing separator on nothing reads as broken.
    void a_card_says_its_stage_and_its_progress_under_one_separator()
    {
        const QString far = Words::howFar(12 * 1024 * 1024, 134217728);
        const QString both = Words::stageAnd(Words::importStage(Words::Importing::Sending), far);
        QVERIFY2(both.contains(far), qPrintable(both));
        QVERIFY2(both.contains(Words::importStage(Words::Importing::Sending)), qPrintable(both));
        QCOMPARE(Words::stageAnd(Words::importStage(Words::Importing::Ready), QString()),
                 Words::importStage(Words::Importing::Ready));
    }
};

QTEST_APPLESS_MAIN(WritesFrench)
#include "writes_french.moc"
