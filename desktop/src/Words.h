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
#include "Preferences.h"
#include "Toasts.h"
#include "Manifest.h"
#include "Navigation.h"
#include "Orders.h"
#include "Shape.h"
#include "Widths.h"

#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QStringList>

#include <optional>

namespace Words {

/// A non-breaking space. French puts one before every two-part punctuation mark, and a normal
/// space lets the line break in front of the colon.
extern const QChar Nbsp;

/// A narrow non-breaking space, the one French sets before `%` — thinner than `Nbsp`, and as
/// unbreakable: « 200 % » split at the end of a line leaves a sign opening the next.
extern const QChar NarrowNbsp;

/// « Level.01 : Nom », « Chap. 13 : Le réveil » — a chapter wherever it is named: the label
/// the server settled (the entry's own, the edition's pattern, or « Chap. n »), a
/// non-breaking space and a colon, then the title. The label alone when the title is empty
/// or only says the label again; the title alone when the label is empty, which is how a
/// file asks for a bonus to be called by its title. Built once here so that the bar, the
/// panel, the bubble, the band and the search say a chapter the same way.
QString chapterName(const QString &label, const QString &title);

/// "21 tomes", "7 albums", "1 tome". A BD comes in albums and everything else in volumes —
/// which is a fact about the medium, so it is read from the medium and not from the shelf.
QString volumes(int count, std::optional<Api::Medium> medium);
/// What a series holds, under its tile: « 12 tomes », or « 120 chapitres » when it holds
/// no volume and files that are all chapters — a webtoon is downloaded as chapters and read
/// « 0 tome » for as long as the line counted volumes only (test of 24/11/2026).
QString holdings(const Api::Series &one);

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

/// What an empty shelf says, headline then sentence. The shelf of a library with nothing in
/// it drew a bar, a row of pills and four hundred and fifty pixels of bare paper in the audit
/// of 23/10/2026: not one word to tell an empty library from a screen that failed to draw.
QString emptyShelfHeadline();
QString emptyShelf();

/// What a shelf emptied by its own filters says — not the same thing as a library with
/// nothing in it, and a reader told « vide » would go looking for the files.
QString nothingLeftHeadline();
QString nothingLeft();

/// The headline over what went wrong when the shelf could not fill, said above the trouble
/// itself: the trouble names the cause, this names what it cost.
QString shelfTroubleHeadline();

/// « Chercher dans Auteur… » — the field that narrows one long axis, named after the axis so
/// that nobody mistakes it for the one that asks the server a question.
QString searchWithin(const QString &axisTitle);

/// « auteur » as the field names it: quoted, since the axis is a name and not a noun with an
/// article to agree — « Chercher dans auteur… » was no French.
QString quotedAxis(const QString &axisTitle);

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
/// « Ce qu’il a trouvé » — the card beside the scan, which is what the scan is for.
QString whatItFound();

/// « Apparence », and the three answers to it. A fourth vocabulary of its own for the
/// reason the other two have one: `Words` is a leaf, and taking `Preferences::Appearance`
/// here would point the dependency back at the object that uses it.
enum class Looks { System, Light, Dark };
QString appearance();
/// The card of the reader's own habits — layout, fit, backdrop, motion — as the general
/// settings hold them: what a volume opens with, before it is changed from inside the reader.
QString readerDefaults();
QString appearanceChoice(Looks which);

/// « Bibliothèque connectée », « Pas de connexion ». What a reader needs of a server: not
/// its address, not its version — whether their books are there.
QString connected(bool reachable);

/// What the back button says it does. Named after where it goes rather than « Retour »: a
/// tooltip that repeats the arrow it sits on says nothing the arrow did not.
QString backTo(std::optional<Navigation::Destination> destination);

/// Its sections, behind the same pills the search scopes use. Two today; the reader's own
/// preferences will be a third, and adding one is adding a line here.
QString generalSettings();
QString librarySettings();

/// Where the key came from — « Dans l’environnement », « Dans le trousseau », « Dans un
/// fichier protégé ». Worth showing rather than hiding: somebody told their key sits in a
/// file can decide to move it, and somebody who is not, cannot.
QString keyStorage(KeyFrom where);

/// What the scan is doing, and what it found. « Jamais lancé » is not a failure: a library
/// scanned at startup and never since is the ordinary case.
QString scanState(Scanning state);
QString startAScan();
QString theCache();
QString purgeTheCache();
QString exportReading();
QString exportExplained();
/// « Export enregistré : /chemin/leaf-lecture.json »
QString readingExported(const QString &path);
QString readingExportName();
QString purgeExplained();
/// « Cache vidé : 12 fichiers, 3,4 Mio. », or that there was nothing to empty.
QString cachePurged(qint64 files, qint64 bytes);

/// « 6 séries, 59 tomes, 546 chapitres » — what the library holds, as the last scan counted
/// it. The counts describe the library and not the work done on it: an unchanged rescan
/// still reports every chapter, because that is how many there are.
QString scanCounts(const Api::ScanCounts &counted);
/// « 3 tomes relus » — the one count that describes the work, and absent when it is zero.
QString reanalysed(int entries);
/// « 42 reprises de lecture conservées · 1 perdue » — said only when a scan had any to
/// carry, which is the one after a library stops being identified by its folder names.
/// A line of two zeroes on every other scan teaches a reader to stop reading the card.
QString placesCarried(int carried, int lost);
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
/// « Dernières notifications » and what it says before there is one: the list of what the
/// bubbles said lately, in the notifications tab.
QString notificationHistory();
QString nothingToldYet();
/// « Tout voir (12) » and « Replier »: a finding of the scan is three lines until asked for
/// the rest, so that nine of them do not stack into a wall.
QString seeAllFindings(int count);
QString foldFindings();
QString lastScan(qint64 milliseconds);
/// « Commencé : 19 octobre 2026, 14:02 » — the line under « En cours… ». Not `lastScan`:
/// a scan under way has no end yet, the server says so with an empty `finishedAt`, and that
/// read as « Jamais lancé » under the word saying one was running.
QString scanStarted(qint64 milliseconds);

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

/// « En parution · 30 albums annoncés » — the state of the run beside what the publisher
/// announces, one line, because « En parution » alone leaves « out of how many? » unanswered.
/// The space before « · » does not break.
QString runAnnounced(const QString &status, int declared, std::optional<Api::Medium> medium);

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
/// `chapter` is the chapter's whole name, as `chapterName` builds it, shown as the server
/// settled it. A chapter entry is named by it rather than « Chapitre 98 » when it is known.
/// `page` counts from 1, as the reader does — the caller turns the recorded index into it.
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

/// The first line of a file hit: a chapter named by `chapterName`, anything else by its label.
QString hitName(const Api::Hit &hit);

/// « Voir les 12 autres », and « Voir l’autre » when there is one. It unfolds in place, so
/// the wording says how many are hidden rather than where they would be.
QString seeTheOthers(int remaining);

// ——— La fiche d'une série ————————————————————————————————————————————————————

/// A number as French writes it: « 3,5 » and not « 3.5 », and « 12 » and not « 12,0 ». Volume
/// numbers are halves as often as not — a 3.5 is a side story — so the column of a list is a
/// column of these. Exported because a list of volumes is the first screen to show a number
/// with no word in front of it.
QString number(double value);

/// The three tabs, in the order they are drawn. An enumeration and not three functions,
/// because the tab bar repeats over them and a fourth would otherwise be a fourth call site.
enum class Tab { Volumes, Description, Elsewhere };
QString tab(Tab which);

/// « Collectif · Soleil · BD · En cours » — who made it and what it is, in one line. Absent
/// facts are left out rather than shown empty, the way the resume band does it.
///
/// `people` are the names in the order the page ranks them; left empty, they are read off the
/// series' credits. With a `link`, the words are written for a rich text and « et N autres »
/// is a link to it.
QString makers(const Api::Series &one, const QStringList &people = {}, const QString &link = {});
/// « et 9 autres » — the people a line does not name.
QString andOthers(int count);
/// `most` names and « et N autres » — two in the header, three a line of the description —
/// but never « et 1 autre », since the name it would hide takes no more room than the words
/// hiding it. With a `link`, written for a rich text and the count a link to it.
QString namesWithin(const QStringList &names, int most, const QString &link = {});
/// « Scénario : Jul · Dessin : Achdé » under a volume's title — empty when the volume is
/// credited with its series' default on both counts.
QString volumeCredits(const Api::Entry &one);
/// « Les auteurs de Lucky Luke » and « 11 personnes · rangées par nombre d’albums » — the
/// window listing everyone.
QString peopleTitle(const QString &work);
QString peopleCount(int count, std::optional<Api::Medium> medium);

/// One line of who made a work: « Auteur », « Scénario » or « Dessin », and whom.
struct CreditLine {
    QString label;
    QStringList names;
};
/// Who wrote and who drew, each person once: in both lists, an « Auteur » — « Auteurs » for
/// several, since it names people —; in one, « Scénario » or « Dessin », which name a role
/// and do not agree. Lines in that order, empty ones left out, names in the order they came;
/// two spellings the search folds together are one person, spelt as first met.
QList<CreditLine> creditLines(const QStringList &authors, const QStringList &artists);

/// Under a series' credit in its window: « Pour les albums ne précisant pas de scénariste ».
/// What the field holds is the default its volumes draw on, not the union of what they name,
/// and nothing else on the window said so. `drawing` for « Dessin ».
QString creditDefault(bool drawing, std::optional<Api::Medium> medium);

/// « 29 albums · 5 arcs · Tout public · Gauche à droite » — what it weighs. The count of
/// volumes follows the medium, so a BD is counted in albums; the rest is left out when it is
/// not recorded.
QString weights(const Api::Series &one);

/// « 2 éditions » — what the switcher offers. Never drawn below two: there is nothing to
/// choose between when a work has one edition, and an implicit edition has no name to show.
QString editions(int count);

/// « 5 arcs », and nothing at all at nought.
QString arcs(int count);

/// « Non lu ». Written in full on a line, where an empty cell reads as an information that is
/// missing rather than as a state — and never on a cover, where thirty of them over thirty
/// illustrations would be thirty stains.
QString neverRead();

/// « ×6 » — how many times a file has been finished, shown from two. A series that is done
/// would otherwise carry « ×1 » on every line for no news at all.
QString timesFinished(int times);

/// « Manquant » · « Manquants ». The label agrees, and the value below enumerates.
QString missingLabel(int count);

/// « Tome 7 », « Tomes 7, 9, 12, 18 » — commas to the end and no « et »: this is a list of
/// identifiers, not a sentence, and « 7, 9, 12, 21, 24 et 30 » is a preciosity on a series
/// with holes in it.
QString missingVolumes(const QList<double> &numbers);

/// « 29 sur 30 » — what this library holds of what the edition runs to.
QString heldOutOf(int owned, int ceiling);

/// « Dans cette bibliothèque » — the heading over the only block that speaks about you rather
/// than about the work.
QString inThisLibrary();

/// The labels of the description, each a label and not a sentence.
enum class Fact {
    Writers, Artists, Publisher, Collection, Language, Kind, Direction, Status, Age, Colour,
    Held, Read, FirstReceived, LastReceived
};
QString fact(Fact which);

/// « Gauche à droite », « Droite à gauche », « Verticale ».
QString readingDirection(Api::ReadingDirection value);

/// « Couleur » · « Noir et blanc » — positive form on both sides, never « pas en couleur ».
QString colour(bool coloured);

/// « 4 · le 5ᵉ en cours » — how many of an edition are finished, and whether one is open. The
/// ordinal is worth the trouble: « le 5 en cours » reads as a quantity.
QString readEntries(int finished, bool oneOpen, int which);

/// « tomes 1 à 4 », « chapitres 42 à 68 » — an arc's range, in its own unit. The range is
/// written beside the name and never inferred from where the next separator falls: an arc
/// does not end where the one below it starts, which is the whole reason it is a range.
QString arcRange(Api::Arc::Unit unit, double from, double to);

/// « chapitres 64 à 68 » — a stretch of chapters inside one volume, shown under the line of
/// the volume an arc crosses. Two of them and a marker between, rather than the list of the
/// chapters themselves: a range is not something one opens, so `startPage` — null on most
/// libraries — is missed by nobody.
QString chapterRange(double from, double to);

/// « à partir du chapitre 69 » — the same, for the last volume of an edition, which has no
/// neighbour to give it an upper bound. Open, the way the format writes its ranges open.
QString fromChapter(double from);

/// « albums 1 à 7 » — the stretch of a work a step of a reading order covers, in the unit the
/// step counts in. A step with no bounds covers the whole work and says nothing, which the
/// contract calls the common case.
QString stepRange(const Api::ReadingStep &step);

/// « En liste » and « En grille » — the two ways of looking at the volumes of an edition.
/// Each button says what it *does*, not what it acts on: two buttons side by side both
/// labelled « Tomes » tell a reader nothing about which is which.
QString viewAsList();
QString viewAsGrid();

/// « les tomes » — the axis the field of a series page narrows, handed to `searchWithin`
/// rather than spelled into a sentence of its own. One rule writes « Chercher dans … », and
/// a second copy of it would be a second thing to keep in step.
QString volumesAxis();

/// « Aucun tome ne porte ce nom. » — said under an emptied list rather than leaving a blank,
/// the way the shelf says it of a search that found nothing.
QString noVolumeByThatName();

// ——— Where to go from a series that is not this series ————————————————————————————

/// « La même œuvre, autrement » and « Dans l'univers » — the two blocks of the last tab,
/// titled apart because they are not the same intention: changing edition is this book in
/// another binding, walking the universe is other books.
QString sameWorkOtherwise();
QString inTheUniverse();

/// « Terres d'Arran · 3 autres » — the universe a series belongs to and how many other series
/// it holds, on one line. The count drops with its separator when a way through is walked:
/// that walk names its own steps, and a count beside them would be counting something else.
QString universeLine(const QString &name, int others);

/// « Hors parcours » and « 2 séries » — what a chosen way through does not name. Under its
/// own heading rather than slipped onto the end, where it would read as the end of the walk.
QString outsideTheOrder();
QString seriesCount(int count);

// ——— What the three dots offer, and what a deletion says before it happens —————————

/// The entries of the menu that commands an object where it is drawn. Two lists and not one:
/// a series is not a file, and « enregistrer une copie » of thirty volumes is not a thing.
///
/// Every one of them names what it acts on — « ce tome », « la série » — because the menu is
/// opened from a tile among fifty and the pointer has moved by the time it is read.
enum class Command {
    MarkSeriesRead,
    MarkSeriesUnread,
    ReimportSeries,
    AddEditions,
    EraseSeries,
    MarkEntryRead,
    MarkEntryUnread,
    ReimportEntry,
    SaveACopy,
    EraseEntry,
    ShowInfo,
    ChooseDirection,
    EditSeries,
    EditEntry,
    Rescan,
    ReplaceChapter,
    /// An edition is not « la série » on the shelf: its tile reads « Dragon Ball · Perfect
    /// Edition », and a menu that says « la série » about it names something else.
    EraseEdition,
    EditEdition,
    /// The export asks a folder every time, and says what it takes: this edition or volume,
    /// the whole work, the whole universe.
    Export,
    ExportWork,
    ExportUniverse,
};
QString command(Command which);

/// « 3 séries sélectionnées », « 2 tomes sélectionnés » — what a selection holds, said on its
/// bar. One is singular like nought: French counts 0 and 1 in the singular.
QString selectionCount(int count, bool volumes);

/// « Marquer la sélection comme lue » / « …comme non lue » — one sentence for series and for
/// volumes, since « sélection » is the same word for both and the bar says which it holds.
QString markSelection(bool read);

/// « Désélectionner » — empties the selection, and is the word of the bar's last button.
QString clearSelection();

/// « Supprimer le tome 23 ? » — the question a deletion asks, naming the one thing that goes.
QString eraseEntryQuestion(double number);

/// « Supprimer « Elfes » ? » — with its guillemets, because an edition's name is a name and
/// « Supprimer Elfes ? » reads as a sentence about elves.
QString eraseSeriesQuestion(const QString &name);

/// « La Dryade — 54 pages, 48,0 Mio » — what goes, and what it weighs. The title stands alone
/// when a volume has none, rather than a dash leading nowhere.
QString whatGoes(const QString &title, int pages, qint64 bytes);

/// « Fichier : Elfes - Tome 23.cbz » — the name alone. `Entry.file` is « the file name alone,
/// never a path », so the modal says the name and does not invent the place.
QString whichFile(const QString &name);

/// « Il restera 28 albums, et le 23 rejoindra les manquants. La numérotation ne bouge pas :
/// le 24 reste le 24. »
///
/// The sentence changes with the volume, and that is the whole reason it is composed rather
/// than written once: erasing the last or the first leaves no hole — the ceiling comes down,
/// the floor goes up — while erasing one in the middle leaves one for ever. A confirmation
/// that said the same thing every time would only ever be clicked.
QString whatWouldRemain(int remaining, std::optional<Api::Medium> medium, double number,
                        bool between, std::optional<double> nextHeld);

/// « 29 fichiers, 1,4 Gio. Les 4 tomes lus et la position dans le 5ᵉ seront oubliés avec
/// eux. » — what a whole edition takes with it. A deletion that speaks only of files hides
/// half of what it carries away.
QString whatAWholeEditionTakes(int files, qint64 bytes, int finished, bool oneOpen, int which);

/// « Le fichier est supprimé du disque. Leaf n'a pas de corbeille — un scan ne le ramènera
/// pas. » Plural when a whole edition goes, because thirty files are not a file.
QString noTrash(bool several);

/// « L'édition Intégrale de la même œuvre n'est pas touchée. » — said only when there is one.
/// « Supprimer Elfes » is ambiguous the day two editions carry that name, and that is exactly
/// the day one must not be wrong.
QString otherEditionUntouched(const QString &name);

/// « Tapez « Supprimer » pour confirmer » — the only protection that works against a reflex,
/// and it is worth its weight on thirty files where it would be ceremony on one. The word is
/// the button's own, so what is typed is what is pressed.
QString typeToConfirm(const QString &word);

/// « Supprimer » — the button that does it, and the one word on the screen painted in the
/// alert colour.
QString eraseButton();

/// « Il n'y a rien à supprimer ici. » — said when the modal was opened about nothing at all,
/// which is a client fault and not a server one. It is still said rather than swallowed: a
/// confirmation that comes up empty and does nothing is worse than one that explains itself.
QString nothingToErase();

/// The headline of each thing that warns. The second line is the work's own words — an
/// import says `whatLanded` and a refusal says what the server said — so only the first line
/// is written here, and it names what happened rather than repeating why.
QString couldNotSend(const QString &subject);
QString scanEnded();
QString scanStopped();
QString copyKept();
QString commandRefused();
/// « Cet appareil est déjà relié à une bibliothèque : déliez-le d’abord pour en changer. » —
/// what a clicked link is answered when there is already a library, which it never replaces.
QString alreadyLinked();
/// « ISBN copié » — the one bubble that answers a click rather than reports on something
/// unseen: a copy to the clipboard changes nothing on the screen, so without it the click on
/// the volume's ISBN looked like a click on nothing.
QString isbnCopied();
/// « Il manque le module d’images WebP de Qt », and what to install. Said at startup when Qt
/// cannot read WebP: every cover and every resized page is WebP, and without the plugin the
/// desktop showed a shelf of « Format d'image non supporté » (22/10/2026) — an
/// error on each image and nowhere a word about the one package that was missing.
QString webpMissing();
QString webpMissingRemedy();

/// « Voir », « Réessayer », « Ouvrir le dossier » — the one thing a bubble may offer besides
/// its cross. Nothing for the ones with nothing to propose: a button that only closes is the
/// cross with a word written on it.
QString bubbleOffer(Toasts::Offer what);

/// « 4 autres » — what is said in place of the fourth bubble and everything after it. Forty
/// volumes that fail do not make forty bubbles.
QString moreBubbles(int how);

/// « Imports terminés » and « un dossier installé, un fichier rangé » — the four families a
/// switch can answer for, and what each one covers. The detail is not decoration: « Scans
/// terminés » off does not silence a scan that *failed*, and a reader who is not told that
/// finds out by being surprised.
QString warns(Preferences::Warns which);
QString warnsAbout(Preferences::Warns which);

/// « En bas à droite » — where the bubbles are laid. The picture says where; the word is what
/// a screen reader announces and what makes the setting findable by searching for it.
QString corner(Preferences::Corner where);

/// « Ce qui vous prévient » and « Où les bulles apparaissent » — the two cards of the third
/// section of the settings, and the section's own name.
QString notificationSettings();
QString whatWarnsYou();
QString whereBubblesAppear();
/// The three columns of the list: « Événement » over the lines themselves, then « Bulle » and
/// « Bureau » over the two switches. The first was missing, and the two that were there made
/// the column of names look like the one thing that needed no heading.
QString eventColumn();
QString bubbleColumn();
QString desktopColumn();

/// What an export says, from the question of where to the bubble that ends it. The folder is
/// asked every time, so its title is the whole of what the picker needs to be told.
QString exportFolderTitle();
QString exportConflictTitle();
/// « 3 éléments existent déjà dans ce dossier, par exemple « Tome 01.cbz ». » — what stands in
/// the way, with one of them named so that the reader knows whether it is the right folder.
QString exportConflict(int count, const QString &example);
QString exportReplace();
QString exportComplete();
/// « Export de « Dragon Ball » » — the bar's name for what is on its way.
QString exportingNow(const QString &name);
/// « 3 séries » — the name of an export of a selection, which has none of its own.
QString exportOfSeveral(int count);
/// « Export terminé », and under it the folder's name and the sidecars the disk did not have.
QString exportDone();
QString exportDoneDetail(const QString &name, const QStringList &missing);
QString exportStopped();
QString nothingToExport();
/// « Il manque 1,2 Gio sur ce disque : 4,1 Gio à écrire, 2,9 Gio libres. »
QString notEnoughRoom(qint64 needed, qint64 free);
QString exportUnsafePath(const QString &path);
QString couldNotMakeFolder(const QString &path);

/// « Tome 5.cbz n'a pas pu être écrit. » — a copy that did not reach the disk. Named by the
/// file it was going to be, because that is the only part of it the reader chose.
QString couldNotWrite(const QString &name);

/// « 2 fichiers n'ont pas pu être supprimés : Tome 4.cbz, Tome 9.cbz » — what the server
/// refused. Named, because « the caller is told exactly which files are still there instead
/// of working it out from a count ».
QString wouldNotGo(const QList<QString> &refused);

/// « 29 albums · ici » — what a tile says under its name, with the mark that it is the one
/// being read. Appended to that line rather than drawn beside it: a tile has one grey line,
/// and a mark floating over a cover would have to be placed again at every size.
///
/// Lower case and in full: it marks a list one looks at, not an item one chose — the
/// switcher above, which *is* a choice, marks its own with a tint instead.
QString hereToo(const QString &detail);

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

/// What a search that found nothing at all says, alone on its screen: a sentence, where the
/// screen drew two headings reading « 0 » over bare paper before (23/10/2026).
QString nothingFound();

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

// ——— Le lecteur ——————————————————————————————————————————————————————————————

/// « Tome 12 », « Chapitre 108 » — the entry the reader holds, named the way the resume band
/// names it, from the same spelling: two screens saying « Tome 12 » and « T. 12 » of one
/// book read as two books.
QString readerVolume(double number);
QString readerChapter(double number);

/// « Lire le tome 13 », « Lire le chapitre 108 » — the one button of the end card, saying
/// what it opens. A chapter that arrived on its own is not a volume, and « Lire le tome 108 »
/// would send the reader looking for a book that does not exist.
QString readOn(Api::Entry::Kind kind, double number);

/// « Tome 12 terminé » — what the end card says first. A chapter has its own, for the
/// button's reason.
QString finishedVolume(double number);
QString finishedChapter(double number);

/// « dernier tome de la série », « dernier chapitre de la série » — said after the line
/// above when nothing follows, so that a card with no button reads as an end and not as a
/// suite that failed to load.
QString lastOfSeries(Api::Entry::Kind kind);

/// The end card's first line: « Tome 12 terminé », and « Tome 21 terminé · dernier tome de
/// la série » when nothing follows. The space before « · » does not break, so the second
/// half never opens a line on its separator.
QString ending(Api::Entry::Kind kind, double number, bool last);

/// « Tome 12 · Chap.099 : Coup de sifflet » — the second line of the reader's bar: the book,
/// then the chapter the page on screen belongs to, named by `chapterName`. A book with no
/// chapter known is its name alone, and a book that is its chapter is named once.
QString readerWhere(const QString &entry, const QString &chapter, const QString &title);

/// « Chap.099 : Coup de sifflet · page 47 » — what the bubble over the progress bar says of
/// the page under the pointer while it slides, the chapter named by `chapterName`. `page`
/// counts from 1, as a reader does.
QString readerScrubbing(const QString &chapter, const QString &title, int page);

/// « : Coup de sifflet » — what follows a chapter's label on the line under the progress bar,
/// where the label is drawn apart, stronger: the second half of `chapterName`. Empty for a
/// chapter with no title and for one whose title only repeats `label`; the title alone, with
/// no colon, for a chapter with no label, so the line still names it. The space before « : »
/// does not break.
QString readerBesideLabel(const QString &label, const QString &title);

/// « Chap.107 · fin du tome » — the other end of that line: the last chapter of the volume by
/// its label, by its title when it has no label, and « fin du tome » alone when it has neither.
QString readerVolumeEnd(const QString &label, const QString &title);

/// « / 190 », drawn fainter after the page number. The slash never ends a line nor opens one.
QString readerOutOf(int count);

/// « 200 % » — the zoom reached, said a second in the bar at the foot of the page. `level` is
/// 2 for 200 %; a pinch stops anywhere, and is said to the nearest whole percent.
QString readerZoom(qreal level);

/// « Zoom temporaire · Échap » — said in the bar at the foot while a pinch magnifies a strip:
/// nothing of it is kept, and the reader is told so, and how to leave it.
QString readerLoupe();

/// « sur 120 », under « Chapitre 12 » in a webtoon's band: how many chapters the series has.
QString readerAmong(int count);

/// « Agrandir », « Réduire » — the zoom steps of a webtoon's band, for the bubble and for
/// whoever reads the screen aloud.
QString readerZoomStep(bool larger);

/// « Lecture automatique », and « Arrêter la lecture automatique » while it runs: the A of a
/// webtoon's band.
QString readerAutoRead(bool running);

/// « 98 à 106 » — the chapters a volume holds, by their numbers; one number alone when the
/// stretch is a single chapter. The word « Chapitres » is the line's, not this.
QString readerChapterSpan(double from, double to);

/// « Tome 12 · Le temps des examens » — the head of the volume's panel. A volume with no title
/// is its name alone, and so is one whose title is its name again, in whatever case.
QString readerVolumeHead(const QString &entry, const QString &title);

/// The names of the lines in the volume's panel.
enum class ReaderFact { Series, Pages, Chapters, Published, Isbn, Read };
QString readerFact(ReaderFact which);

/// The names of the lines of a file's information that the volume's panel does not have: the
/// file's own name, what it weighs, and when it arrived.
enum class FileFact { File, Size, Added };
QString fileFact(FileFact which);

/// The words of the two editing windows, by name: what a field is called, how a list is
/// typed, and what saving a volume costs. One map for the lot (`Editing.words`) rather
/// than a function a field.
QVariantMap editing();

/// « Cliquer pour copier », under the ISBN drawn as a barcode in the volume's panel.
QString readerCopyIsbn();

/// « 4 mars 2015 », « 1er mars 2015 » — a date as the server writes it (« 2015-03-04 »). What
/// does not read as a date is returned as it came, since a wrong date is worse than a plain one.
QString readerPublished(const QString &iso);

/// « page 47 / 190 », or « terminé » for a volume read to its end and not opened since.
QString readerReadAt(int page, int count, bool finished);

/// « p. 3 » — where a chapter begins, from 1.
QString readerChapterPage(int page);

/// The reader's buttons, said by their tooltips and to whoever reads the screen aloud.
QString readerVolumePanel();
QString readerSettings();
QString readerFullScreen(bool on);
QString readerPage(bool forward);
QString readerChapterStep(bool forward);

/// The settings card. Two groups, named for what they reach: what is kept for the series on the
/// server and so follows the reader to every device, and what is kept on this desk alone.
/// « Lecture » — the card's own heading — and what its cross says to whoever cannot see it.
QString readerCardTitle();
QString readerCloseCard();
QString readerThisSeries();
/// The two quiet links of the end card: leave for the series page, and put the card away to
/// stay on the last page.
QString readerBackToSeries();
QString readerStayHere();
QString readerThisDesk();
/// The four directions, in words and nothing else: « Par défaut » when `std::nullopt`. A
/// picture of an arrow beside « De droite à gauche » says it twice, and the order of one
/// troubles the other.
QString readerDirection(std::optional<Api::ReadingDirection> which);
QString readerLayout(Preferences::PageLayout which);
QString readerFit(Preferences::Fit which);
/// « Noir », « Gris », « Blanc » — what is behind the pages.
QString readerBackdrop(Preferences::Backdrop which);
/// « Glisser », « Fondu », « Sans effet » — how a page that comes takes the place of the last.
/// Not « Aucune »: alone on its row, under « Changement de page », it read as the change of
/// page being switched off rather than drawn without a motion.
QString readerMotion(Preferences::Motion which);
/// The two halves of « Sur cet appareil », each over its rows: « Pages », what the book looks
/// like, and « Confort », what is around it. The critique of 22/10/2026 was four
/// anonymous rows of pills, which a reader had to try to learn what each one was about.
/// A webtoon's card has « Défilement » and « Commandes » instead of « Pages ».
enum class ReaderDeskGroup { Pages, Comfort, Scrolling, Controls };
QString readerDeskGroup(ReaderDeskGroup which);
/// What each of those rows chooses, written beside its pills: « Mise en page »,
/// « Ajustement », « Fond », « Changement de page » — and a webtoon's « Sensibilité »,
/// « Lecture automatique », « Commandes ».
enum class ReaderSetting { Layout, Fit, Backdrop, Motion, Sensitivity, AutoSpeed, Controls };
QString readerSetting(ReaderSetting which);
/// « La série déclare : **droite → gauche**. Ce que vous choisissez ici ne vaut que pour cette
/// série, mais sur tous vos appareils. » The bold is `<b>…</b>` for a `Text` in styled-text
/// format, so the emphasis is decided here with the words and a `.qml` file carries neither.
/// The arrow is held between non-breaking spaces so a line never ends on « droite ».
QString readerDeclares(std::optional<Api::ReadingDirection> declared);
/// « Pour tous les livres, sur ce bureau seulement. »
QString readerDeskNote();
/// « Pour tous les webtoons, sur ce bureau seulement. »
QString readerStripDeskNote();
/// The words at either end of a webtoon's two sliders: « Douce » to « Vive » for how far the
/// wheel moves the strip, « Lente » to « Rapide » for its A.
enum class StepEnd { Gentle, Lively, Slow, Fast };
QString readerStepEnd(StepEnd which);
/// « Discrètes », « En remontant », « Au clic » — when a webtoon's bars go and come back.
QString readerStripControls(Preferences::StripControls which);
/// The line under those pills, saying what the one chosen does.
QString readerStripControlsHint(Preferences::StripControls which);
/// « Reviennent aussi à l’arrêt, après 5 s sans défiler ».
QString readerStripAtRest();

/// « Raccourcis » — the card of the reader's keys: its title, and what the bar's button that
/// opens it says under the pointer.
QString readerShortcuts();

/// The four groups of that card, in the order they are read: the left column, then the right.
enum class ShortcutGroup { Navigate, Display, Zoom, Leave };
QString shortcutGroup(ShortcutGroup which);

/// What each line of the card does, worded as the approved mockup words it. The arrows are
/// worded twice: in a book read right to left ← goes on, as the left third of the screen
/// does, and a card that said « Page précédente, suivante » over ← → there said the opposite
/// of what the keys do.
enum class ShortcutDoes {
    TurnByArrow,
    TurnByArrowRightToLeft,
    TurnBySpace,
    ScrollByArrow,
    ScrollBySpace,
    ToTheEndsOfChapter,
    AutoRead,
    ToTheEnds,
    ToTheEndsRightToLeft,
    BackAPage,
    FullScreen,
    VolumeInfo,
    ReaderSettings,
    TheseShortcuts,
    ZoomByKey,
    BackToWhole,
    ZoomByWheel,
    ZoomWhereAimed,
    Pinch,
    CloseWhatIsOpen,
    BackToSeries,
    CloseReader,
};
QString shortcutDoes(ShortcutDoes which);

/// What is written on a key. A letter and a function key are words of the card like the
/// others, and kept here with them: « Échap », not « Esc », is a decision about French.
enum class KeyName { Space, Shift, Ctrl, Alt, Escape, F, F11, I, Question, F1, Zero, W, Enter, Tab,
                     Menu, F10, Slash, A, Home, End, S };
QString keyName(KeyName which);

/// « + » between the keys of one combination, « / » and « ou » between two that do the same,
/// and « double clic » beside the mouse.
QString shortcutAnd();
QString shortcutSlash();
QString shortcutOr();
QString doubleClick();

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
enum class Asking { Shelf, Search, Filters, Resume, State, Reader };

/// « Leaf n’a pas pu s’installer : il n’y a rien à afficher. »
QString notSetUp(Asking what);

/// « Leaf ne sait pas où est votre bibliothèque. » — no address at all, which is a setup
/// that never happened rather than a server that is down.
QString noLibrary();

/// « Le serveur est injoignable — connexion refusée ». The tail is Qt's own words for the
/// network error, which Qt does translate; only Leaf's half of the sentence was English.
QString unreachable(const QString &why);

/// « Leaf n’a pas pu lire la réponse du serveur… » — an answer that is not the JSON its
/// own contract promises.
QString unreadableAnswer();
/// What the client says when the server answered and the answer lacks what it needs, from the
/// machine's « part: field is missing ». A line that is not in that shape is returned as it
/// came: it was already a sentence.
QString answerNotUnderstood(const QString &raw);

/// « La clé a été refusée. », and what the server said after it when it said anything.
QString keyRefused(const QString &said);

/// « Le serveur à cette adresse ne se présente pas comme la bibliothèque attendue. … » — the
/// pinned certificate, refused, in words for anyone. It offers no way past: a certificate
/// that is not the pinned one is either a server that changed or somebody standing in front
/// of it, and this client cannot tell which — so it says who can.
QString certificateMismatch();

// ——— Linking a device: the screen « Aucune bibliothèque reliée » ————————————————————
//
// The refusals began as the spec's own sentences. They no longer name a command, an address
// or a setting: a person on the machine that runs the library was shown 127.0.0.1, a tunnel
// and sudo by the relay panel, and every sentence about linking was rewritten so that anyone
// can act on it. The commands are the README's.

/// The screen's title, its one paragraph, and the names of its two fields.
QString noLibraryLinked();
QString linkIntro();
QString linkField();
/// What the empty field shows: the shape of what goes in it.
QString linkPlaceholder();
QString deviceNameField();
QString deviceNameHelp();
/// « Relier » while there is nothing to link to, « Relier à cette bibliothèque » once found.
QString linkAction(bool found);
/// The button of a link that was read and not yet looked for.
QString lookForTheLibrary();
/// Said while the address in the link is being reached, before anything is known of it.
QString lookingForTheLibrary();
/// « Bibliothèque trouvée : 192.168.1.20 », and under it what was checked and what it holds.
QString libraryFound(const QString &host);
QString libraryFoundDetail(bool pinned, int series);
/// « Ce n’est pas un lien de liaison. … » — what is not a link is told where one comes from
/// and what it looks like.
QString notALink();
/// « Aucune bibliothèque Leaf ne répond à ce lien … » — `device add` gives one link per
/// address the server listens on, and only some of them are reachable from a given device.
QString noLibraryAnswers();
/// « Ce lien n’est plus valable … » — expired, already used, or never issued: the server
/// gives one answer for the three, and so does this.
QString linkNoLongerValid();
/// « Un autre appareil s’appelle déjà Bureau. » — the code is still good.
QString nameTaken(const QString &name);
/// The server refused the name itself: empty, too long, or holding a control character.
QString nameNotAccepted();
/// Failed codes count against the same throttle as wrong keys, and this is said in terms of
/// codes rather than of keys — this device has no key yet.
QString tooManyTries(const QString &seconds);

/// « Trop de clés fausses ont été essayées. Attendez 30 secondes. » — the server's own
/// `Retry-After`, said rather than counted down, because nothing here ticks.
QString tooManyWrongKeys(const QString &seconds);

/// « Il n’y a rien de tel ici. » — a 404 the server did not word itself.
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

/// « … est lisible par d’autres que vous, il n’a donc pas été lu du tout. » A key file
/// anyone can read is a key already given away; Leaf refuses it rather than use it, and
/// says the one command that fixes it.
QString readableByOthers(const QString &path);

/// Said when the client is asked to write its settings while `LEAF_ADDRESS` or `LEAF_KEY` is
/// set. The environment wins for the run; a link written underneath it would stay invisible
/// until the day the environment went away, so nothing is written and this says why.
QString settingsFromEnvironment();

/// Said when unlinking could not take the key out of the keyring. Everything else is forgotten
/// already; this names the one thing left and where it sits, so it can be removed by hand.
QString keyringKeptTheKey();

// ——— Once linked: the settings tab « Bibliothèque » ——————————————————————————————

/// The card's title, and the names of its two lines.
QString linkedLibrary();
QString thisDevice();
QString linkedOnLabel();
/// « 22 octobre 2026 » — the day, not the minute: nobody needs the time a device was linked.
QString linkedOn(const QDateTime &when);
/// What the pencil beside the name does. Never drawn: a screen reader says it.
QString renameDevice();
/// « Modifier l’adresse », the shape of what goes in its field, and the button that tries it.
QString changeAddress();
QString addressPlaceholder();
QString tryThisAddress();
/// Said while the key is tried at the new address, before anything is known of it.
QString tryingTheAddress();
/// What was typed is not an address a library can be at.
QString notAnAddress();
/// « Vers une autre machine, l’adresse d’une bibliothèque commence par https://. » — plain
/// http only ever on the loopback; what an address looks like, not why.
QString httpsElsewhere();
/// The new address answered, and did not know this device's key: not the same library, or
/// not this device's. Nothing is written.
QString keyUnknownThere();
/// « Délier cet appareil » and its confirmation: the question, what goes, how to come back,
/// what to do when the server does not answer, and the button.
QString unlinkDevice();
QString unlinkQuestion();
QString unlinkWhat(const QString &name);
QString unlinkComeBack();
QString unlinkIfSilent(const QString &name);
QString unlinkGo();
/// The link screen, after an unlinking the server did not confirm — it did not answer, or
/// the client was holding back and never asked: the key may still work there.
QString unlinkedWithoutTheServer(const QString &name);
/// The link screen, after an unlinking the server refused with a 409: this device came in
/// with a key from `LEAF_KEYS`, which no device command removes — only the file does. The
/// README says how; the screen says who.
QString unlinkedAnOldKey();
/// « Cet appareil a été retiré de la bibliothèque. … » — the link screen, opened because the
/// server no longer knows this device's key: `device revoke` was run.
QString deviceRevoked();
/// The same refusal for a key the environment set, which Leaf does not erase.
QString revokedUnderEnvironment();
/// A command cancelled because the pin changed under it, and not by anything the server
/// said: it may have arrived, and it may not.
QString cutShortByANewPin();
/// « Relier un autre appareil » — the button, and the panel it opens: what to do with the
/// link, how long it lasts, and the one action this client has for it.
QString relayAnother();
QString relayIntro();
QString copyTheLink();
/// Said in place of « Copier » for a moment, so that a click that worked looks like one.
QString linkCopied();
/// « Valable encore 9:42 · une seule fois » — minutes and seconds, the way a timer reads.
QString relayTimeLeft(int seconds);
/// « PC du salon relié » — the other device used the code, and the panel can be closed.
QString deviceRelayed(const QString &name);
/// The code ran out, or was withdrawn elsewhere, before anybody used it.
QString relayRanOut();
/// Under a link the server built from one of its own network addresses, there being no
/// public one: it opens the library from the same network and nowhere else.
QString relaySameNetwork();
/// In place of « Relier un autre appareil » and « Délier cet appareil », when the server says
/// nobody else reaches it. Plain words, and nothing more: a person on the machine that runs
/// the library was once shown 127.0.0.1, a tunnel, sudo and LEAF_PUBLIC_URL here, and the
/// README is where those belong.
QString onlyThisComputer();
/// Another code, for a panel whose code ran out.
QString anotherLink();
QString closeThePanel();

// ——— L'import ———————————————————————————————————————————————————————————————

/// Where one file is on its way in. Its own enumeration rather than `Imports::Stage`, for
/// the reason at the top of this file: this is a leaf, and it does not know what owns a
/// queue or a socket.
/// `Reviewing` comes last, as on `Card::Stage`: the screen mirrors these as numbers, and a
/// stage inserted in the middle would renumber every one after it under the QML's feet.
enum class Importing { Asking, Deciding, Ready, Sending, Filing, Filed, Paused, Failed, Reviewing };

/// « Importer » — the dialog's own name, and the bar button's.
QString importing();
/// « Déposez vos fichiers ici », and underneath, how. The second line exists because the
/// first one alone leaves a reader wondering whether the window is the target or the box is.
QString dropFilesHere();
QString dropHow();
/// The drop zone's one sentence under its title: what may be dropped, and what is read of it.
QString dropWhat();
/// Under the drop zone: nothing leaves the machine on the choosing step.
QString chooseReassurance();
/// « Déjà lu, 3 éléments » — over the cards of what was dropped.
QString readSoFar(int cards);
/// The footer's sentence while the cards are shown: nothing is written before it was seen.
QString everythingIsChecked();
/// « Vider » — gives up what was dropped and goes back to the empty drop zone.
QString emptyTheQueue();
/// « 53 fichiers à envoyer · 6,8 Gio » — what the drop would send, over the cards.
QString toSendTotal(int files, qint64 bytes);

/// The third step, « Envoyer »: what is about to travel, said once at the top and then line by
/// line, and the same frame once a line has gone. Volumes, which is what a reader counts:
/// the archives are what a folder holds that is sent.
QString recapTitle(int volumes);
QString recapAbout(qint64 bytes, int cards);
QString recapExplains();
QString recapSeries();
QString recapVolumes();
QString recapSize();
QString recapState();
/// Under the checking box of the third step, which only recalls what the first chose.
QString recapVerifies();
/// « Importer tout » — the button that sends every line not yet going nor done.
QString importAll();
/// « Terminer » — that same button once every line has arrived: it closes the window.
QString finishIt();
/// « Envoi en cours : 69 tomes sur 79 » and « 3,1 Gio sur 7,5 Gio » — the banner once a line
/// has gone.
QString sendingTitle(int done, int whole);
QString sendingAbout(qint64 sent, qint64 whole);
QString sendingExplains();
/// « Envoi terminé : 79 tomes », or « Envoi interrompu : 69 tomes sur 79 » when a line did
/// not arrive.
QString sentTitle(int done, int whole);
/// The sentence under the banner: what « Envoyer » will do before the send, what each line's
/// button does during it, and nothing once it is over.
QString bannerExplains(bool going, bool finished);
QString sentAbout(qint64 bytes, int failed);
/// The foot's sentence before and after a line has gone.
QString recapFoot();
QString sendingFoot();
/// « 46 % » — how far a line has gone, rounded down: 100 is for what has arrived.
QString percentSent(double fraction);
/// What a line says under its name: « Série · 2 éditions · sera créée · JSON Leaf lu ».
/// `works` and `editions` count what stands under the card's own root; `state` is what
/// becomes of that root, empty when nothing is known; `origin` is how the sheets were read
/// (0 said, 1 taken, 2 nothing, -1 not read yet).
QString lineAbout(Manifest::Level level, int works, int editions, const QString &state,
                  int origin);
/// A line that waits its turn or was stopped, said in front of what it is.
QString lineQueued(const QString &about);
QString linePaused(const QString &about);
/// A line put back to zero, until the server has answered again.
QString lineResetting();
/// The tooltips of the three marks of a line: send it, stop it, put it back to zero.
QString lineStart();
QString lineStop();
QString lineRetry();
QString lineCancel();
/// « 12 fichiers déjà dans la bibliothèque, non renvoyés » — empty when none is.
QString heldNotResent(int files);
/// How a whole card was read — « Complet » (its own JSON), « Komga » (a ComicInfo), « Nu » (nothing).
QString readHow(int origin);
/// « Blue Period existe déjà dans la bibliothèque : 12 tomes sur 14. Cet apport sera traité
/// comme un réimport. » — the drop held in part, by name.
QString heldNamed(const QString &name, int held, int whole);
/// What a line of the choosing step says of its own count: « Vérification · 6 / 14 » while the
/// walk hashes, « Lecture · 6 / 14 » while it only reads, and bare « 6 / 14 » when the line has
/// no room for the word (`brief`). Said over the line and not under its name: the second line
/// of a card is what it holds, and the count has to outlast the walk it counts.
QString lineCounting(bool hashing, qint64 done, qint64 whole, bool brief);
/// « Vérifié · 14 / 14 » once a walk that hashed is over, « Lu · 14 / 14 » when it did not;
/// bare « 14 / 14 » when brief.
QString lineChecked(bool hashed, qint64 whole, bool brief);
/// What the same line says when a walk that hashed could not hash the pages of some archive of
/// the folder: « Fichier vérifié, pages non vérifiées », and « Pages non vérifiées » when
/// brief. Never « Vérifié », which would say the pages that a rewrite at the other end is
/// compared with were covered, and they were not; the reason is said under the line.
QString lineCheckedFileOnly(bool brief);
/// « Vérification : 37 tomes sur 120 » over the whole drop, « Lecture : … » when nothing is
/// hashed — the one line that says why « Suivant » is not yet there.
QString checkingAll(bool hashing, qint64 done, qint64 whole);
/// « Lecture en cours : 2 séries sur 5 » — before any folder has said how many volumes it holds.
QString readingAll(int done, int whole);
/// « Prêt : 5 séries à importer » — everything read, nothing left to answer.
QString readyAll(int series);
/// « À vous de dire : 2 séries » — what the drop still asks, once it is all read.
QString decidingAll(int series);
/// The one line under the drop zone, from where the whole drop stands: the series before any
/// folder has said how many volumes it holds, then the volumes, then the wait for the server,
/// then what is left to answer, then ready.
struct DropCounts
{
    int foldersRead;
    int foldersToRead;
    int reading;
    qint64 checked;
    qint64 toCheck;
    bool hashing;
    int deciding;
    int listed;
};
QString dropProgress(const DropCounts &counts);
/// « Suivant s’active à la fin » — beside the count, for as long as it counts.
QString startWaits();
/// What the greyed « Suivant » says when it is hovered.
QString startWaitsHint();
/// The commands along the bottom, in the order they appear and change.
QString cancel();
QString goBack();
/// « Suivant » — the way on from the first step and from the second.
QString next();
/// « Précédent » — the way back from the second step and from the third.
QString previous();
/// « Importer » — the third step's button, the one that starts the transfer.
QString importIt();

/// « En attente », « Envoi », « Rangé »… — one word per stage, for the row that shows it.
QString importStage(Importing stage);

/// « 12,0 Mio sur 128,0 Mio » — how far one file has got, in `size`'s own units rather
/// than a vocabulary of its own.
QString howFar(qint64 sent, qint64 whole);

/// « Nouvelle tentative dans 8 s » — said, because a queue that retries in silence is a
/// queue that looks stuck.
QString tryingAgainIn(int seconds);

/// « Aucune série ne correspond. Un fichier seul rejoint une série existante ; pour en
/// créer une, déposez le dossier. » The honest answer, and the one that teaches the model
/// rather than pretending a single file can create anything.
QString noSeriesForThisFile();

/// « Pause », « Reprendre », « Abandonner » — the three things a row can be told. The
/// third is worded apart on purpose: it is the only one that throws something away.
QString pauseIt();
QString resumeIt();
QString abandonIt();

/// « 3 décisions » / « une décision » — what the bar's button says while the dialog is
/// shut. A transfer that finished and a question nobody saw look the same from there.
QString decisionsWaiting(int many);

/// « 38 tomes envoyés · 2 encore à venir · 1 mal arrivé » — what a commit did, and what it
/// could not. Said even when everything went home, because « rien à signaler » and a row
/// that simply stopped saying anything look the same.
QString whatLanded(int installed, int pending, int corrupt, int orphans);

/// « « Tome 7.cbz » n'a pas pu être lu. » — the local file went away, or turned
/// unreadable, between the moment it was chosen and the moment its bytes were wanted.
QString couldNotBeRead(const QString &name);

/// « Le nettoyage de Koro a échoué : ses octets restent sur le serveur. » — `abandon()`'s
/// own `Row::trouble`, on the rare occasion its `DELETE` got a refusal rather than the 204
/// the contract otherwise promises. The row is reinserted, `Stage::Failed`, to carry it —
/// this card's own fact, said on this card alone, rather than a queue-wide `trouble` that
/// would stop every other row from sending over one session the server would not drop.
QString couldNotCleanUp(const QString &name);

/// « Accepter » — the answer to the list of what an import would create.
QString acceptCreations();

/// « « Elfes » est déjà dans « Mangas » — le ranger ici l'y déplacera » — the sixth case of
/// an import, said in full because every part of it matters: what is already there, where
/// it is, and what ticking the box would do to it. The folder is named rather than described
/// « ailleurs » — a reader has to recognise the place before agreeing to leave it.
QString alreadyElsewhere(const QString &name, const QString &folder);

/// « Vérifier que chaque tome arrive intact », and what that means in full.
///
/// The label said « Vérifier chaque fichier », which does not say **against what** — and the
/// person who asked for the option had to ask what it did. A checkbox whose own author
/// cannot read it from its label is a checkbox that will be left at whatever it came with.
/// So the label names the outcome, and the line under it names the mechanism and the cost.
/// Per page, and not per file: the server rewrites an archive as it installs it, and the
/// pages are what survives that, so they are what the result is compared with.
///
/// Checked by default: a volume that travelled wrong is worse than one that did not travel,
/// because nothing afterwards would say so.
QString verifyEachFile();
QString verifyingMeans();

/// « ou choisir » — the verb, said once and in faint ink, for the two pickers under it.
///
/// Two buttons each carrying their own verb read as two unrelated commands competing under
/// the invitation to drop. One verb and two objects read as one idea with two doors, which
/// is what they are: the desktop has a window for files and a window for a folder.
QString chooseLead();

/// « Des fichiers » — the other way in, for a reader who does not drag. The title is the
/// picker window's own, which on this desktop is the only place it shows.
QString chooseFiles();
QString chooseFilesTitle();
/// And the folder, which is a second picker and not a mode of the first: the desktop offers
/// one window for files and one for a folder, and a single button would quietly do half the
/// job. Dropping does both at once, which is why it is what the box invites.
QString chooseFolder();
QString chooseFolderTitle();

/// « « Vieux/Death Note » arriverait au même endroit que « Death Note ». Déposez-les
/// séparément. » — two folders of one name in a single drop aim at the same folder of the
/// library, and the second would overwrite the first.
///
/// Both are named, and by what tells them apart: « Death Note » twice would help nobody, so
/// it is the path from the dropped folder that is shown.
QString sameDestination(const QString &one, const QString &other);

/// « « Terres d’Arran » est dit par universe.json là où il ne peut pas être. Impossible : pas
/// d’univers dans un univers. Corrigez ce fichier, puis déposez le dossier à nouveau. » — a
/// declaration the server refuses at the announcement, said before anything is sent. `why`
/// is the `impossible…` sentence of the rule it breaks.
QString declaredOutOfPlace(const QString &name, const QString &file, const QString &why);

// ——— The levels of the model, and what a node of the import tree says it holds ————————

/// The name of a level of the model, and the icon that marks it.
///
/// Both live here because they serve everywhere the application names these levels — the
/// scan report, the search, the import tree — and a universe drawn one way on one screen
/// and another way elsewhere is a vocabulary nobody learns.
///
/// **An icon, and not an emoji.** An emoji is in colour, different on every platform, and
/// does not tint: it would be the one thing in the interface that does not follow the
/// theme. It is the reason `FilterValueRow` already gives for drawing its own mark rather
/// than taking it from a font, pushed one notch further.
QString level(Manifest::Level level);

/// The same level as a label begins: « Univers », « Série », « Édition ». Written out rather
/// than computed from `level`, for the reason this whole file exists — a capital put on by
/// code is a capital nobody sweeps, and « édition » would have to become « Édition » with its
/// accent intact.
QString levelTitle(Manifest::Level level);
/// The file's own name, without `.svg` or a path — `LevelMark.qml` composes the two.
QString levelIcon(Manifest::Level level);

/// What becomes of one node of an import's tree, in seven sentences and not one more.
///
/// A tree of sixty lines each carrying a different wording is a tree nobody reads: the same
/// thing has to be said with the same word wherever it happens. They are written here rather
/// than composed at the point of use for the reason every other string in this file is.
///
/// Three of the six take the level they describe, because they end in a past participle and
/// a past participle agrees with its subject: « série » and « édition » are feminine, so a
/// series or an edition « sera créée », while « univers », « tome » and « chapitre » are
/// masculine and « sera créé ». Left invariable, every line of a sixty-line tree would read
/// « série · sera créé » — a wrong letter repeated once per row. The other four — « déjà là »,
/// « à envoyer », « envoi », « échec » — do not inflect, so they take nothing.
QString willBeCreated(Manifest::Level level);
QString willBeMoved(Manifest::Level level);
QString alreadyInTheLibrary();
QString willBeSent();
QString beingSent();
QString wasFiled(Manifest::Level level);

/// What a container says once its card is done: « créée », « déplacée », « envoyée ».
///
/// The tree kept saying « sera créé » under a card whose badge already read « Envoyé » —
/// seventy-six volumes in the library and a line still promising they would arrive. A state
/// is what is about to happen or what has happened, never both, and the card knowing which
/// is no use if the tree under it does not.
QString wasCreated(Manifest::Level level);
QString wasMoved(Manifest::Level level);
QString wasSent(Manifest::Level level);
/// « échec » — the volume the queue tried to send and could not. Without a word for it, the
/// node kept saying « à envoyer » at the exact moment somebody looked at it to see what had
/// gone wrong; the row's own `trouble` says why, but the node still said something untrue.
QString failedToSend();

/// « sera remplacé » — the volume the library already holds under this path, which the
/// arriving one does not match. It agrees with its level for the reason the three above it
/// do.
///
/// A commit installs by renaming, and a rename onto an existing path replaces it. The server
/// names these in `replaces`; before it did, they fell into `toSend` like any other and the
/// tree said « à envoyer » over a volume about to be overwritten — the one word that had to
/// be right, because it is the last thing shown before somebody accepts.
QString willBeReplaced(Manifest::Level level);

/// « série · sera créée » — a node's level and what becomes of it, joined the one way French
/// joins them. Composed here rather than in `.qml`, for `pill`'s own reason: a `.qml` file
/// that puts a word beside another is a `.qml` file writing French, and the next one will
/// put them in the other order.
///
/// `state` empty gives the level alone, with no dangling middle dot: before the server has
/// answered, no node knows what it will become, and « série · » trailing on nothing would
/// read as broken rather than as not yet known.
QString levelAnd(Manifest::Level level, const QString &state);

/// « Série · déjà là · 21 tomes · 2,2 Gio » — everything one line of the tree says about
/// itself, on one line and under one separator.
///
/// It was two: the level and the state right-aligned in a column of their own, the count and
/// the weight under the name. Two columns of small grey text down a twenty-one volume series
/// read as two unrelated lists, and the right-hand one had no left edge to line up against.
/// Whatever is empty drops out with its separator — a node before the server has answered
/// says « Série · 21 tomes », not « Série ·  · 21 tomes ».
QString nodeLine(Manifest::Level level, const QString &state, const QString &holds);

/// « Assassinat · Tome 1.cbz » — what a volume declares itself to be, beside what it is
/// called on disk.
///
/// A container shows the title its sidecar declares; a volume showed its file name alone, so
/// a shelf of « Tome 1.cbz » said nothing a folder listing did not. The title first, because
/// it is what somebody recognises, and the file name kept because it is what they will see
/// on disk afterwards. The file name alone when the archive declares no title, and when
/// nothing has opened it yet — the instant tree opens nothing.
QString fileNamed(const QString &title, const QString &fileName);

/// « à la place de « Assassinat » · 102,2 Mio » — what the library already holds where this
/// volume would land.
///
/// The one sentence that turns « sera remplacé » from a warning into a decision somebody can
/// take. Measured on a real library: two archives 682 bytes apart, whose whole difference was
/// a title edited through the API months earlier — and the only way to find that out was to
/// open both by hand.
///
/// `read` false is the server saying it did not open the file, which is not the same as an
/// archive that declares no title: past a ceiling on one preflight it stops opening them, and
/// a sentence that quietly dropped the title would read as « it has none ».
QString insteadOf(const QString &title, qint64 bytes, bool read);

/// « la déclaration de « Assassination Classroom » diffère · résumé, arcs » — the sidecar the
/// library already holds, and what the two disagree about.
///
/// The field names and not the values: a summary is four hundred words and a line of a tree
/// is one line. And they are what a reader needs, because the question is never « which of
/// these two strings » but « did I edit this here ».
QString declarationDiffers(const QString &presentName, const QStringList &differs);

/// What a container says instead of « déjà là » when something under it would be overwritten
/// — and whether that is still a question or already an answer.
///
/// « 1 tome remplaçable » while nobody has ticked it, « 1 tome sera remplacé » once somebody
/// has. Remplaçable and not « à remplacer »: nothing here has to be replaced, and a line
/// reading like a task left undone would push somebody into doing it. The two are not the same fact and the line said the first one in both cases, so the
/// box under it looked like it did nothing: the only thing that changed was three lines
/// down, folded.
///
/// A series every volume of which the library already holds, minus one, used to read
/// « Série · déjà là » — true of twenty volumes out of twenty-one, and the twenty-first is
/// the point.
QString holdsReplacements(int count, int chosen);

/// « 4,1 Gio » — sizes, in binary units correctly named. The only formatter before this
/// one was a lambda inside `howFar` dividing by 1024² and writing « Mo » — wrong by five
/// percent, and the import tree would have said « Gio » right beside it.
QString size(qint64 bytes);

/// « 34 tomes · 4,1 Gio » — what a node holds. Empty when it holds nothing, because a line
/// reading « 0 tome · 0 o » says nothing a reader needs. The space before « · » does not
/// break, as in `fileDetail` beside it in the same column.
QString nodeHolds(qint64 volumes, qint64 bytes);
/// A series in a list: « 2 éditions · 76 tomes · 1,2 Gio ».
QString seriesHolds(int editions, qint64 volumes, qint64 bytes);
/// A volume called by its number and its title: « Tome 1 · Son Gokû et ses amis ».
QString titled(const QString &name, const QString &title);

/// « blue_period_13.cbz · 48 Mio » — what a file's line of the checking frame says under its
/// title: the name it has on disk, and its weight. The space before « · » does not break, so
/// a narrow column never opens its second line on the separator.
QString fileDetail(const QString &fileName, qint64 bytes);

/// « Vérification · 12/68 tomes ». The count is what tells a wait from a freeze: the word
/// alone, fixed for thirty seconds, has already made the application look crashed for long
/// enough that the desktop offered to kill it.
QString checking(qint64 done, qint64 whole);
/// « Lecture · 23/50 tomes » — the same count when no checksum is computed. The walk still
/// opens every archive to read what it says about itself, which takes long enough on a real
/// series to need a count, and is not a verification: « Vérification » would name a hash
/// nobody is computing.
QString reading(qint64 done, qint64 whole);

/// « En attente » — a folder at `Stage::Asking` that is not the one the pool is actually
/// walking: still queued behind another one (`describeNext` reads a single folder at a
/// time). « Vérification » and « Lecture » name a walk in progress, and this is not one.
QString waitingToBeChecked();

/// « Vérifié… » — the folder has been read to the last byte and the server is being told
/// what is in it.
///
/// Named after what just ended rather than after what is running, because the end of the
/// verification is what a reader is watching for. The ellipsis carries the rest.
///
/// Its own word because « En attente » already meant two other things: queued behind another
/// folder, and announced and waiting for somebody to press « Importer ». Three moments under
/// one word left a reader with no way to know whether a verification had finished — which is
/// the one thing they are watching for on a folder of twenty-seven gigabytes.
QString announcing();

/// « Rien à envoyer » — a folder the library already holds whole, with nothing ticked.
///
/// Said rather than left reading « Prêt », which promised a transfer that would move no
/// byte. A card in this state goes when « Importer » is pressed: it has nothing to do, and
/// leaving it in a list of things being sent is one more thing to read for nothing.
QString nothingToSend();

// ——— What a file or a folder could not say about itself ——————————————————————————————
//
// Nine sentences that used to sit in `Cbz.cpp` and `Manifest.cpp`, beside the code that
// produces them. They reach the screen — `Imports::ask` puts a `Cbz` trouble on the card's
// own line, `Manifest` puts its own under the tree — so they are French an interface shows,
// and French an interface shows lives here or it lives untested. One of them carried an
// ordinary space before its « ? » the whole time, and `tools/words_stay_french.py` could not
// see it: the guard reads `Words.cpp` and nothing else, which is exactly the point of
// keeping none of this anywhere else.

/// The archive would not open at all — a permission, a vanished file, a device gone.
QString couldNotBeOpened();

/// It opened, and there is no zip in it. A `.cbz` that is a renamed `.rar` reaches here.
QString notAnArchive();

/// Bigger than this client reads in one go. Said rather than attempted, because attempting
/// it is what fills a laptop's memory.
QString archiveTooBig();

/// A zip whose central directory points past its own end: written by something that stopped
/// halfway, or truncated in transit.
QString catalogueMissing();

/// The declaration inside the archive is too large to be one. A sidecar is a few hundred
/// bytes; anything else is a file that happens to share its name.
QString sidecarTooBig();

/// Compressed by a method this reader does not implement. Deflate and stored are what a
/// `.cbz` uses; the rest exists and is not worth carrying.
QString sidecarCompressedInAnUnknownWay();

/// Deflate said no. The archive is readable, this one member is not.
QString sidecarCouldNotBeInflated();
/// Why a volume's members could not be hashed, as opposed to its sidecar read.
QString memberCompressedInAnUnknownWay();
QString memberCouldNotBeRead();

/// The walk stopped: a folder deeper than the model goes, which on a real disk means a
/// symbolic link pointing back at a parent.
QString folderTooDeep();

/// Asked of something that is not a folder at all.
QString notAFolder();

/// « Envoi · 412 Mio sur 1,2 Gio » — a stage and how far it has got, joined the one way
/// French joins them, and the stage alone when there is nothing to measure yet.
///
/// Here for `pill`'s own reason. `ImportRow.qml` built it with `stageLabel(…) + " · " +
/// howFar(…)`, which is a `.qml` file writing French, and nothing tested the separator it
/// chose.
QString stageAnd(const QString &stage, const QString &howFar);

/// « · une page manquante » — one concern of an archive, marked as one of a list. The dot
/// belongs to the sentence, not to the delegate that draws it.
QString concern(const QString &said);

// ——— Profiles: who reads ———————————————————————————————————————————————————————————
//
// Neutral, and in the second person of politeness: « profil », never a gendered noun for the
// person, and no sentence that says what somebody is. The full set of words the screens use
// is theirs to add; these are the few the model itself has to say.

/// « Qui lit ? » — the question, and the name of the screen that asks it.
QString whoReads();
/// « Ce code n’est pas le bon. » — said by every route that takes the code, and not the
/// server's own « code », which `keyRefused` would turn into a sentence about the key.
QString wrongCode();
/// What stands under a profile's name in a list: « Principal » for the first, nothing for a
/// reader, « Jusqu’à 12 ans » for a restricted profile, and a plain « Restreint » when that
/// profile is of a kind this client was not taught or has no age.
QString profileNote(Api::Kind kind, std::optional<int> maxAge);

// ——— Profiles: the screens ————————————————————————————————————————————————————————
//
// One function per text of the mock-ups, second person of politeness, neutral. A text that
// carries a name or a number takes it as an argument: a `.qml` file that joins « Passer à » to
// a name is a `.qml` file writing French.

/// The button that adds a profile from the « Qui lit ? » screen.
QString addOne();

/// The switch that makes a choice outlast the session: this device opens on that profile next time.
QString keepOnThisDevice();

/// The way from « Qui lit ? » to the screen that edits the profiles.
QString manageProfiles();

/// Under the profile list when a restricted profile is current: leaving it is guarded.
QString switchNeedsCode();

/// Leaves « Qui lit ? » without choosing, for a profile that is already current.
QString backToShelf();

/// The cross of « Qui lit ? », opened from the bar by a profile free to stay where it is.
QString closeWhoReads();

/// « Passer à Camille » — the name goes in the string, so a screen never concatenates one.
QString switchToProfile(const QString &name);

/// Said when the current profile is restricted and another is picked.
QString leaveNeedsCode();

/// Asks for the code, on every screen that takes it.
QString enterCode();

/// Title of the sheet that creates a profile, and its button.
QString addAProfile();

/// Why the code is asked before the form that adds a profile.
QString addNeedsCode();

/// The link under the code field.
QString forgotCode();

/// That command, as it is typed on the server: a `sudo` and not a sentence, so it is never
/// translated, and kept here so that the screen does not spell it either.
QString resetCodeCommand();

/// Back out of a sheet, without the destination `goBack` and `backTo` would name.
QString back();

/// Confirms a sheet that asks for the code.
QString confirm();

/// Title of the form, as opposed to the sheet that guards it.
QString newProfile();

/// Title of the form when the profile being edited is the current one.
QString yourProfile();

/// Title of the form for another profile: « Modifier Camille ».
QString editProfile(const QString &name);

/// Label of the name field.
QString profileName();

/// Label of the colour field.
QString profileColour();

/// Explains the initial drawn on a taken swatch.
QString takenColourNote();

/// Heading of the rights block.
QString whatItMayDo();

/// The first of the two roles a profile can be given.
QString readerRole();

/// The second role.
QString restrictedRole();

/// Right to import.
QString mayImport();

/// Right to edit.
QString mayEdit();

/// Right to delete.
QString mayErase();

/// Under the « Lecteur » role.
QString readerExplained();

/// Under the « Restreint » role.
QString restrictedExplained();

/// Label of the age picker.
QString seriesUpTo();

/// Under the age picker: why it stops at seventeen. Eighteen is the rating a restriction
/// exists to hide, so a profile allowed to see it would be a reader that merely cannot edit —
/// and the “Lecteur” role is already that. Tried by hand on 24/10/2026, “why does it stop at
/// 17?” was a question the window did not answer.
QString ageLimitNote();

/// One age of the picker.
QString yearsOld(int age);

/// Heading of the block that sets the code.
QString codeToCreate();

/// Label of the confirmation field.
/// The placeholder of the first code field, as « Une seconde fois » is the second's: a field left
/// blank beside one that says what it wants read as the one that was forgotten.
QString codeFirst();

/// The second code field's placeholder.
QString codeAgain();

// ——— The recovery code ————————————————————————————————————————————————————————————————
//
// The way back from a forgotten code that needs no terminal, so that it works whatever machine
// the server runs on. Shown once, written down by the person it belongs to.

/// « Votre code de secours » — the window that shows it, and the card that draws another.
QString recoveryTitle();

/// What the window says under it: write it down, keep it apart, it is not shown again, and what
/// it is for.
QString recoveryExplained();

/// « J’ai noté mon code de secours » — the one button of that window. Not « OK »: the window
/// cannot be reopened, and the button is where the person says they have it.
QString recoveryNoted();

/// What the card in the profiles tab says it is for, and the button that draws another.
QString recoveryCardExplained();
QString recoveryRedraw();

/// Under that button: the old one stops working.
QString recoveryRedrawNote();

/// In the window of a forgotten code: what to type, and the two fields' placeholders.
QString recoverExplained();
QString recoveryField();
QString newCodeField();

/// « Le code de secours n’a pas pu être créé : … » — said in the recovery window when the code
/// was made and the way back from forgetting it was not. Not as a trouble of the window that
/// made the code: that one has done its job, and a failure said there kept it open on a code
/// that was already saved.
QString recoveryNotDrawn(const QString &why);

/// « Code actuel » — the field that proves the code is known before it is changed or taken off,
/// and the sentence under it. The principal's profile proves nothing of the kind: a device that
/// keeps the principal opens on it with no code.
QString currentCode();
QString currentCodeAsked();

/// « Remplacer le code » — the button of that window.
QString replaceCode();

/// « Ce code de secours n’est pas le bon. »
QString wrongRecovery();

/// Under the fields: no recovery code, so the terminal is the way, with the command below.
QString noRecoveryCommand();

/// Label of the first field.
QString aCode();

/// Heading of the list of moments the code is asked.
QString whenCodeAsked();

/// First of those moments.
QString toLeaveRestricted();

/// Second of those moments.
QString alsoToComeBack();

/// Shown on the principal profile's form, where there are no rights to tick.
QString principalExplained();

/// Under the code block while no profile is restricted.
QString codeHintFree();

/// Under the code block once one is: it names that profile.
QString codeHintHeld(const QString &name);

/// Kind line of the principal.
QString principalKind();

/// Kind line of a reader.
QString readerKind();

/// Kind line of a restricted profile.
QString restrictedKind();

/// The mark on the profile that is current here.
QString youMark();

/// This device opens on this profile.
QString retainedHere();

/// Another device opens on it.
QString retainedOn(const QString &device);

/// What a restricted profile sees, as a part of its line.
QString seriesUpToYears(int age);

/// « Supprimer Camille ? » — the confirmation's title.
QString eraseProfile(const QString &name);

/// What deleting a profile costs.
QString eraseConsequence();

/// And what it does not: the devices it sat on ask again, the volumes stay.
QString eraseLeavesDevices();

/// The confirmation's button: « Supprimer Inès ». The name again, so that the button that
/// cannot be undone says on whom.
QString eraseProfileButton(const QString &name);

/// The form's button when it creates: « Ajouter Inès », or « Ajouter » before a name is typed.
QString addProfileButton(const QString &name);

/// The form's button when it changes a profile that exists.
QString saveProfile();

/// Under « Le code, à créer », when the first restricted profile is the reason for the code.
QString firstCodeHint();

/// The form a reader opens: what the new profile will be able to do, and who decides the rest.
QString principalManages(const QString &principal);

/// The two halves of the age stepper, for whoever reads the screen aloud.
QString fewerYears();
QString moreYears();

/// A swatch, for whoever reads the screen aloud: its palette name from the id `Theme` keeps
/// (`emeraude` → « Émeraude »), and the profile that wears it when it is taken.
QString colourName(const QString &id, const QString &owner);

/// Tab of the settings screen.
QString profilesTab();

/// Heading of the rights summary.
QString whatEachMayDo();

/// Tab.
QString devicesTab();

/// Tab.
QString aboutTab();

/// Heading of what the application opens on.
QString atOpening();

/// Lead-in of the choice that follows it.
QString opensOnThisComputer();

/// The choice that makes « Qui lit ? » come first.
QString askWhoReads();

/// Heading of the list of the other devices.
QString otherDevices();
/// « Vu le 19 octobre 2026, 14:32 »: when that device last asked the server, to the minute.
/// Absolute, as `moment` — a device left on a shelf for a month is told by its date. Empty
/// until the server has heard from it.
QString deviceSeen(qint64 milliseconds);

/// What one of them opens on.
QString opensOn(const QString &name);

/// Unlinks it.
QString removeDevice();

/// How many series a profile sees.
QString seriesForYou(int count);

/// Heading of the shortcuts list.
QString shortcuts();

/// About: the application.
QString thisClient();

/// About: the server. `theServer` is the reader's name for it, in a sentence about whether it answers.
QString serverHeading();

/// What stands under a profile's name on the screens that list them: « Profil principal · vous »,
/// « Profil lecteur · retenu sur cet ordinateur », « Profil restreint · séries jusqu’à 10 ans ».
/// Built from the parts above, joined by one separator, and the parts that are empty are dropped
/// — a line that ends on « · » reads as broken. `marks` are the optional ones, in the order they
/// should read (`youMark`, `retainedHere`, `retainedOn`).
QString profileLine(Api::Kind kind, std::optional<int> maxAge, const QStringList &marks);

/// The reader's Profils tab, under their own row: who manages the rest, by name.
QString managedByPrincipal(const QString &principal);

/// « Ce que chacun peut faire », in place of the boxes, for a restricted profile.
QString onlyReads(const QString &name);

/// Under the boxes: what « Modifier » covers, and what a new profile starts with.
QString rightsExplained();

/// Under the other devices: what removing one does, and who decides what a device opens on.
QString devicesExplained();

/// Under the list of profiles: what deleting one costs, said before anyone reaches the bin.
QString eraseExplained();

/// Another device that keeps no profile: it asks who reads when it opens.
QString asksWhoReadsThere();

/// The pencil beside another device: it chooses the profile that device keeps.
QString chooseItsProfile();

/// The confirmation before a device is removed, and what it costs.
QString removeDeviceQuestion(const QString &name);
QString removeDeviceConsequence();

/// The keyboard shortcuts the client defines, `{what, keys}`, in the order the À propos tab
/// lists them. Written from the `Shortcut` and `Keys` handlers in the QML, and to be changed
/// with them: a shortcut listed here that the client does not answer is worse than none.
enum class AppShortcut { Search, Back, Move, Activate, SeriesMenu, ReaderKeys };
QString appShortcut(AppShortcut which);

/// Above that list: where its keys hold. Not in the reader, whose keys are others — Space and
/// the arrows turn pages there — and whose own list is the last line of this one.
QString appShortcutsScope();

/// What the À propos tab says of this client and of the server it talks to. One label each,
/// and the values are the screen's: a version, an address, a count.
enum class AboutFact { Version, Device, Qt, System, State, Access, Api, Format, ServerVersion };
QString aboutFact(AboutFact which);

/// « Fermé : il faut une clé » / « Ouvert, sur cet ordinateur seulement ».
QString libraryAccess(bool closed);

/// Under the server's card: what the two numbers are, since neither says so on its own.
QString apiExplained();

// ——— What an import sheet doubts, and what it could not read ——————————————————————————
//
// Said under a field or over a sheet of the checking step (`Facts`). A doubt is not a
// refusal: it is why the step stops on a series instead of letting it through unseen.

/// « Un autre fichier porte aussi le numéro 4 » — under the number of each of two files of
/// one edition that claim the same place. Said on both, since either may be the wrong one.
QString sameNumberTwice(double number);


/// « Ordre « tout », étape 2 : « nowhere » n’est pas un ordre de cet univers » — an inclusion the
/// announcement would refuse, by the order's declared id and the step counted from 1 as a reader
/// counts. `target` is what was named, or the order whose inclusion would close a loop.
QString orderRefused(const QString &order, int step, Orders::Refusal why, const QString &target);

/// « Cet univers a plus d’ordres, ou d’étapes, que le serveur n’en lit : le reste est laissé de
/// côté » — under the orders of a universe past the ceilings. A doubt, not a refusal.
QString ordersPastTheCeiling();

/// « 5 séries · 7 étapes, dont 3 recopiées » — the line under an order. `flat` is what it reads
/// once its inclusions are read in place; `copied` is the steps that are not its own to write:
/// read from an included order when it `includes` one (« incluses »), written again from
/// another otherwise (« recopiées »). Told by its steps and not by its counts: an order of one
/// inclusion reading one step writes as many steps as it reads, and was said to copy it.
QString orderLine(int works, int flat, int copied, bool includes);

/// Over the sheet of an archive that did not open: `why` is the archive's own trouble, and
/// what follows is what it costs — a number from the name, and nothing else.
QString archiveUnreadable(const QString &why);

/// Beside a volume whose ComicInfo.xml could not be read: it stays in the archive, since
/// nothing is removed that was not read.
QString comicInfoUnreadable();

/// Over the sheet of a folder whose sidecar is not a JSON object: neither read nor completed.
QString sidecarUnreadable(const QString &name);

/// Beside an archive whose `oneshot.json` is not a JSON object, or could not be read: it
/// stays in the archive, and the archive is read as the server will read it — not a book.
QString oneShotUnreadable();

// ——— The checking frame —————————————————————————————————————————————————————————————
//
// What `Review` hands the frame: its tree, its list, its counters, its sheet and its footer.
// They are here so that their spaces and apostrophes are swept like every other.

/// Where an import's values come from, as the line under « Importer » names it. One source
/// when every file and declaration agrees on it, `Several` when Leaf sidecars and ComicInfo
/// sit side by side, `Nothing` when neither says anything.
enum class Source { Leaf, ComicInfo, Nothing, Several };

/// « 1 univers · 2 séries · 42 fichiers · JSON Leaf ». No universe is not « 0 univers ».
QString apportSummary(int universes, int series, int files, Source source);

/// « Manquant » — what a required field nobody gave reads, and a line lacking several.
QString missing();

/// « Titre », « Sens de lecture », « Tomes prévus » — a sheet's field by its JSON key, on the
/// sheet of a `level`: the authors of a volume or a book are its « Scénario » (the drawing is
/// the « Dessin » beside it), and those of a series are the « Auteurs » of all its volumes. A
/// key this client has not been taught comes back as itself rather than as nothing.
QString fieldLabel(const QString &key, Manifest::Level level);

/// « Non renseigné » — what an empty field shows inside itself, so that a value nobody said
/// is never a blank box that looks like one still loading.
QString notFilledIn();

/// The frame's four filters, and the groups its list is split into. Its own vocabulary and
/// not `Review::Filter`, for the reason `KeyFrom` gives: `Words` is a leaf.
enum class Reviewed { All, ToReview, Ready, Identical };
QString reviewFilter(Reviewed which);
/// « À revoir · 2 » — the line over one group of the list.
QString reviewGroup(Reviewed which, int count);
/// « Identiques · 12 · 1,2 Gio non renvoyés » — the folded group says how many and what
/// they spare, which is what tells a quick import from one that skipped something.
QString identicalGroup(int count, qint64 bytes);
QString showIt();
QString hideIt();
/// « Chercher une série, un auteur… » — what the search over the frame's list looks in.
QString searchTheDrop();
/// « 15 identiques : 22,0 Gio non renvoyés » — over the whole drop, a shelf or « Sans
/// univers », what its identical lines spare, said beside the list they are folded in.
QString identicalSpared(int count, qint64 bytes);
/// The ticks of the frame's list, and what is applied to the series ticked. « cochée »
/// agrees with the count; « Tout » ticks the lines shown, or unticks them all.
QString forTicked(int count);
QString applyToTicked(int count);
QString leaveUnchanged();
QString groupedActions();
QString groupedNote();
QString tickAll();
/// « Revoir la suivante » — to the next series that still asks something, in the tree's order.
QString reviewNext();
/// « 4 séries cochées » — what a tick box says aloud, the number it leaves ticked.
QString tickedCount(int count);
/// What applying did: how many series changed, how many already held every value given,
/// and how many could no longer be corrected — each said only when there is one.
QString groupedOutcome(int changed, int alreadyThere, int refused);
/// « Échec » — the chip of a card refused, on its line, in place of how its values were
/// said: a card that will never go has no values worth weighing.
QString failedChip();
/// « Retirer » — what takes a refused card out of the queue, from its sheet.
QString removeIt();
/// Opens the window that corrects what the library holds of the volume or series aimed at.
QString editInformation();

/// The tree's caption and its two nodes that are no folder: the whole drop, and the series
/// dropped outside any universe.
QString apport();
QString wholeApport();
QString withoutUniverse();

/// Over the list: « Séries de l’univers », « Tomes de Blue Period ».
QString seriesOfTheUniverse();
QString volumesOf(const QString &name);

/// Under the tree, when ComicInfo.xml will be converted: how many of the files that travel
/// carry one, and what becomes of it.
QString comicInfoConverted(int files);
/// Under the tree, when every value of the drop is said already.
QString everythingSaid();

/// « Blue Period existe déjà : 12 tomes sur 14 » — over the list of a series of which the
/// library already holds some files.
QString alreadyHeld(const QString &name, int held, int whole);

/// Under a sheet's title: its level, and where most of its values came from — « Série · lue
/// dans ComicInfo.xml », « Tome · deviné d’après son nom », « Édition · rien n’est dit »
/// when `from` is empty. The participle agrees with the level.
QString sheetSubtitle(Manifest::Level level, const QString &from, bool guessedFromName);

/// The sentence on a sheet whose reading direction nobody said. It blocks, because a series
/// read the wrong way round is not noticed until page forty.
QString readingDirectionUnsaid();

/// « Scénario manquant pour 41 tomes » — on a series some of whose volumes nobody is said to
/// have written (`authors`) or drawn (`artists`), neither they nor the series: the one place a
/// name typed releases them all. The role and not the person, as the field is labelled.
QString creditLacking(const QString &key, int volumes);

/// Under a number field, when what was typed does not read as a number (`whole`: as a whole
/// number). Quoted, so that what was typed is seen as typed.
QString notANumber(const QString &typed, bool whole);

/// Under a field with a choice (the reading direction, the status), when what was given is
/// none of its values.
QString notAChoice(const QString &typed);

/// The footer: what still stops the step, and how many archives the server will rewrite.
QString reviewFooter(int missing, int rewritten);
/// Its first half alone — « Rien ne manque », « 2 valeurs manquent encore » — for the footer
/// that has something else to say after it.
QString lackingValues(int missing);

/// « Importer 2 tomes » — the button, counting what will travel. « Importer » alone when no
/// volume travels and something still will be written: a declaration that changed.
QString importVolumes(int count);
/// « Rien à importer » — the same button when everything dropped is already there.
QString nothingToImport();

/// « Tome 13 », « Chapitre 686,5 » — a file of the list by its number, its name otherwise.
QString fileTitle(Manifest::Level level, std::optional<double> number, const QString &fileName);

// ——— The way through an import ——————————————————————————————————————————————————————

/// « Choisir », « Vérifier », « Appliquer » — the three numbered steps over the window, from 1.
QString importStep(int step);
/// The grip of a volume in the list, for a reader who cannot see it: dragging changes its number.
QString moveVolume();
/// « Fermer » — the frame's button when there is nothing left to import.
QString closeIt();

// ——— What the shape of a folder suggests ————————————————————————————————————————————

/// « Étagère », « Univers », « Série », « Édition », « Tome », « Livre », « Non lu » — the
/// label on a role's chip, so it begins with its capital.
QString role(Shape::Role role);

/// « probable », « à confirmer » — and nothing for a certain role, which asks nothing: a
/// folder holding archives is a series whatever it is called, and saying « certain » beside
/// every one would teach somebody to stop reading the word.
QString confidence(Shape::Confidence confidence);

/// « 1 série · 2 éditions · 84 tomes » — what a reading makes of the drop, in the order of
/// `Shape::Reading`. A count of zero drops out with its separator. The space before « · »
/// does not break.
QString readingSummary(int universes, int works, int editions, qint64 volumes);

/// « Une série, deux éditions », « Un univers, deux séries » — a reading by the role it gives
/// its folder and what that folder then holds. Spelled out to ten, as French writes a small
/// count in a sentence.
QString readingName(Shape::Role root, int universes, int works, int editions);

/// The clues, one sentence each, as « Pourquoi » lists them.
///
/// « Perfect et Original sont des mots d’édition connus. »
QString clueEditionWords(const QStringList &names);
/// « Les deux numérotent leurs tomes de 1 à 42 : probablement la même œuvre en deux
/// présentations. » — the count of folders written out, so three editions are not « les deux ».
QString clueSameNumbers(int folders, double from, double to);
/// « Des numérotations différentes, 6 et 2 tomes : plutôt des séries distinctes. » — 1 to 6
/// beside 7 to 8, which share no number.
QString clueOtherNumbers(const QList<int> &counts);
/// What two series would have and two editions do not: the ✕ under an edition reading.
QString clueProperNames();
/// The same fact seen from the other side: proper names and no edition word, the ✓ of a
/// reading that makes them series. Three names at most, then how many others.
QString clueDistinctNames(const QStringList &names);
/// Archives sitting in the folder: the scan's own rule for a series. Without numbers when no
/// file name gives one.
QString clueArchivesHere(double from, double to);
QString clueArchivesHere();
QString clueNoSubfolder();
/// « « Mangas » ne ressemble pas au titre d’une œuvre. » — a reason for a shelf.
QString clueGenericName(const QString &name);
/// The same fact on a series, where it is a fault to mend: « … : il faut en donner un. »
QString clueNameToGive(const QString &name);
/// Archives further down than universe, series and edition go: no role holds them.
QString clueTooDeep();
QString clueHoldsUniverse(const QString &name);
/// A folder made a shelf by a click, under a folder of series: a universe holds series only.
QString clueHoldsAShelf(const QString &name);
QString clueManySeries(int count);
QString clueNoUniverseInUniverse();
/// The reservation on a universe that would be the dropped folder itself.
QString clueDropped();
/// A folder whose every subfolder declares a series or a universe: the scan walks it through.
QString clueAllDeclared();
/// A folder whose every subfolder declares an edition: it is their series.
QString clueEditionsDeclared();

// ——— The role menu ———————————————————————————————————————————————————————————————————————

/// What a role is, under its name in the menu: « Une œuvre, avec une seule édition si rien
/// d’autre n’est dit. » The line a possible role carries where an impossible one carries its
/// reason, so a menu reads the same way whichever it is.
QString roleDetail(Shape::Role role);

/// « Dit par work.json : à changer dans le fichier. » — every other role of a folder that
/// declares itself. The import cannot take a sidecar out of the library, and a second one
/// beside the first would be read after it.
QString saidBy(const QString &file);

/// Why a role is greyed where it is, one sentence each, « Impossible : … ».
QString impossibleNoSubfolder();
QString impossibleHoldsFiles();
/// An edition anywhere but under a series: making it the edition of a series beside it means
/// moving the folder, which this step does not do.
QString impossibleEditionAlone();
QString impossibleUniverseInUniverse();
QString impossibleFolderInUniverse();
QString impossibleShelfInSeries();
QString impossibleUniverseInSeries();
QString impossibleSeriesInSeries();
/// « Impossible : une édition ne contient que ses fichiers. » — anything declared inside an
/// edition, which the server refuses at the announcement.
QString impossibleInsideAnEdition();
/// A series whose nearest archives are further down than its editions: it would hold nothing
/// the scan reads.
QString impossibleFilesTooDeep();
/// Under an edition, under a folder of a series that holds no archive, or under a universe
/// with no archive within a series and its editions: the scan stops there, and no role
/// reaches what sits below.
QString impossibleBeyondTheModel();
/// The same fact as the line of the one role left there, Étagère, which is possible and so
/// says no « Impossible ».
QString roleBeyondTheModel();
/// A universe whose nearest archive is deeper than its editions' — `layout::MODEL_DEPTH`.
QString impossibleFilesTooDeepForAUniverse();
QString impossibleNotAFolder();
QString impossibleBookIsAnArchive();
/// « Tome d’une série » on an archive at a universe's root: filing it is moving it.
QString impossibleFilingMoves();
/// A role above a declared folder that the model cannot hold it under — the server would
/// refuse the whole import for it.
QString impossibleDeclaredBelow(const QString &name, const QString &file);

// ——— The checking frame, once roles are read from the shape ————————————————————————————

/// « Apport · rôle proposé » — the tree's caption once a role in it is not said by a file.
QString proposedApport();
/// Under the tree, in that case: how a role is changed, and what changing it does below.
QString roleHint();
/// Beside the filters, when no file of the drop says anything and the roles come from the
/// shape of its folders alone.
QString nothingSaid();
/// Beside the filters, when an archive sits at a universe's root: the one kind the scan reads
/// there.
QString onlyABookAtTheRoot();

/// « 1 dossier · 100 fichiers · ni JSON ni ComicInfo.xml » — the line under the title while
/// the roles are still guesses: counting universes and series would count the guesses as if
/// they were what was dropped.
QString dropSummary(int folders, int files, Source source);
/// « 1 univers · 5 séries · 2 archives à sa racine » — the archives at a universe's root are
/// what that drop is about, and are counted apart from the series.
QString rootSummary(int universes, int series, int atRoot);

/// « Un univers · probable », « Une série · à confirmer », « Une série · nom à donner » — a
/// card's line, by the reading it was given. A name still to give is said before the
/// confidence: the reading confirmed, the name is still missing. Certain, it is the role
/// alone.
QString readingChip(Shape::Role role, Shape::Confidence confidence, bool titleMissing);
/// « 2 dossiers : Elfes (4 tomes), Mages (3 tomes) », « 9 fichiers directement dedans » —
/// what a folder read as a whole holds, folder by folder, three named at most.
QString foldersHeld(const QList<std::pair<QString, qint64>> &folders, qint64 filesHere);

/// The badge of a reading: the first of two, or one whose series has a name to give.
QString mostLikely();
QString nameToGive();
/// Over the clues: « Pourquoi la première » when there are two readings, « Pourquoi » else.
QString whyTheFirst();
QString why();
/// « entry.json × 84 » — one line of « Ce qui sera écrit ».
QString writtenTimes(const QString &name, int count);
/// Under « Ce qui sera écrit »: why those files, and why it matters that they are written.
QString writesNote();

/// « Ce qui sera écrit » — over the files a reading writes, on its folder's sheet.
QString whatWillBeWritten();
/// The mark before a clue: ✓ for the reading it supports, ✕ against it. Words and not a
/// drawing, so a screen reader says them with the sentence they open.
QString clueForMark();
QString clueAgainstMark();

/// Under the tree while the role menu is open: why some of its lines are greyed.
QString greyedRolesHint();
/// « Pas maintenant : un dossier que ce choix touche est encore lu, ou déjà parti. » — a role
/// the cards it would change cannot take now (`Carrier::correctable`): offered, it was a
/// click that did nothing.
QString roleWaitsForTheCards();
/// « Vrac, Série » — a role chip as a screen reader names it: the role alone said nothing of
/// the folder it ends, on a tree of chips all called « Série ».
QString roleSpoken(const QString &name, const QString &role);
/// « Grisé : Univers. Impossible : … » for each greyed role, label and reason — what the menu
/// shows and its arrows pass over, said on the chip that opens it.
QString rolesGreyed(const QList<std::pair<QString, QString>> &greyed);
/// « Deux lectures possibles » — under the title of a folder read two ways.
QString readingsPossible(int readings);
/// « Archive » — under the title of a file at a universe's root that no role reads: what it
/// is, since it is no volume of anything. Its pages are not counted — no archive is opened
/// for that.
QString anArchive();

/// A role as its menu names it: « Livre seul », « Tome d’une série », and the others as
/// their chip does — the menu says what a click makes of it, the chip what it is.
QString roleOption(Shape::Role role);
/// « Livre seul » greyed on an archive whose `oneshot.json` does not read: nothing is
/// written over what was not read.
QString oneShotUnreadableReason();
/// The two places offered to an archive at a universe's root, and what the first one does.
QString declareABook();
QString declareABookDetail();
QString fileInASeries();
/// Over the sheet of an archive at a universe's root that says it is no book.
QString unreadAtTheRoot();
/// « Aucun rôle : il ne sera pas lu » — that archive's line in the list.
QString noRoleNotRead();
/// « À la racine · 2 » — the group of a universe's archives, before its series.
QString atTheRoot(int count);
/// « Dans « BD » » — over the list of a shelf's cards.
QString insideOf(const QString &name);

/// « 1 archive sans rôle ne sera pas lue ».
QString leftOutCount(int archives);
/// « Confirmer les 3 lectures » — the button while a reading is left to confirm.
QString confirmReadings(int readings);
/// « Rien n’est envoyé avant que les 3 lectures soient confirmées. » — the footer then.
QString nothingSentBeforeReadings(int readings);
/// The footer, `leftOutCount` first when an archive will not be read, then `rest`.
QString afterLeftOut(int leftOut, const QString &rest);
/// « 23 / 50 dossiers lus » — how far the walk has come, while it lasts.
QString foldersRead(int read, int of);
/// The frame's subtitle `summary`, then `foldersRead` while a folder is still to read.
QString whileRead(const QString &summary, int read, int of);
/// « 3 séries attendent : 2 à compléter, 1 encore en lecture. Elles ne retiennent pas les
/// autres. » — said before the footer, so that what « Importer » leaves behind is not taken
/// for what holds it back. Empty when nothing waits.
QString waitingSeries(int blocked, int reading, int failed);

// ——— Credits and reading orders in the checking frame ——————————————————————————————————

/// « Scénario : Auteur A · Dessin : Auteur B » — a volume's line in the list of a series
/// whose volumes are not all credited alike. A part nobody is credited for is left out.
QString creditsLine(const QStringList &authors, const QStringList &artists);
/// « Tomes d’Elfes · crédits » — over that list.
QString volumesCredited(const QString &name);
/// « 12 auteurs au total » — its footer: the people of the series, each once. With `lacking`
/// values still missing that hold a series back, they come first — « 2 valeurs manquent
/// encore · 12 auteurs au total ». It counted « 1 crédit manque encore » apart, for a volume
/// that lacked what its neighbours had: a second count of red beside the first, and
/// « Aucun crédit ne manque » beside forty-one volumes credited by nobody.
QString creditsFooter(int people, int lacking = 0);
/// « Auteur A, Auteur B, Auteur C et 9 autres » — the people a series computes, three named.
QString creditsListed(const QStringList &people);

/// « 1 univers · 5 séries · universe.json et 2 ordres de lecture » — the line under the title
/// when a universe of the drop declares its reading orders.
QString ordersSummary(int universes, int series, int orders);
/// « « Tout Aquilon » recopie les étapes de « Terres d’Arran » ; … » — under the orders of a
/// universe one of which writes again every step of another: two places to keep in step.
QString ordersCopied(const QString &of, const QString &into);

/// « + Étiquette », « Ajouter un nom », « + Genre » — what an empty list field of a sheet offers
/// to add.
QString addWord(const QString &key);
/// What the green asterisk after a field's name means, at the right of the sheet's bar.
QString requiredLegend();
/// « Étiquettes : retirer « Terres d’Arran » » — the × of one word, as a screen reader names
/// it: the word alone said nothing of the field it would leave.
QString removeWordFrom(const QString &field, const QString &word);
/// The section of a series' sheet that says whom it is credited to, and its pieces.
QString seriesCredits();
/// The way to see every person of the series, and to fold them back.
QString showEveryCredit();
QString foldTheCredits();
/// « Ordres de lecture » — the section of a universe's sheet listing them.
QString readingOrders();
/// The arcs of a series on its sheet: the section, its button (« Éditer les arcs · 2 », or
/// the invitation when there are none), the window and its columns.
QString arcsSection();
QString arcsEdit(int count);
QString arcsWindowTitle();
QString arcsAdd();
/// The chapters of a volume, as the sheet and the window that edits them say them.
QString chaptersSection();
QString chaptersEdit(int count);
QString chaptersWindowTitle(const QString &volume);
QString chaptersAdd();
QString chaptersKeep();
QString chaptersColumn(const QString &which);
QString chaptersRefused();
/// « 12 chapitres · 188 pages » — beside the title of a volume's chapters window; the pages
/// left out when the file was not counted (-1).
QString chaptersCount(int chapters, int pages);
/// « chapitres 1 à 14, sur 3 tomes » — an arc already running when a volume opens, said with
/// its whole span and the volumes of its edition that hold it.
QString chaptersArcRuns(double from, double to, int volumes);
/// « chapitre 7, page 114 » — an arc opening inside a volume, at its chapter.
QString chaptersArcStarts(double chapter, std::optional<int> page);
/// « commence ici » or « se poursuit », after the arc's name.
QString chaptersArcState(bool starts);
/// Under the chapters: the arcs are shown there and changed on the series.
QString chaptersArcsNote();
QString chaptersArcsEdit();
QString chaptersArcLabel();
QString chaptersNumberFrom();
/// « Chap. {n} » — what a series' chapters are called when its `work.json` says nothing: the
/// server's `DEFAULT_CHAPTER_LABEL`, shown greyed in the empty box.
QString defaultChapterLabel();
/// Under a series' chapter label, in grey: how a form is written — `{n}`, its zeros, and the
/// number that follows a form without it (the server's `compose`).
QString chapterLabelHint();
/// Where an arc falls in the volumes of the drop, « tome 1, p. 4 → chapitre 194, tome 17, p. 90 »,
/// or « tomes 18 à 31 » for an arc counted in volumes. Empty when the volumes do not say.
QString arcsFallsInChapters(const QString &fromVolume, std::optional<int> fromPage, double toChapter,
                            const QString &toVolume, std::optional<int> toPage);
QString arcsFallsInVolumes(int from, int to);
QString arcsAddSaga();
/// « + Aucune saga » — the separator that ends the saga above it.
QString arcsAddNone();
/// « Arc » / « Saga » — what a line is, said before its name.
QString arcsKind(const QString &kind);
/// « 3 sagas · 9 arcs » beside the window's title; the sagas left out when there are none.
QString arcsCount(int sagas, int arcs);
/// « 3 arcs · chapitres 2 à 194 » — a saga's line, its range that of the arcs under it.
QString arcsSagaSpan(int count, const QString &unit, int from, int to);
/// « chapitres 113 à 135 · aucun arc » — the chapters no arc covers between two arcs.
QString arcsGap(int from, int to);
/// « Aucune saga » — the separator's name, which nobody types.
QString arcsNoSaga();
/// « Commencer à l’arc… » — the menu that places the chosen separator before an arc.
QString arcsStartAt();
/// « Pages et tomes de » before the edition whose pages the window gives, and the column's head.
QString arcsEditionPick();
QString arcsInEdition();
/// « Séparateur choisi : ↑ ↓ pour le déplacer, ou » — said while a separator is chosen.
QString arcsMoveHint();
QString arcsKeep();
QString arcsRemove();
QString arcsColumn(const QString &which);
QString arcsUnit(const QString &unit);
QString arcsNote();
QString arcsRefused();
/// The badge of the order read when none is chosen, and of one another order holds.
QString defaultOrder();
QString subsetOrder();

// ——— The order editor ——————————————————————————————————————————————————————

/// « 5 étapes écrites · 7 une fois aplati » — under the steps of the order being written:
/// what it says itself, and what the reader reads once its inclusions are read in place.
QString orderFooter(int written, int flat);
/// « Enregistrer l’ordre », « Enregistrer les ordres » — by the orders a save would write.
QString saveOrders(int orders);
/// « Dans « Terres d’Arran », inclure « Tout Aquilon » ramènerait à « Terres d’Arran ». » —
/// why an order is greyed among those `of` may include: `through` already reads `of`.
QString includeWouldLoop(const QString &of, const QString &through);
/// « Lu dans « Terres d’Arran » · se modifie là-bas » — under the steps an inclusion reads.
QString inheritedFrom(const QString &order);
/// « Inclure « Terres d’Arran » » — the line of a step that reads another order.
QString includeStep(const QString &order);
/// « Œuvre entière », « Tomes », « Chapitres » — a step's unit, "", "VOLUME" or "CHAPTER".
QString orderUnit(const QString &unit);
/// A range whose first number is past its last: the scan leaves such a step out.
QString rangeBackwards();
/// A range of volumes on a work of several editions, which does not say which one it counts.
QString rangeNeedsEdition(const QString &work);
/// A range of chapters that names an edition: the server refuses one.
QString chapterRangeNamesNoEdition();
/// A change that would take the orders past what the server reads of a universe.
QString orderPastTheCeiling();
/// The order editor's own words, over its tabs, its table, its sheet and its rail.
QString orderDragHint();
QString orderStepColumn();
QString orderUnitColumn();
QString orderFromColumn();
QString orderToColumn();
/// « Proposé » — an inclusion offered in place of the steps it would read, not yet kept.
QString proposedStep();
QString addWorkStep();
QString addIncludeStep();
QString universeHeading();
QString ordersHeading();
QString seriesHeading();
/// « Étape 2 », over the sheet of the step selected, counted from 1 as a reader counts.
QString stepNumbered(int step);
QString includeAnother();
QString includeInstead();
/// « L’étape nomme un ordre du même univers ; … » — what an inclusion is, under the heading.
QString includeExplained();
QString includedOrder();
QString inheritedSteps();
QString flatOrder();
/// « 7 étapes », « 1 étape » — an order once flattened.
QString flatSteps(int steps);
/// « Si « Terres d’Arran » change, « Tout Aquilon » suit. »
QString followsIt(const QString &included, const QString &order);
QString nothingCopied();
QString loopRefused();
QString loopExplained();
/// An order bearing the id of one written before it, which the scan disregards.
QString orderDisregarded(const QString &order);
/// « Aucune » — the edition of a step that names none.
QString noEdition();
/// What « Ordre inclus » says while the inclusion names no order it can read.
QString chooseIncludedOrder();
/// « Garder l’inclusion » — the proposal kept as it is, without saving yet.
QString keepInclusion();
QString stepWork();
QString stepEdition();
/// A step whose work this drop does not hold, and why it is kept.
QString absentFromDrop();
/// « Retirer « Elfes » » — the × of a step, as a screen reader names it.
QString removeStep(const QString &step);
/// How a step is moved and removed from the keys, said with the step.
QString stepKeys();

// ——— Réimporter : le cadre d'un tome ou d'une série visés ——————————————————————————————

/// « Réimporter le tome 2 » — over the frame of a reimport aimed at a volume.
QString reimportVolume(const QString &number);
/// « Réimporter Blue Period » — aimed at anything else.
QString reimportTitle(const QString &name);
/// « Blue Period · Édition française » — a volume, under the series and edition it is in.
QString reimportVolumeUnder(const QString &work, const QString &edition);
/// « Série · Édition française · 12 tomes dans la bibliothèque » — the series aimed at.
QString reimportSeriesHolds(const QString &edition, int volumes);

/// The title of the import that adds editions to the series `name`, and the line under it
/// saying what a file dropped there becomes: an edition, never a volume of the one already held.
QString addEditionsTitle(const QString &name);

/// The line under the name field of an edition an import adds, what the edition holds
/// meanwhile, and the sentences that say what is wrong with a name.
QString editionNameHelp();
QString unnamedEdition();
QString currentEdition();
QString editionNameMissing();
QString editionNameTaken(const QString &with);
QString addEditionsSubtitle();

/// « Série · 42 tomes dans la bibliothèque » — what the window says under the title of a
/// reimport while it waits for the files, so that the name it asks about is a name with a size.
QString reimportHolds(int volumes);
/// « Cible » — over the way down to what is aimed at.
QString aimedRail();
/// Under that way down, for a volume and for a series.
QString aimedRailNoteVolume();
QString aimedRailNoteSeries();
/// « Cible : le tome 2 » — over the toolbar, for a volume aimed at.
QString aimedVolumeNote(const QString &number);
/// « 2 ajoutés · 2 remplacés · 1 absent » — a part left out where it is nought.
QString aimedChanges(int added, int replaced, int absent);
/// « Ce que vous apportez » and « Tomes qui changent » — over the list.
QString aimedListVolume();
QString aimedListSeries();
/// « Tome 2 », the line of a volume.
QString volumeNumbered(const QString &number);
/// « Chapitre 12 », what a chapter file is called when nobody gave it a title: a webtoon is
/// downloaded as `Chapitre 012.cbz` and a list of empty lines names nothing to open.
QString chapterNamed(double number);
/// The name under a volume's tile: « 12 · Le Titre » when it has a title worth reading,
/// « Tome 12 » or « Chapitre 12 » when it has none, the title or the file's name when it has no
/// number. Its number alone was « 12 · » over every untitled volume, and « 12 · Chapitre 12 »
/// over every chapter a webtoon is downloaded as.
QString tileCaption(const Api::Entry &file);
/// The word over the number a borrowed cover carries, in an edition that mixes volumes and
/// chapters: « TOME », « CHAPITRE ». Written in capitals and not raised by a `toUpper`, for
/// the reason the header gives: a word is spelled here, once.
QString coverWord(Api::Entry::Kind kind);
/// « 42 % » under a webtoon chapter being read, rounded down: its pages are one long strip
/// and their count says nothing; 100 is for what is finished.
QString percentRead(double fraction);
/// The chips of the lines: Remplacer, Mettre à jour, Ajouter, Garder, Autre tome, Autre
/// série, Sans numéro, Deux fois.
enum class Aim { Replace, Update, Add, Keep, OtherNumber, OtherSeries, Unnumbered, Twice, Same };
QString aimChip(Aim which);
/// « 196 → 200 pages · progression gardée » and its parts.
QString pagesGoFromTo(int before, int after, bool placeKept);
QString titleAdded(const QString &title);
QString titleChanged(const QString &title);
QString chaptersChanged();
QString newVolume(int pages);
QString absentFromTheDrop();
QString sameAsHeld();
QString comicInfoSays(const QString &number, int pages);
QString saysItBelongsTo(const QString &series);
QString withoutNumber();
QString numberTwice();
/// « Bibliothèque » and « Apport », the two sides of a comparison, and its row labels.
QString heldSide();
QString arrivingSide();
QString pagesLabel();
QString chaptersLabel();
QString progressLabel();
QString comicInfoLabel();
/// « 3 (5 à 7) » and « page 112 » and « sera retiré ».
QString chaptersSpan(int count, const QString &first, const QString &last);
QString pageNumbered(int page);
QString comicInfoRemoved();
/// « Ce fichier est le tome 5, pas le tome 2. » and what goes under it.
QString fileIsAnotherVolume(const QString &own, const QString &aimed);
QString nothingWasModified();
QString fileBelongsToAnotherSeries(const QString &series);
/// The two ways out of a file that is another volume.
QString filePutAtVolume(const QString &number);
QString recommended();
QString volumeIsAdded(const QString &number, const QString &aimed);
QString volumeIsReplaced(const QString &number, const QString &aimed);
QString replaceAnyway(const QString &aimed);
QString pagesGoFromToSentence(int before, int after);
/// « Votre progression appartient au tome 2 : elle ne bouge pas. » and the question put when
/// the pages change by much.
QString progressStays(const QString &number);
QString keepProgress();
QString resetProgress();
/// A volume the drop does not bring: it stays.
QString volumeNotInTheDrop(const QString &number);
QString volumeStaysInTheLibrary();
/// « Non renseigné » — a value the library's declaration does not carry.
QString notDeclared();
/// « 4 infos modifiées » — over the sheet of the series aimed at.
QString infosChanged(int count);
/// The footer, and « Appliquer 8 changements ».
QString aimedFooter();
QString applyChanges(int count);
QString identicalNothingToDo(int count);
/// The parts of a line's detail, set apart by « · ».
QString joinDetails(const QStringList &parts);
/// What the drop zone of a reimport expects, by what it is aimed at.
QString aimedDrop(const QString &kind);
/// « Autre fichier » — back to the drop zone, aimed at the same place.
QString anotherFile();
/// « Nouveau » — the chip of the value the drop brings, beside « Garder ».
QString newValue();
/// « Réimporter le tome 2 » or « Réimporter Blue Period », from what the menu knew.
QString reimportAsked(const QString &kind, const QString &name);


// ——— Remplacer un chapitre ———————————————————————————————————————————————————————————
/// « Remplacer un chapitre du tome 2 » — over the frame of a chapter aimed at.
QString replaceChapterTitle(const QString &volume);
/// « Chapitres du tome 2 » — over the list of the chapters of the volume.
QString chaptersOfVolume(const QString &volume);
/// « 6 · Couleurs froides », or whichever of the two the chapter says.
QString chapterListed(const QString &number, const QString &title);
/// « pages 63 à 121 » — pages counted from 1, as a reader counts them.
QString pagesRange(int from, int to);
/// « pages 63 à 121 → 63 à 124 ».
QString pagesRangeChange(int from, int to, int becomesFrom, int becomesTo);
QString chapterReplaced();
QString chapterKept();
QString chapterShifted(int by);
/// « 3 pages en plus », « 2 pages en moins », « Même longueur ».
QString spliceExtra(int delta);
/// « Le tome 2 passe de 188 à 191 pages ; les chapitres suivants sont décalés. »
QString spliceVolumeNote(const QString &volume, int before, int after);
/// « Votre progression, page 112, reste dans le chapitre 6 : elle est gardée. »
QString spliceProgressKept(int page, const QString &chapter);
/// « Votre progression, page 112, est ramenée à la page 124. »
QString spliceProgressBrought(int page, int becomes);
/// « Source : blue_period_t02_v2.cbz ».
QString spliceSource(const QString &file);
/// « L’archive du tome 2 est réécrite une fois. »
QString spliceFooter(const QString &volume);
/// « Remplacer le chapitre ».
QString replaceChapterLabel();
/// The two ends of the range, « De » and « à », and the two sides, « Actuel » and « Source ».
QString spliceFromLabel();
QString spliceToLabel();
QString spliceCurrentSide();
QString spliceSourceSide();


// ——— Un univers visé, et une série déjà là ——————————————————————————————————————————
/// « Réimporter l’univers « Dragon Ball »… » — the entry of a series header's menu.
QString reimportUniverse(const QString &name);
/// « Univers · 3 séries dans la bibliothèque · 4 dans l’apport ».
QString universeSubtitle(int held, int arriving);
/// « à la racine, 18 → 20 tomes dans l’univers », or « dans « BD », … ».
QString moveDetail(const QString &from, int held, int arriving);
/// « nouvelle série · 26 tomes ».
QString newSeriesDetail(int volumes);
/// « absente de l’apport · 3 tomes ».
QString absentSeriesDetail(int volumes);
/// « 2 tomes modifiés · 1 ajouté ».
QString changedSeriesDetail(int changed, int added);
/// « 20 tomes identiques ».
QString identicalSeriesDetail(int volumes);
QString seriesMoveChip();
QString seriesNewChip();
QString seriesKeepChip();
QString seriesChangedChip();
QString seriesIdenticalChip();
QString seriesExistsChip();
/// « Séries », over the lines of an universe.
QString aimedListUniverse();
QString universeRailNote();
QString universeFooter();
/// « Univers + ordre nommé ajouté ».
QString universeOrdersNote();
/// « Elle existe déjà, à la racine. » / « … dans « BD ». »
QString existsAlreadyAt(const QString &from);
QString moveItWithReadings();
QString moveIntoUniverse();
/// « 18 tomes gardent leur progression ; 2 sont ajoutés. »
QString moveKeepsDetail(int kept, int added);
QString createSecondSeries();
QString existingStaysPut();
/// « « Blue Period » est déjà dans la bibliothèque. »
QString alreadyInLibrary(const QString &name);
/// « Réimporter sur « Blue Period » ».
QString reimportOnto(const QString &name);
/// « Nouvelle édition de « Blue Period » » and what it does, beside « Réimporter sur… ».
QString newEditionOf(const QString &name);
QString newEditionDetail();
/// « Sera rangé sous Blue Period › Originale » — the folder as the server says it.
QString filedUnder(const QString &folder);
/// « 42 tomes → passent dans Blue Period › Originale et prennent ce nom, lecture conservée ».
QString reshelved(int count, const QString &edition);
/// « ancien.cbz → serie_edition_tome_001.cbz » — one volume, as it was called and as it will be.
QString renamedExample(const QString &was, const QString &now);
/// « Deux fichiers prendraient le même nom : a.cbz et b.cbz. »
QString namesMeet(const QString &first, const QString &second);

} // namespace Words
