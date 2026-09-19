#pragma once

// French, written properly, in one place.
//
// Three habits creep into every interface copied from an American one, and all three are
// visible the moment they are wrong and invisible when they are right:
//
//   · one initial capital per label, not one per word — « Non lues », never « Non Lues » ;
//   · a non-breaking space before `: ; ! ?` and inside « … » ;
//   · a curly apostrophe, and real accented capitals — « À compléter », « Édition Deluxe ».
//
// They are here rather than in QML because a string in a `.qml` file is a string nobody
// tests, and because the same words appear on three screens. Acronyms keep their case: BD,
// never "Bd" — which is why this cannot be a `toUpper` on the first letter and nothing else.

#include "Api.h"
#include "Navigation.h"
#include "Widths.h"

#include <QString>

#include <optional>

namespace Words {

/// A non-breaking space. French puts one before every two-part punctuation mark, and a normal
/// space lets the line break in front of the colon.
extern const QChar Nbsp;

/// "21 tomes", "7 albums", "1 tome". A BD comes in albums and everything else in volumes —
/// which is a fact about the medium, so it is read from the medium and not from the shelf.
QString volumes(int count, std::optional<Api::Medium> medium);

/// The pills, left to right: « Non lues », « En cours », « Terminées ».
QString readStatus(Api::ReadStatus status);

/// « Non lues 12 » — a pill and the count that makes it worth offering. One string and not
/// two fields, because a `.qml` file that puts a label beside a number is a `.qml` file
/// writing French, and the next one will put the number first.
QString pill(const QString &label, int count);

/// The pills, right: « Manga », « BD », « Comics ». The acronym stays an acronym.
QString medium(Api::Medium value);

/// « Tout effacer » — the one command of the filter panel's heading, and the only way out of
/// a selection spread across eight axes that a reader has lost track of.
QString clearEveryFilter();

/// What the panel says when the library gives it nothing to narrow by: six series all unread
/// and all manga leave no axis that cuts anything, and an empty panel with no sentence in it
/// reads as a panel that failed to load.
QString nothingToFilter();

/// « Chercher dans Auteur… » — the field that narrows one long axis, named after the axis so
/// that nobody mistakes it for the one that asks the server a question.
QString searchWithin(const QString &axisTitle);

/// What one axis says when its own field matches none of its values.
QString noValueByThatName();

/// Two vocabularies of its own, and not the models' enumerations.
///
/// `Words` is a leaf: it depends on `Api.h`, which is data, and on nothing that owns a
/// socket. Taking `Settings::Storage` and `Scan::State` here would make the vocabulary
/// depend on the objects that use it — a cycle, and a file no test could link alone. The
/// models map to these, which costs one switch each and keeps the dependency pointing one
/// way.
enum class KeyFrom { Unknown, Environment, Keyring, ProtectedFile };
enum class Scanning { Unknown, Idle, Running, Done, Other };

/// The three things the settings screen says, in the order it says them.
QString theServer();
QString theKey();
QString theScan();
/// « Ce qu'il a trouvé » — the card beside the scan, which is what the scan is for.
QString whatItFound();

/// « Apparence », and the three answers to it. A fourth vocabulary of its own for the
/// reason the other two have one: `Words` is a leaf, and taking `Preferences::Appearance`
/// here would point the dependency back at the object that uses it.
enum class Looks { System, Light, Dark };
QString appearance();
QString appearanceChoice(Looks which);

/// « Bibliothèque connectée », « Pas de connexion ». What a reader needs of a server: not
/// its address, not its version — whether their books are there.
QString connected(bool reachable);

/// What the back button says it does. Named after where it goes rather than « Retour »: a
/// tooltip that repeats the arrow it sits on says nothing the arrow did not.
QString backTo(const QString &destination);

/// Its sections, behind the same pills the search scopes use. Two today; the reader's own
/// preferences will be a third, and adding one is adding a line here.
QString generalSettings();
QString librarySettings();

/// Where the key came from — « Dans l'environnement », « Dans le trousseau », « Dans un
/// fichier protégé ». Worth showing rather than hiding: somebody told their key sits in a
/// file can decide to move it, and somebody who is not, cannot.
QString keyStorage(KeyFrom where);

/// What the scan is doing, and what it found. « Jamais lancé » is not a failure: a library
/// scanned at startup and never since is the ordinary case.
QString scanState(Scanning state);
QString startAScan();

/// « 6 séries, 59 tomes, 546 chapitres » — what the library holds, as the last scan counted
/// it. The counts describe the library and not the work done on it: an unchanged rescan
/// still reports every chapter, because that is how many there are.
QString scanCounts(const Api::ScanCounts &counted);
/// « 3 tomes relus » — the one count that describes the work, and absent when it is zero.
QString reanalysed(int entries);
/// What one kind of finding is called. A word this client has not been taught comes back
/// empty, and the screen shows the items without a heading rather than dropping them.
QString finding(const QString &kind);
/// « 12 chapitres sans page de départ » — a count and no list, because the list would be
/// every chapter.
QString withoutStartPage(int chapters);
QString scanFailed(const QString &why);
/// « et 18 autres » — a list is capped at sixteen, and the cap has to be visible or the
/// screen quietly claims there were sixteen.
QString andMore(int rest);
QString lastScan(qint64 milliseconds);

/// « 19 septembre 2026, 14:32 ». Absolute and never « il y a trois heures »: the question a
/// reader asks of a scan is whether it ran since they changed something, and only a date
/// answers that.
QString moment(qint64 milliseconds);

/// « Répond », « Ne répond pas ». The second is not the same as having no address at all,
/// and the screen says which — one is fixed by reading, the other by looking at the machine.
QString answering(bool reachable);
QString libraryHolds(int series);
QString apiVersion(int api, int format);
QString sharedFolder(bool there);

/// The eight axes a library can be narrowed by, worded for the panel that offers them. The
/// contract's own spelling goes in — `read`, `medium`, `universe`… — because that is what the
/// server answers with and what goes back on the wire; a word this client has not been taught
/// comes back empty, and an axis with no name is not drawn.
QString axis(const QString &name);

/// « En parution », « Terminée » — whether the work is still being published, which is not
/// the same question as whether *you* have read it. Worded apart from `readStatus` on
/// purpose: « En cours » under two different headings is one word doing two jobs.
QString editionStatus(const QString &word);

/// « Français », « Japonais » — a language tag as a reader's word for it. Read from the
/// system rather than from a table here: a table of every tag a library might carry is a
/// table that is wrong for the one tag somebody actually has.
QString language(const QString &tag);

/// « Nom », « Ajout », « Nombre de tomes », « Dernière lecture » — the four the bar
/// offers, and the only place they are worded. The value is the information, so the bar shows
/// it in full rather than an icon alone.
QString sortOrder(Api::Sort value);

/// The criterion and its effective direction, compact enough for the application bar.
QString sortValue(Api::Sort value, bool reversed);

/// « Trier : Nom » — with the space that will not break before the colon.
QString labelled(const QString &label, const QString &value);

/// « Tome 12 · Page 47/190 · Chapitre 98 ».
///
/// Each segment is its own label, so each begins with a capital. Segments with nothing to say
/// are left out rather than shown empty: a card reading "Page /" is worse than a shorter card.
QString where(Api::UpNext::Kind kind, std::optional<double> number, std::optional<int> page,
              int pageCount, const std::optional<QString> &chapter);

/// « T12 · Page 47/190 » — the same line for a band that has lost the width for the long one.
/// The chapter segment goes rather than shrinks, and only a volume abbreviates.
QString whereShort(Api::UpNext::Kind kind, std::optional<double> number, std::optional<int> page,
                   int pageCount);

/// « Reprendre » while you are inside an entry; « Continuer » for the next unread entry.
QString resumeAction(Api::UpNext::Reason reason);

/// « Fichiers · 17 » — the heading over what a search found that is not a series. The count
/// is the whole of it: a file is a line, and the lines say what they are by being lines.
QString files(int count);

/// The three scopes of a search. Counts stay in the labels because a scope named only
/// « Séries » makes the reader enter it merely to learn whether anything is there.
QString overview();
QString series(int count);

/// The overview never unfolds in place: these actions change scope, so the page above the
/// series cannot turn into sixty file rows and push the series several screens away.
QString seeAllSeries(int count);
QString seeAllFiles(int count);

/// The second line of a file hit: its series, containing volume and page count, with absent
/// facts omitted. A chapter called « Assaut » is not useful without « Tome 8 » beside it.
QString fileContext(const Api::Hit &hit);

/// « Voir les 12 autres », and « Voir l'autre » when there is one. It unfolds in place, so
/// the wording says how many are hidden rather than where they would be.
QString seeTheOthers(int remaining);

/// « Vouliez-vous dire Tsugumi Ōba ? » — the approximate guess, and only when the search
/// found nothing at all. A guess shown like an exact hit costs more trust than finding
/// nothing, so it is phrased as a question, with the space French puts before the mark.
QString didYouMean(const QString &name);

/// « Rechercher une série, un tome, un chapitre… », and the short form for a bar that has
/// lost the width for it. The long one names the three things the search can find, which is
/// the only place a reader is told.
QString searchHint();
QString searchHintShort();

/// « Effacer la recherche » — what the cross in the field does. Never drawn: it is what a
/// screen reader announces, and what nothing else would say.
QString clearTheSearch();

/// « Filtrer » — the bar's own command, and where the pills fold when the window narrows.
QString filter();

/// « Aucune série ne porte ce nom. » — under the lines, when a search found files and no
/// series at all. The grid disappears rather than stay empty, and this says why.
QString noSeriesByThatName();

/// « aucun résultat dans manga · 3 sans les filtres » — the sentence under the field when a
/// search finds nothing behind a lit pill. Without it the screen just looks broken.
QString nothingHere(const QStringList &pills, int withoutThem);

/// The screen's own name — « Étagère », « Série », « Lecteur », « Santé », « Réglages » —
/// which is otherwise the one string this client would put in a `.qml` file and never test.
QString destination(Navigation::Destination value);

/// The band a window is in, said the way a person reads it — « Large », « Moyenne »,
/// « Étroite » — for the same reason `destination` exists: so nothing switches on the enum
/// from inside QML.
QString band(Widths::Band value);

// ——— What Leaf says when something goes wrong ———————————————————————————————
//
// These sentences were `tr(...)` literals at their call sites, which meant English on the
// screen: no catalogue is installed and none is planned, so `tr` hands back its source
// string. It showed — « The server could not be reached — Connexion refusée », half of it
// Qt's French and half ours. They live here now, with the rest of the French, and for the
// reason at the top of this file: a sentence written far from `Words` is a sentence nobody
// proofreads.

/// Which screen was left with nothing when the client could not set itself up. One tail per
/// screen and not one sentence for all of them: a reader who opened the shelf and a reader
/// who typed in the field are owed different halves of it.
enum class Asking { Shelf, Search, Filters, Resume, State };

/// « Leaf n'a pas pu s'installer : il n'y a rien à afficher. »
QString notSetUp(Asking what);

/// « Leaf ne sait pas où est votre bibliothèque. » — no address at all, which is a setup
/// that never happened rather than a server that is down.
QString noLibrary();

/// « Le serveur est injoignable — connexion refusée ». The tail is Qt's own words for the
/// network error, which Qt does translate; only Leaf's half of the sentence was English.
QString unreachable(const QString &why);

/// « Le serveur a répondu quelque chose d'illisible. » — an answer that is not the JSON its
/// own contract promises.
QString unreadableAnswer();

/// « La clé a été refusée. », and what the server said after it when it said anything.
QString keyRefused(const QString &said);

/// « Trop de clés fausses ont été essayées. Attendez 30 secondes. » — the server's own
/// `Retry-After`, said rather than counted down, because nothing here ticks.
QString tooManyWrongKeys(const QString &seconds);

/// « Il n'y a rien de tel ici. » — a 404 the server did not word itself.
QString noSuchThing();

/// « Le serveur a répondu 500. », with its own sentence after the code when it sent one.
QString serverAnswered(int status, const QString &said);

/// « Encore 12 secondes avant de redemander. » — Leaf stopped asking on purpose, and says
/// so, because a screen that has simply gone quiet reads as a screen that is broken.
QString waitingBeforeAsking(int seconds);

/// « Trop de demandes attendent déjà que Leaf ouvre votre bibliothèque. »
QString tooManyWaiting();

/// « Un paramètre se donne à part du chemin, pas collé dedans. » — a caller's mistake and
/// not a reader's, but it comes out on the same screens as the rest.
QString queryBelongsApart();

/// What the setup is missing, naming the file that would hold it. Three sentences and not
/// one: told that both are missing a reader fixes both, told only the key is, they look for
/// the one thing.
QString nothingConfigured(const QString &file);
QString noAddress(const QString &file);
QString noKey(const QString &file);

/// « … est lisible par d'autres que vous, il n'a donc pas été lu du tout. » A key file
/// anyone can read is a key already given away; Leaf refuses it rather than use it, and
/// says the one command that fixes it.
QString readableByOthers(const QString &path);

} // namespace Words
