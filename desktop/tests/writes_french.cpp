// The three things French asks for that are invisible when present and obvious when missing.
//
// A test rather than care, because care does not survive the twentieth label. The non-breaking
// space in particular cannot be seen in a diff, cannot be seen in the source, and can only be
// seen on screen once the window is narrow enough to break the line in front of the colon.

#include "Words.h"

#include <QSet>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

class WritesFrench : public QObject
{
    Q_OBJECT

private slots:
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
                          Words::readableByOthers(u"leaf.conf"_s)};
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
};

QTEST_APPLESS_MAIN(WritesFrench)
#include "writes_french.moc"
