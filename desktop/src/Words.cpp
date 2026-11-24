#include "Words.h"

#include "Fold.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>

#include <QStringList>

#include <algorithm>
#include <cmath>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// « tome 2 » for the name « 2 », and « Tome 2 » for the name « Tome 2 »: a name that already
/// says the word it would be introduced by is shown as it is. A volume called « Tome 2 » on its
/// folder was announced « Réimporter le tome Tome 2 ». The word is looked for in any case, and
/// the name keeps its own capital inside a sentence.
QString called(const QString &word, const QString &name)
{
    return name.contains(word, Qt::CaseInsensitive) ? name : word + u' ' + name;
}

/// « d’Elfes », « d’Écume », « de Nains » — and « de Yoko Tsuno »: French elides « de » before
/// a vowel, accented or tied (« d’Œdipe »), and a name that opens on a « y » said as a
/// consonant keeps it. Written once: one title elided and the other not read « Tomes de
/// Elfes » over the same list as « Tomes d’Elfes · crédits ».
QString ofName(const QString &name)
{
    const QString first =
        name.left(1).normalized(QString::NormalizationForm_D).left(1).toLower();
    const bool vowel = !first.isEmpty() && u"aeiouœæ"_s.contains(first);
    return (vowel ? u"d’"_s : u"de "_s) + name;
}

/// Only a volume shortens. A chapter's number is the entry's own identity, and « Ch. 98 » is
/// an abbreviation nobody has agreed on — so the short line drops the chapter segment rather
/// than inventing a spelling for it.
QString entry(Api::UpNext::Kind kind, double value, bool briefly)
{
    if (kind == Api::UpNext::Kind::Chapter)
        return u"Chapitre "_s + Words::number(value);
    return (briefly ? u"T"_s : u"Tome "_s) + Words::number(value);
}

QString line(Api::UpNext::Kind kind, std::optional<double> number_, std::optional<int> page,
             int pageCount, const std::optional<QString> &chapter, bool briefly)
{
    QStringList parts;
    const bool named = chapter && !chapter->isEmpty();

    // A chapter entry is named by its chapter when the band knows it — « Chap. 13 : Le
    // réveil », as the reader names it — and from its number only when it does not.
    if (kind == Api::UpNext::Kind::Chapter && named && !briefly)
        parts << *chapter;
    else if (number_.has_value())
        parts << entry(kind, *number_, briefly);

    if (page.has_value() && pageCount > 0)
        parts << u"Page %1/%2"_s.arg(*page).arg(pageCount);

    // A chapter entry already said which chapter it is; repeating it would read as two. The
    // name is the server's, never rewritten here: the band once turned « #13 » into
    // « Chapitre 13 » on its own (test of 22/10/2026), and was then the one screen of the
    // application that named the chapter differently from the reader.
    if (named && kind != Api::UpNext::Kind::Chapter)
        parts << *chapter;

    return parts.join(u" · "_s);
}

/// Whether a title says nothing its label has not said: empty once trimmed, or the label
/// again, whatever its case or the spaces around it. The test of 22/10/2026 read
/// « Tome 3 · Tome 3 » over the panel of a volume whose files title it by its number.
bool saysNothingMore(const QString &title, const QString &label)
{
    const QString said = title.trimmed();
    return said.isEmpty() || said.compare(label.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace

namespace Words {

const QChar Nbsp = QChar(0x00A0);
const QChar NarrowNbsp = QChar(0x202F);

QString chapterName(const QString &label, const QString &title)
{
    const QString said = label.trimmed();
    if (said.isEmpty())
        return title.trimmed();
    if (saysNothingMore(title, said))
        return said;
    return said + Nbsp + u": "_s + title.trimmed();
}

QString volumes(int count, std::optional<Api::Medium> medium)
{
    // Zero and one both take the singular in French; two and above do not.
    const bool many = count >= 2;
    if (medium == Api::Medium::Bd)
        return u"%1 %2"_s.arg(count).arg(many ? u"albums"_s : u"album"_s);
    return u"%1 %2"_s.arg(count).arg(many ? u"tomes"_s : u"tome"_s);
}

QString holdings(const Api::Series &one)
{
    if (one.holding.ownedVolumes > 0 || one.counts.entries <= 0)
        return volumes(one.holding.ownedVolumes, one.medium);
    return u"%1 %2"_s.arg(one.counts.entries)
        .arg(one.counts.entries >= 2 ? u"chapitres"_s : u"chapitre"_s);
}

QString readStatus(Api::ReadStatus status)
{
    using enum Api::ReadStatus;

    switch (status) {
    case Unread:
        return u"Non lues"_s;
    case InProgress:
        return u"En cours"_s;
    case Read:
        return u"Terminées"_s;
    }
    return {};
}

QString pill(const QString &label, int count)
{
    return u"%1 %2"_s.arg(label).arg(count);
}

QString medium(Api::Medium value)
{
    using enum Api::Medium;

    switch (value) {
    case Manga:
        return u"Manga"_s;
    // An acronym keeps its case. This is the reason none of this is a capitalise-the-first
    // -letter helper applied to the contract's spelling.
    case Bd:
        return u"BD"_s;
    case Comics:
        return u"Comics"_s;
    case Manhwa:
        return u"Manhwa"_s;
    case Manhua:
        return u"Manhua"_s;
    case Webtoon:
        return u"Webtoon"_s;
    case Artbook:
        return u"Artbook"_s;
    case Other:
        return u"Autre"_s;
    }
    return {};
}

QString clearEveryFilter()
{
    return u"Tout effacer"_s;
}

QString nothingToFilter()
{
    return u"Rien à filtrer ici"_s + Nbsp + u": tout ce que vous avez porte les mêmes réponses."_s;
}

QString emptyShelfHeadline()
{
    return u"Votre étagère est vide"_s;
}

QString emptyShelf()
{
    return u"Cette bibliothèque ne contient encore aucune série. Importez des fichiers, ou "
           u"lancez un scan dans les réglages s’ils sont déjà dans son dossier."_s;
}

QString nothingLeftHeadline()
{
    return u"Aucune série ne correspond"_s;
}

QString nothingLeft()
{
    return u"Les filtres choisis ne laissent aucune série. Retirez-en un, ou effacez-les "
           u"tous."_s;
}

QString shelfTroubleHeadline()
{
    return u"L’étagère n’a pas pu s’afficher"_s;
}

QString searchWithin(const QString &axisTitle)
{
    return u"Chercher dans %1…"_s.arg(axisTitle.toLower());
}

QString quotedAxis(const QString &axisTitle)
{
    // « Chercher dans auteur… » named no thing; the axis is a name and is quoted as one.
    return u"« %1 »"_s.arg(axisTitle);
}

QString noValueByThatName()
{
    return u"Aucune valeur ne porte ce nom."_s;
}

QString theServer()
{
    // Not « Le serveur »: a reader does not have a server, they have a library that is
    // reachable or is not. The word names what they would notice, not what runs.
    return u"Connexion"_s;
}

QString theKey()
{
    return u"La clé"_s;
}

QString theScan()
{
    return u"Le scan"_s;
}

QString appearance()
{
    return u"Apparence"_s;
}

QString readerDefaults()
{
    return u"Lecteur"_s;
}

QString appearanceChoice(Looks which)
{
    using enum Looks;

    switch (which) {
    case Light:
        return u"Clair"_s;
    case Dark:
        return u"Sombre"_s;
    case System:
        break;
    }
    return u"Système"_s;
}

QString connected(bool reachable)
{
    // What a reader needs of a server is whether their books are there. The address, the
    // version and where the key sits are a deployment's business, and appear only when
    // something is wrong — which is the one moment they help.
    return reachable ? u"Bibliothèque connectée"_s : u"Pas de connexion"_s;
}

QString backTo(std::optional<Navigation::Destination> destination)
{
    // The article is the destination's own — « à l’étagère », « aux réglages » — so it is
    // written with each, never inserted: « Retour à étagère » was what the rule produced.
    if (!destination)
        return u"Retour"_s;
    switch (*destination) {
    case Navigation::Destination::Shelf:
        return u"Retour à l’étagère"_s;
    case Navigation::Destination::Series:
        return u"Retour à la série"_s;
    case Navigation::Destination::Reader:
        return u"Retour au lecteur"_s;
    case Navigation::Destination::Health:
        return u"Retour à la santé"_s;
    case Navigation::Destination::Settings:
        return u"Retour aux réglages"_s;
    }
    return u"Retour"_s;
}

QString whatItFound()
{
    return u"Ce qu’il a trouvé"_s;
}

QString generalSettings()
{
    return u"Général"_s;
}

QString librarySettings()
{
    return u"Bibliothèque"_s;
}

QString keyStorage(KeyFrom where)
{
    using enum KeyFrom;

    switch (where) {
    case Environment:
        return u"Dans l’environnement"_s;
    case Keyring:
        return u"Dans le trousseau de la session"_s;
    case ProtectedFile:
        return u"Dans un fichier protégé"_s;
    case Unknown:
        return u"Introuvable"_s;
    }
    return {};
}

QString scanState(Scanning state)
{
    using enum Scanning;

    switch (state) {
    case Running:
        return u"En cours…"_s;
    case Done:
        return u"Terminé"_s;
    case Idle:
        return u"À l’arrêt"_s;
    case Unknown:
        return u"Inconnu"_s;
    case Other:
        // The server grew a state this client has not been taught. Saying so is better than
        // showing nothing, and better than guessing which of the three it resembles.
        return u"Dans un état que Leaf ne connaît pas"_s;
    }
    return {};
}

QString startAScan()
{
    return u"Lancer un scan"_s;
}

QString theCache()
{
    return u"Cache"_s;
}

QString exportReading()
{
    return u"Exporter ma lecture"_s;
}

QString exportExplained()
{
    return u"Enregistre en un fichier JSON les séries que ce profil peut voir, leurs tomes "
           u"et où il en est dans chacun. Ni chemins, ni clés, ni autres profils."_s;
}

QString readingExported(const QString &path)
{
    return u"Export enregistré"_s + Nbsp + u": %1"_s.arg(path);
}

QString readingExportName()
{
    return u"leaf-lecture.json"_s;
}

QString purgeTheCache()
{
    return u"Vider le cache"_s;
}

QString purgeExplained()
{
    return u"Supprime les couvertures et les pages redimensionnées que le serveur garde "
           u"sur son disque. Rien de la bibliothèque ni des lectures n’est touché"_s + Nbsp +
           u": les images sont recalculées à la demande, un peu plus lentement la première fois."_s;
}

QString cachePurged(qint64 files, qint64 bytes)
{
    if (files == 0)
        return u"Le cache était déjà vide."_s;
    if (files == 1)
        return u"Cache vidé"_s + Nbsp + u": 1 fichier, %1."_s.arg(size(bytes));
    return u"Cache vidé"_s + Nbsp + u": %1 fichiers, %2."_s.arg(files).arg(size(bytes));
}

namespace {

/// French counts 0 and 1 in the singular — « 0 série », « 1 an » — like `volumes` does.
/// « 1 séries pour vous » is what this replaced.
QString plural(int how, const QString &one, const QString &many)
{
    return (how >= 2 ? many : one).arg(how);
}

/// « 1 seconde », « 30 secondes » from the `Retry-After` header as it came, a string: one that
/// is not a number is a wait of no length worth printing, and said as « un instant » rather
/// than « Attendez  secondes ».
QString waitOf(const QString &seconds)
{
    bool number = false;
    const int count = seconds.toInt(&number);
    return number ? plural(count, u"%1 seconde"_s, u"%1 secondes"_s) : u"un instant"_s;
}

/// « 6 séries », « 1 série », « 0 série ». Written once because seven counts follow the same
/// rule and six of them would otherwise each grow their own plural. It said « 0 séries » and
/// « 0 tomes envoyés » until the language was read through on 24/10/2026.
QString counted(int how, const QString &one, const QString &many)
{
    return u"%1 %2"_s.arg(how).arg(how >= 2 ? many : one);
}

} // namespace

QString scanCounts(const Api::ScanCounts &counted_)
{
    QStringList said;
    // Universes are rare, so an absent one says nothing rather than « 0 univers ».
    if (counted_.universes > 0)
        said << counted(counted_.universes, u"univers"_s, u"univers"_s);
    said << counted(counted_.editions, u"série"_s, u"séries"_s);
    said << counted(counted_.entries, u"tome"_s, u"tomes"_s);
    if (counted_.chapters > 0)
        said << counted(counted_.chapters, u"chapitre"_s, u"chapitres"_s);
    return said.join(u", "_s);
}

QString reanalysed(int entries)
{
    if (entries <= 0)
        return {};
    return counted(entries, u"tome relu"_s, u"tomes relus"_s);
}

QString placesCarried(int carried, int lost)
{
    if (carried <= 0 && lost <= 0)
        return {};
    QStringList said;
    if (carried > 0) {
        said << counted(carried, u"reprise de lecture conservée"_s,
                        u"reprises de lecture conservées"_s);
    }
    if (lost > 0)
        said << counted(lost, u"perdue"_s, u"perdues"_s);
    return said.join(u" · "_s);
}

QString finding(const QString &kind)
{
    if (kind == u"ERRORS"_s)
        return u"N’a pas pu être lu"_s;
    if (kind == u"MISSING_METADATA"_s)
        return u"Rien de déclaré"_s;
    if (kind == u"DISREGARDED"_s)
        return u"Lu, puis écarté"_s;
    if (kind == u"CONTRADICTIONS"_s)
        return u"Dit deux choses à la fois"_s;
    if (kind == u"IDENTITY"_s)
        return u"L’identité ne correspond pas au dossier"_s;
    if (kind == u"WITHOUT_METADATA"_s)
        return u"Ne dit rien de soi"_s;
    if (kind == u"DUPLICATE_NUMBERS"_s)
        return u"Deux fois le même numéro"_s;
    if (kind == u"DUPLICATE_PAGES"_s)
        return u"Deux fois le même nom de page"_s;
    if (kind == u"DERIVED_ARCS"_s)
        return u"Arcs déduits, donc par tome"_s;
    return {};
}

QString withoutStartPage(int chapters)
{
    if (chapters <= 0)
        return {};
    return counted(chapters, u"chapitre sans page de départ"_s,
                   u"chapitres sans page de départ"_s);
}

QString scanFailed(const QString &why)
{
    return u"Le scan a échoué"_s + Nbsp + u": %1"_s.arg(why);
}

QString notificationHistory()
{
    return u"Dernières notifications"_s;
}

QString nothingToldYet()
{
    return u"Rien à signaler pour l’instant."_s;
}

QString seeAllFindings(int count)
{
    return u"Tout voir (%1)"_s.arg(count);
}

QString foldFindings()
{
    return u"Replier"_s;
}

QString andMore(int rest)
{
    if (rest <= 0)
        return {};
    return rest == 1 ? u"et un autre"_s : u"et %1 autres"_s.arg(rest);
}

QString moment(qint64 milliseconds)
{
    if (milliseconds <= 0)
        return {};
    const QLocale french(QLocale::French);
    return french.toString(QDateTime::fromMSecsSinceEpoch(milliseconds),
                           u"d MMMM yyyy, HH:mm"_s);
}

QString lastScan(qint64 milliseconds)
{
    const QString when = moment(milliseconds);
    // Never run is not a failure: a library scanned once at startup and never since is the
    // ordinary case, and an empty line would read as a missing answer.
    return when.isEmpty() ? u"Jamais lancé"_s : labelled(u"Dernier scan"_s, when);
}

QString scanStarted(qint64 milliseconds)
{
    const QString when = moment(milliseconds);
    return when.isEmpty() ? QString() : labelled(u"Commencé"_s, when);
}

QString answering(bool reachable)
{
    return reachable ? u"Répond"_s : u"Ne répond pas"_s;
}

QString libraryHolds(int series)
{
    if (series == 0)
        return u"Aucune série"_s;
    return series == 1 ? u"1 série"_s : u"%1 séries"_s.arg(series);
}

QString apiVersion(int api, int format)
{
    return u"API %1 · format %2"_s.arg(api).arg(format);
}

QString sharedFolder(bool there)
{
    return there ? u"Dossier de dépôt partagé"_s : u"Pas de dossier de dépôt partagé"_s;
}

QString axis(const QString &name)
{
    if (name == u"read"_s)
        return u"Lecture"_s;
    if (name == u"medium"_s)
        return u"Type"_s;
    if (name == u"universe"_s)
        return u"Univers"_s;
    if (name == u"genre"_s)
        return u"Genre"_s;
    if (name == u"author"_s)
        return u"Auteur"_s;
    if (name == u"publisher"_s)
        return u"Éditeur"_s;
    if (name == u"language"_s)
        return u"Langue"_s;
    if (name == u"status"_s)
        return u"Statut"_s;
    return {};
}

QString editionStatus(const QString &word)
{
    if (word == u"ongoing"_s)
        return u"En parution"_s;
    if (word == u"completed"_s)
        return u"Terminée"_s;
    if (word == u"hiatus"_s)
        return u"En pause"_s;
    if (word == u"cancelled"_s)
        return u"Abandonnée"_s;
    // The server may learn a third — « hiatus » was refused once and may not always be — and
    // a word this client has not been taught is shown rather than dropped: the reader can
    // still choose it, and it is their own library that is saying it.
    return word;
}

QString runAnnounced(const QString &status, int declared, std::optional<Api::Medium> medium)
{
    return status + Nbsp + u"· "_s + volumes(declared, medium)
           + (declared >= 2 ? u" annoncés"_s : u" annoncé"_s);
}

QString language(const QString &tag)
{
    const QLocale spoken(tag);
    QString name = spoken.nativeLanguageName();
    // An unknown tag gives the system language rather than nothing, which would file every
    // foreign edition under « Français ». The tag itself is the honest answer there.
    if (name.isEmpty() || spoken.language() == QLocale::C
        || (!tag.isEmpty() && spoken.name().left(2) != tag.left(2).toLower()))
        return tag;
    name[0] = name[0].toUpper();
    return name;
}

QString sortOrder(Api::Sort value)
{
    using enum Api::Sort;

    switch (value) {
    case Name:
        return u"Nom"_s;
    case Added:
        return u"Ajout"_s;
    case Volumes:
        return u"Nombre de tomes"_s;
    case Read:
        return u"Dernière lecture"_s;
    }
    return {};
}

QString sortValue(Api::Sort value, bool reversed)
{
    using enum Api::Sort;

    switch (value) {
    case Name:
        return reversed ? u"Nom · Z → A"_s : u"Nom · A → Z"_s;
    case Added:
        return reversed ? u"Ajout · ancien → récent"_s : u"Ajout · récent → ancien"_s;
    case Volumes:
        return reversed ? u"Tomes · moins → plus"_s : u"Tomes · plus → moins"_s;
    case Read:
        return reversed ? u"Lecture · ancienne → récente"_s
                        : u"Lecture · récente → ancienne"_s;
    }
    return {};
}

QString labelled(const QString &label, const QString &value)
{
    return label + Nbsp + u": "_s + value;
}

QString where(Api::UpNext::Kind kind, std::optional<double> number_, std::optional<int> page,
              int pageCount, const std::optional<QString> &chapter)
{
    return line(kind, number_, page, pageCount, chapter, false);
}

QString whereShort(Api::UpNext::Kind kind, std::optional<double> number_,
                   std::optional<int> page, int pageCount)
{
    return line(kind, number_, page, pageCount, std::nullopt, true);
}

QString resumeAction(Api::UpNext::Reason reason)
{
    using enum Api::UpNext::Reason;

    switch (reason) {
    case InProgress:
        return u"Reprendre"_s;
    case NextUp:
        return u"Continuer"_s;
    }
    return {};
}

QString files(int count)
{
    return u"Fichiers · %1"_s.arg(count);
}

QString overview()
{
    return u"Aperçu"_s;
}

QString series(int count)
{
    return u"Séries · %1"_s.arg(count);
}

QString seeAllSeries(int count)
{
    if (count == 1)
        return u"Voir la série"_s;
    return u"Voir les %1 séries"_s.arg(count);
}

QString seeAllFiles(int count)
{
    if (count == 1)
        return u"Voir le fichier"_s;
    return u"Voir les %1 fichiers"_s.arg(count);
}

QString fileContext(const Api::Hit &hit)
{
    QStringList parts;
    if (hit.seriesName && !hit.seriesName->isEmpty())
        parts << *hit.seriesName;

    // An entry already names itself on the first line. A chapter does not: add the volume
    // that contains it, then its optional title. `where` owns French number formatting.
    QString container;
    if (hit.kind == Api::Hit::Kind::Chapter && hit.entryNumber.has_value()) {
        const Api::UpNext::Kind kind = hit.entryKind.value_or(Api::UpNext::Kind::Volume);
        container = where(kind, hit.entryNumber, std::nullopt, 0, std::nullopt);
        if (!container.isEmpty())
            parts << container;
    }
    // A volume nobody titled carries its own label as its title, so the line read « Parasite
    // Reversi · Tome 2 · Tome 2 · 187 pages ». The title is worth a place only when it says
    // something the volume's own name did not.
    if (hit.entryTitle && !hit.entryTitle->isEmpty() && *hit.entryTitle != hit.label
        && *hit.entryTitle != container && *hit.entryTitle != hitName(hit))
        parts << *hit.entryTitle;
    if (hit.entryPageCount.has_value() && *hit.entryPageCount > 0) {
        parts << (u"%1 page"_s.arg(*hit.entryPageCount)
                  + (*hit.entryPageCount >= 2 ? u"s"_s : QString()));
    }
    return parts.join(u" · "_s);
}

QString hitName(const Api::Hit &hit)
{
    // What was searched is a chapter's title, or what its file said when it has none — the
    // « #13 » the rest of the application no longer shows. A chapter is named as the reader
    // names it, whenever the server says how.
    if (hit.kind == Api::Hit::Kind::Chapter && hit.chapterLabel)
        return chapterName(*hit.chapterLabel, hit.chapterTitle.value_or(QString()));
    return hit.label;
}

QString seeTheOthers(int remaining)
{
    // One is not "les 1 autres". French has a singular and this sentence is short enough to
    // carry it, so it carries it.
    if (remaining == 1)
        return u"Voir l’autre"_s;
    return u"Voir les %1 autres"_s.arg(remaining);
}

QString didYouMean(const QString &name)
{
    return u"Vouliez-vous dire %1"_s.arg(name) + Nbsp + u"?"_s;
}

QString searchHint()
{
    return u"Rechercher une série, un tome, un chapitre…"_s;
}

QString searchHintShort()
{
    return u"Rechercher…"_s;
}

QString clearTheSearch()
{
    return u"Effacer la recherche"_s;
}

QString filter()
{
    return u"Filtrer"_s;
}

QString noSeriesByThatName()
{
    return u"Aucune série ne porte ce nom."_s;
}

QString nothingFound()
{
    return u"Rien dans cette bibliothèque ne répond à cette recherche. Essayez un autre mot, "
           u"ou le nom d’un auteur."_s;
}

QString nothingHere(const QStringList &pills, int withoutThem)
{
    // The pills arrive already worded, so this sentence names them exactly as the bar does.
    // Section 05's rule still applies to the sentence itself: one capital, at the start.
    return u"Aucun résultat dans %1 · %2 sans les filtres"_s.arg(pills.join(u", "_s))
        .arg(withoutThem);
}

QString destination(Navigation::Destination value)
{
    using enum Navigation::Destination;

    switch (value) {
    case Shelf:
        return u"Étagère"_s;
    case Series:
        return u"Série"_s;
    case Reader:
        return u"Lecteur"_s;
    case Health:
        return u"Santé"_s;
    case Settings:
        return u"Réglages"_s;
    }
    return {};
}

QString band(Widths::Band value)
{
    using enum Widths::Band;

    switch (value) {
    case Wide:
        return u"Large"_s;
    case Medium:
        return u"Moyenne"_s;
    case Narrow:
        return u"Étroite"_s;
    }
    return {};
}

QString readerVolume(double number_)
{
    return entry(Api::UpNext::Kind::Volume, number_, false);
}

QString readerChapter(double number_)
{
    return entry(Api::UpNext::Kind::Chapter, number_, false);
}

QString readOn(Api::Entry::Kind kind, double number_)
{
    return (kind == Api::Entry::Kind::Chapter ? u"Lire le chapitre "_s : u"Lire le tome "_s)
           + number(number_);
}

QString finishedVolume(double number_)
{
    return readerVolume(number_) + u" terminé"_s;
}

QString finishedChapter(double number_)
{
    return readerChapter(number_) + u" terminé"_s;
}

QString lastOfSeries(Api::Entry::Kind kind)
{
    return kind == Api::Entry::Kind::Chapter ? u"dernier chapitre de la série"_s
                                             : u"dernier tome de la série"_s;
}

QString ending(Api::Entry::Kind kind, double number_, bool last)
{
    const QString finished = kind == Api::Entry::Kind::Chapter ? finishedChapter(number_)
                                                               : finishedVolume(number_);
    if (!last)
        return finished;
    return finished + Nbsp + u"· "_s + lastOfSeries(kind);
}

QString readerWhere(const QString &entry, const QString &chapter, const QString &title)
{
    const QString named = chapterName(chapter, title);
    if (named.isEmpty())
        return entry;
    // A chapter that arrived on its own is named by its chapter, and saying it twice read
    // « Chap. 13 · Chap. 13 : Le réveil ».
    if (entry.isEmpty() || saysNothingMore(entry, chapter))
        return named;
    return entry + Nbsp + u"· "_s + named;
}

QString readerScrubbing(const QString &chapter, const QString &title, int page)
{
    // `chapterName` keeps the rule the bar follows: « #13 · #13 · page 4 » was still possible
    // here after the bar's line had learned to say a chapter once (the test, 22/10/2026).
    const QString named = chapterName(chapter, title);
    return (named.isEmpty() ? QString() : named + Nbsp + u"· "_s) + u"page "_s
           + QString::number(page);
}

QString readerBesideLabel(const QString &label, const QString &title)
{
    // Drawn right after the label, so a title that only repeats it read « #13 · #13 » under
    // the bar (the test, 22/10/2026): it is the label's to say, once. What it does say
    // follows the colon `chapterName` puts everywhere else a chapter is named.
    if (label.trimmed().isEmpty())
        return title.trimmed();
    if (saysNothingMore(title, label))
        return QString();
    return Nbsp + u": "_s + title.trimmed();
}

QString readerVolumeEnd(const QString &label, const QString &title)
{
    const QString end = u"fin du tome"_s;
    // The label alone, the line being half a caption wide; the title only for a chapter the
    // file asked to be called by its title, which has no label to give.
    const QString lastChapter = label.trimmed().isEmpty() ? title.trimmed() : label.trimmed();
    return lastChapter.isEmpty() ? end : lastChapter + Nbsp + u"· "_s + end;
}

QString readerOutOf(int count)
{
    return u"/"_s + Nbsp + QString::number(count);
}

QString readerZoom(qreal level)
{
    return QString::number(qRound(level * 100)) + NarrowNbsp + u"%"_s;
}

QString readerLoupe()
{
    return u"Zoom temporaire · Échap"_s;
}

QString readerAmong(int count)
{
    return u"sur"_s + Nbsp + QString::number(count);
}

QString readerZoomStep(bool larger)
{
    return larger ? u"Agrandir"_s : u"Réduire"_s;
}

QString readerAutoRead(bool running)
{
    return running ? u"Arrêter la lecture automatique"_s : u"Lecture automatique"_s;
}

QString readerChapterSpan(double from, double to)
{
    if (std::abs(to - from) < 0.0001)
        return number(from);
    return u"%1 à %2"_s.arg(number(from), number(to));
}

QString readerVolumeHead(const QString &entry, const QString &title)
{
    return saysNothingMore(title, entry) ? entry : entry + Nbsp + u"· "_s + title;
}

QString fileFact(FileFact which)
{
    using enum FileFact;
    switch (which) {
    case File:
        return u"Fichier"_s;
    case Size:
        return u"Taille"_s;
    case Added:
        return u"Ajouté le"_s;
    }
    return {};
}

QString readerFact(ReaderFact which)
{
    switch (which) {
    case ReaderFact::Series:
        return u"Série"_s;
    case ReaderFact::Pages:
        return u"Pages"_s;
    case ReaderFact::Chapters:
        return u"Chapitres"_s;
    case ReaderFact::Published:
        return u"Paru le"_s;
    case ReaderFact::Isbn:
        return u"ISBN"_s;
    case ReaderFact::Read:
        return u"Lu"_s;
    }
    return {};
}

QString readerCopyIsbn()
{
    return u"Cliquer pour copier"_s;
}

QString readerPublished(const QString &iso)
{
    const QDate date = QDate::fromString(iso, Qt::ISODate);
    if (!date.isValid())
        return iso;
    // « 1er » and not « 1 »: the first of the month is the one day French writes as an
    // ordinal.
    const QString day = date.day() == 1 ? u"1er"_s : QString::number(date.day());
    return u"%1 %2 %3"_s.arg(day, QLocale(QLocale::French).monthName(date.month(), QLocale::LongFormat),
                             QString::number(date.year()));
}

QVariantMap editing()
{
    // « Janvier », « Février »: a list of twelve says them as the dropdown lists them, and the
    // locale spells them, so no month is typed here.
    QVariantList months;
    const QLocale french(QLocale::French);
    for (int month = 1; month <= 12; ++month) {
        const QString name = french.monthName(month, QLocale::LongFormat);
        months.append(name.left(1).toUpper() + name.mid(1));
    }
    QVariantMap words = {{u"series"_s, u"Modifier la série"_s},
            {u"entry"_s, u"Modifier le tome"_s},
            {u"title"_s, u"Titre"_s},
            {u"summary"_s, u"Résumé"_s},
            {u"authors"_s, u"Scénario"_s},
            {u"artists"_s, u"Dessin"_s},
            {u"genres"_s, u"Genres"_s},
            {u"tags"_s, u"Étiquettes"_s},
            {u"publisher"_s, u"Éditeur"_s},
            {u"collection"_s, u"Collection"_s},
            {u"language"_s, u"Langue"_s},
            {u"ageRating"_s, u"Âge"_s},
            {u"volumeCount"_s, u"Nombre de tomes"_s},
            {u"status"_s, u"Statut"_s},
            {u"medium"_s, u"Type"_s},
            {u"number"_s, u"Numéro"_s},
            {u"isbn"_s, u"ISBN"_s},
            {u"publishedOn"_s, u"Parution"_s},
            {u"list"_s, u"Chercher ou créer, Entrée pour ajouter"_s},
            {u"create"_s, u"Créer « %1 »"_s},
            {u"dateHint"_s, u"JJ/MM/AAAA"_s},
            {u"dateShape"_s, u"La date s\u{2019}écrit JJ/MM/AAAA, MM/AAAA ou AAAA."_s},
            {u"dateNeedsYear"_s, u"Indiquez l\u{2019}année de parution."_s},
            {u"dateYearInvalid"_s, u"L\u{2019}année s\u{2019}écrit avec quatre chiffres."_s},
            {u"dateNeedsMonth"_s, u"Indiquez le mois de parution."_s},
            {u"dateMonthInvalid"_s, u"Ce mois n\u{2019}existe pas."_s},
            {u"dateDayInvalid"_s, u"Ce jour n\u{2019}existe pas dans ce mois."_s},
            {u"openCalendar"_s, u"Choisir dans le calendrier"_s},
            {u"calendar"_s, u"Calendrier"_s},
            {u"calendarYear"_s, u"Année affichée"_s},
            {u"previousMonth"_s, u"Mois précédent"_s},
            {u"nextMonth"_s, u"Mois suivant"_s},
            {u"previousYear"_s, u"Année précédente"_s},
            {u"nextYear"_s, u"Année suivante"_s},
            {u"cover"_s, u"Couverture"_s},
            {u"noCover"_s, u"Aucune couverture"_s},
            {u"chooseImage"_s, u"Choisir une image…"_s},
            {u"choosePage"_s, u"Choisir une page…"_s},
            {u"chooseVolume"_s, u"Choisir un tome…"_s},
            {u"changeCover"_s, u"Changer…"_s},
            {u"backToFirst"_s, u"Revenir au tome 1"_s},
            {u"backToDefault"_s, u"Revenir par défaut"_s},
            {u"imageFiles"_s, u"Images (*.jpg *.jpeg *.png *.webp)"_s},
            {u"chooseImageTitle"_s, u"Choisir l\u{2019}image de couverture"_s},
            {u"choosePageTitle"_s, u"Choisir la page de couverture"_s},
            {u"chooseVolumeTitle"_s, u"Choisir le tome de couverture"_s},
            {u"pageNumber"_s, u"Page %1"_s},
            {u"coverFirst"_s, u"Couverture du tome 1"_s},
            {u"coverDefaultVolume"_s, u"Couverture par défaut"_s},
            {u"coverChosenPage"_s, u"Page %1 du tome"_s},
            {u"coverChosenImage"_s, u"Image choisie"_s},
            {u"coverUnreadable"_s, u"Cette image ne peut pas être lue."_s},
            {u"coverTooLarge"_s, u"Cette image dépasse 20 Mo."_s},
            {u"numberMissing"_s, u"Le numéro est obligatoire."_s},
            {u"numberInvalid"_s, u"Le numéro doit être un nombre, comme 12 ou 2,5."_s},
            {u"unsaid"_s, u"Non précisé"_s},
            {u"ongoing"_s, u"En cours"_s},
            {u"completed"_s, u"Terminée"_s},
            {u"hiatus"_s, u"En pause"_s},
            {u"cancelled"_s, u"Abandonnée"_s},
            {u"save"_s, u"Enregistrer"_s},
            {u"cancel"_s, u"Annuler"_s},
            {u"rewrites"_s, u"Le fichier sera réécrit"_s + Nbsp
                                + u": l\u{2019}archive est modifiée sur le disque."_s},
            {u"ownSummary"_s, u"Ce résumé est celui de cette édition."_s},
            {u"workSummary"_s, u"Ce résumé est celui de l\u{2019}œuvre, commun à ses éditions."_s}};
    words.insert(u"months"_s, months);
    // « L M M J V S D », Monday first as France reads a week; the locale spells the initials.
    QVariantList weekdays;
    for (int day = 1; day <= 7; ++day)
        weekdays.append(french.dayName(day, QLocale::NarrowFormat).toUpper());
    words.insert(u"weekdays"_s, weekdays);
    return words;
}

QString readerReadAt(int page, int count, bool finished)
{
    if (finished)
        return u"terminé"_s;
    return u"page %1"_s.arg(page) + Nbsp + u"/"_s + Nbsp + QString::number(count);
}

QString readerChapterPage(int page)
{
    return u"p."_s + Nbsp + QString::number(page);
}

QString readerVolumePanel()
{
    return u"Le tome"_s;
}

QString readerSettings()
{
    return u"Réglages de lecture"_s;
}

QString readerFullScreen(bool on)
{
    return on ? u"Plein écran"_s : u"Quitter le plein écran"_s;
}

QString readerPage(bool forward)
{
    return forward ? u"Page suivante"_s : u"Page précédente"_s;
}

QString readerChapterStep(bool forward)
{
    return forward ? u"Chapitre suivant"_s : u"Chapitre précédent"_s;
}

QString readerCardTitle()
{
    return u"Lecture"_s;
}

QString readerCloseCard()
{
    return u"Fermer les réglages"_s;
}

QString readerBackToSeries()
{
    return u"Revenir à la série"_s;
}

QString readerStayHere()
{
    return u"Rester sur cette page"_s;
}

QString readerThisSeries()
{
    return u"Pour cette série"_s;
}

QString readerThisDesk()
{
    return u"Sur cet appareil"_s;
}

QString readerDirection(std::optional<Api::ReadingDirection> which)
{
    if (!which)
        return u"Par défaut"_s;
    switch (*which) {
    case Api::ReadingDirection::LeftToRight:
        return u"De gauche à droite"_s;
    case Api::ReadingDirection::RightToLeft:
        return u"De droite à gauche"_s;
    case Api::ReadingDirection::Vertical:
        break;
    }
    return u"De haut en bas"_s;
}

QString readerLayout(Preferences::PageLayout which)
{
    switch (which) {
    case Preferences::PageLayout::Single:
        return u"Simple"_s;
    case Preferences::PageLayout::Double:
        return u"Deux pages"_s;
    case Preferences::PageLayout::Auto:
        break;
    }
    return u"Auto"_s;
}

QString readerFit(Preferences::Fit which)
{
    switch (which) {
    case Preferences::Fit::Height:
        return u"Hauteur"_s;
    case Preferences::Fit::Width:
        return u"Largeur"_s;
    case Preferences::Fit::Original:
        break;
    }
    return u"Original"_s;
}

QString readerBackdrop(Preferences::Backdrop which)
{
    switch (which) {
    case Preferences::Backdrop::Grey:
        return u"Gris"_s;
    case Preferences::Backdrop::White:
        return u"Blanc"_s;
    case Preferences::Backdrop::Black:
        break;
    }
    return u"Noir"_s;
}

QString readerMotion(Preferences::Motion which)
{
    switch (which) {
    case Preferences::Motion::Fade:
        return u"Fondu"_s;
    case Preferences::Motion::Instant:
        return u"Sans effet"_s;
    case Preferences::Motion::Slide:
        break;
    }
    return u"Glisser"_s;
}

QString readerDeskGroup(ReaderDeskGroup which)
{
    using enum ReaderDeskGroup;
    switch (which) {
    case Pages:
        return u"Pages"_s;
    case Scrolling:
        return u"Défilement"_s;
    case Controls:
        return u"Commandes"_s;
    case Comfort:
        break;
    }
    return u"Confort"_s;
}

QString readerSetting(ReaderSetting which)
{
    switch (which) {
    case ReaderSetting::Layout:
        return u"Mise en page"_s;
    case ReaderSetting::Fit:
        return u"Ajustement"_s;
    case ReaderSetting::Backdrop:
        return u"Fond"_s;
    case ReaderSetting::Sensitivity:
        return u"Sensibilité"_s;
    case ReaderSetting::AutoSpeed:
        return u"Lecture automatique"_s;
    case ReaderSetting::Controls:
        return u"Commandes"_s;
    case ReaderSetting::Motion:
        break;
    }
    return u"Changement de page"_s;
}

QString readerDeclares(std::optional<Api::ReadingDirection> declared)
{
    // What the files say, as a path between two edges. A series that says nothing is read
    // from the left, so « Par défaut » has something to mean even then.
    const auto along = [](const QString &from, const QString &to) {
        return u"<b>"_s + from + Nbsp + u"→"_s + Nbsp + to + u"</b>"_s;
    };
    QString said;
    if (!declared) {
        said = u"La série ne déclare rien"_s + Nbsp + u": "_s
               + along(u"gauche"_s, u"droite"_s) + u" par défaut"_s;
    } else {
        using enum Api::ReadingDirection;
        const QString way = *declared == RightToLeft ? along(u"droite"_s, u"gauche"_s)
                            : *declared == Vertical  ? along(u"haut"_s, u"bas"_s)
                                                     : along(u"gauche"_s, u"droite"_s);
        said = u"La série déclare"_s + Nbsp + u": "_s + way;
    }
    return said + u". Ce que vous choisissez ici ne vaut que pour cette série, mais sur tous vos "
                  u"appareils."_s;
}

QString readerDeskNote()
{
    return u"Pour tous les livres, sur ce bureau seulement."_s;
}

QString readerStripDeskNote()
{
    return u"Pour tous les webtoons, sur ce bureau seulement."_s;
}

QString readerStepEnd(StepEnd which)
{
    using enum StepEnd;
    switch (which) {
    case Gentle:
        return u"Douce"_s;
    case Lively:
        return u"Vive"_s;
    case Slow:
        return u"Lente"_s;
    case Fast:
        break;
    }
    return u"Rapide"_s;
}

QString readerStripControls(Preferences::StripControls which)
{
    using enum Preferences::StripControls;
    switch (which) {
    case OnTheWayUp:
        return u"En remontant"_s;
    case OnClick:
        return u"Au clic"_s;
    case Discreet:
        break;
    }
    return u"Discrètes"_s;
}

QString readerStripControlsHint(Preferences::StripControls which)
{
    using enum Preferences::StripControls;
    switch (which) {
    case OnTheWayUp:
        return u"En descendant. Reviennent en remontant d’un quart d’écran, au clic ou à Tab."_s;
    case OnClick:
        return u"Au clic ou à Tab seulement. Défiler ne les touche pas."_s;
    case Discreet:
        break;
    }
    return u"En descendant. Reviennent au clic ou à Tab."_s;
}

QString readerStripAtRest()
{
    return u"Reviennent aussi à l’arrêt, après 5"_s + Nbsp + u"s sans défiler"_s;
}

QString readerShortcuts()
{
    return u"Raccourcis"_s;
}

QString shortcutGroup(ShortcutGroup which)
{
    switch (which) {
    case ShortcutGroup::Navigate:
        return u"Naviguer"_s;
    case ShortcutGroup::Display:
        return u"Affichage"_s;
    case ShortcutGroup::Zoom:
        return u"Zoom"_s;
    case ShortcutGroup::Leave:
        return u"Quitter"_s;
    }
    return {};
}

QString shortcutDoes(ShortcutDoes which)
{
    using enum ShortcutDoes;
    switch (which) {
    case TurnByArrow:
        return u"Page précédente, suivante"_s;
    case TurnByArrowRightToLeft:
        return u"Page suivante, précédente"_s;
    case TurnBySpace:
        return u"Page suivante, précédente"_s;
    case ScrollByArrow:
        return u"Défiler"_s;
    case ScrollBySpace:
        return u"Descendre, remonter d’un écran"_s;
    case ToTheEndsOfChapter:
        return u"Début, fin du chapitre"_s;
    case AutoRead:
        return u"Lecture automatique"_s;
    case ToTheEnds:
        return u"Première, dernière page"_s;
    case ToTheEndsRightToLeft:
        return u"Dernière, première page"_s;
    case BackAPage:
        return u"Page précédente"_s;
    case FullScreen:
        return u"Plein écran"_s;
    case VolumeInfo:
        return u"Les infos du tome"_s;
    case ReaderSettings:
        return u"Les réglages de lecture"_s;
    case TheseShortcuts:
        return u"Ces raccourcis"_s;
    case ZoomByKey:
        return u"Zoomer, dézoomer"_s;
    case BackToWhole:
        // « 100 % », the sign held to its figure as `readerZoom` holds it.
        return u"Revenir à 100"_s + NarrowNbsp + u"%"_s;
    case ZoomByWheel:
        return u"Zoomer à la molette, autour du curseur"_s;
    case ZoomWhereAimed:
        return u"Zoomer là où l’on vise"_s;
    case Pinch:
        return u"Pincer au pavé tactile"_s;
    case CloseWhatIsOpen:
        return u"Fermer ce qui est ouvert"_s;
    case BackToSeries:
        return u"Revenir à la série"_s;
    case CloseReader:
        return u"Fermer le lecteur"_s;
    }
    return {};
}

QString keyName(KeyName which)
{
    using enum KeyName;
    switch (which) {
    case Space:
        return u"Espace"_s;
    case Shift:
        return u"Maj"_s;
    case Ctrl:
        return u"Ctrl"_s;
    case Alt:
        return u"Alt"_s;
    case Escape:
        return u"Échap"_s;
    case F:
        return u"F"_s;
    case F11:
        return u"F11"_s;
    case I:
        return u"I"_s;
    case Question:
        return u"?"_s;
    case F1:
        return u"F1"_s;
    case Zero:
        return u"0"_s;
    case W:
        return u"W"_s;
    case Enter:
        return u"Entrée"_s;
    case Tab:
        return u"Tab"_s;
    case Menu:
        return u"Menu"_s;
    case F10:
        return u"F10"_s;
    case Slash:
        return u"/"_s;
    case A:
        return u"A"_s;
    case Home:
        return u"Début"_s;
    case End:
        return u"Fin"_s;
    case S:
        return u"S"_s;
    }
    return {};
}

QString shortcutAnd()
{
    return u"+"_s;
}

QString shortcutSlash()
{
    return u"/"_s;
}

QString shortcutOr()
{
    return u"ou"_s;
}

QString doubleClick()
{
    return u"double-clic"_s;
}

QString notSetUp(Asking what)
{
    using enum Asking;

    QString tail;
    switch (what) {
    case Shelf:
        tail = u"afficher"_s;
        break;
    case Search:
        tail = u"rechercher"_s;
        break;
    case Filters:
        tail = u"filtrer"_s;
        break;
    case Resume:
        tail = u"reprendre"_s;
        break;
    case State:
        tail = u"demander"_s;
        break;
    case Reader:
        tail = u"lire"_s;
        break;
    }
    if (tail.isEmpty())
        return {};
    return u"Leaf n’a pas pu s’installer"_s + Nbsp + u": il n’y a rien à "_s
            + tail + u"."_s;
}

QString noLibrary()
{
    return u"Leaf ne sait pas où est votre bibliothèque."_s;
}

QString unreachable(const QString &why)
{
    return u"Le serveur est injoignable — %1"_s.arg(why);
}

QString unreadableAnswer()
{
    return u"Leaf n’a pas pu lire la réponse du serveur : ce n’est pas ce qu’il attend. "
           u"Le serveur et l’application ne sont sans doute pas de la même version."_s;
}

QString answerNotUnderstood(const QString &raw)
{
    // « opened: present is missing » is how the client names a field the server did not send:
    // the part, a colon, the field and what is wrong with it. Said in plain words, with the
    // part and the field kept between quotes for whoever has to find them in the code.
    const qsizetype colon = raw.indexOf(u": "_s);
    if (colon <= 0 || raw.indexOf(u' ') < colon)
        return raw;
    const QString part = raw.left(colon);
    QString detail = raw.mid(colon + 2);
    if (detail.endsWith(u" is missing"_s))
        detail = u"il manque « %1 »"_s.arg(detail.chopped(11));
    return u"Leaf n’a pas compris la réponse du serveur : %1 dans « %2 ». "
           u"Le serveur et l’application ne sont sans doute pas de la même version : "
           u"mettez le serveur à jour, puis réessayez. Rien ne se corrige dans cette fiche."_s
        .arg(detail, part);
}

QString keyRefused(const QString &said)
{
    if (said.isEmpty())
        return u"La clé a été refusée."_s;
    return u"La clé a été refusée"_s + Nbsp + u": "_s + said;
}

QString certificateMismatch()
{
    return u"Le serveur à cette adresse ne se présente pas comme la bibliothèque attendue. Par "
           u"prudence, Leaf ne lui envoie rien"_s
           + Nbsp + u": prévenez la personne qui a installé la bibliothèque."_s;
}

QString noLibraryLinked()
{
    return u"Aucune bibliothèque reliée"_s;
}

QString linkIntro()
{
    return u"Ouvrez le lien de liaison sur cet appareil, ou collez-le ici. Un appareil déjà "
           u"relié peut l’afficher, ou la personne qui a installé la bibliothèque vous le "
           u"donner."_s;
}

QString linkField()
{
    return u"Lien de liaison"_s;
}

QString linkPlaceholder()
{
    return u"leaf://…"_s;
}

QString deviceNameField()
{
    return u"Nom de cet appareil"_s;
}

QString deviceNameHelp()
{
    return u"Pré-rempli avec le nom de la machine. C’est celui que le serveur affichera."_s;
}

QString linkAction(bool found)
{
    return found ? u"Relier à cette bibliothèque"_s : u"Relier"_s;
}

QString lookForTheLibrary()
{
    return u"Chercher la bibliothèque"_s;
}

QString lookingForTheLibrary()
{
    return u"Leaf cherche la bibliothèque à cette adresse…"_s;
}

QString libraryFound(const QString &host)
{
    return u"Bibliothèque trouvée"_s + Nbsp + u": %1"_s.arg(host);
}

QString libraryFoundDetail(bool pinned, int series)
{
    const QString holds = libraryHolds(series);
    return pinned ? u"Vérifiée · %1"_s.arg(holds) : holds;
}

QString notALink()
{
    return u"Ce n’est pas un lien de liaison. Collez celui qu’a donné un appareil relié ou la "
           u"personne qui a installé la bibliothèque"_s
           + Nbsp + u": il commence par leaf://."_s;
}

QString noLibraryAnswers()
{
    return u"Aucune bibliothèque Leaf ne répond à ce lien depuis cet appareil. Si on vous a "
           u"donné plusieurs liens, essayez-en un autre."_s;
}

QString linkNoLongerValid()
{
    return u"Ce lien n’est plus valable"_s + Nbsp + u": demandez-en un nouveau."_s;
}

QString nameTaken(const QString &name)
{
    return u"Un autre appareil s’appelle déjà %1."_s.arg(name);
}

QString nameNotAccepted()
{
    return u"Ce nom ne convient pas"_s + Nbsp
           + u": de 1 à 40 caractères, sans caractère de contrôle."_s;
}

QString tooManyTries(const QString &seconds)
{
    return u"Trop d’essais ratés depuis cet appareil. Attendez %1."_s.arg(waitOf(seconds));
}

QString tooManyWrongKeys(const QString &seconds)
{
    return u"Trop de clés fausses ont été essayées. Attendez %1."_s.arg(waitOf(seconds));
}

QString noSuchThing()
{
    return u"Il n’y a rien de tel ici."_s;
}

QString serverAnswered(int status, const QString &said)
{
    if (said.isEmpty())
        return u"Le serveur a répondu %1."_s.arg(status);
    return u"Le serveur a répondu %1"_s.arg(status) + Nbsp + u": "_s + said;
}

QString waitingBeforeAsking(int seconds)
{
    return u"Encore %1 avant de redemander."_s.arg(plural(seconds, u"%1 seconde"_s, u"%1 secondes"_s));
}

QString tooManyWaiting()
{
    return u"Trop de demandes attendent déjà que Leaf ouvre votre bibliothèque."_s;
}

QString queryBelongsApart()
{
    return u"Un paramètre se donne à part du chemin, pas collé dedans."_s;
}

QString nothingConfigured(const QString &file)
{
    return noLibrary() + u"\nMettez une adresse et une clé dans %1, "
                        u"ou dans LEAF_ADDRESS et LEAF_KEY."_s.arg(file);
}

QString noAddress(const QString &file)
{
    return u"Pas d’adresse pour le serveur. Posez-la dans %1, ou dans LEAF_ADDRESS."_s
        .arg(file);
}

QString noKey(const QString &file)
{
    return u"Pas de clé pour le serveur. "
           u"Posez-la dans %1, dans LEAF_KEY, ou dans le trousseau."_s.arg(file);
}

QString readableByOthers(const QString &path)
{
    return u"%1 est lisible par d’autres que vous, il n’a donc pas été lu "
           u"du tout. Faites-en un chmod 600."_s.arg(path);
}

QString settingsFromEnvironment()
{
    return u"L’adresse et la clé de cet appareil sont fixées au lancement de Leaf, qui ne peut "
           u"pas les changer. Demandez à la personne qui a configuré cet appareil de les "
           u"retirer."_s;
}

QString keyringKeptTheKey()
{
    return u"La clé n’a pas pu être retirée du trousseau de la session. Retirez-la à la main, "
           u"entrée server-key du service Leaf."_s;
}

QString linkedLibrary()
{
    return u"Bibliothèque reliée"_s;
}

QString thisDevice()
{
    return u"Cet appareil"_s;
}

QString linkedOnLabel()
{
    return u"Relié le"_s;
}

QString linkedOn(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    return QLocale(QLocale::French).toString(when.toLocalTime().date(), u"d MMMM yyyy"_s);
}

QString renameDevice()
{
    return u"Renommer"_s;
}

QString changeAddress()
{
    return u"Modifier l’adresse"_s;
}

QString addressPlaceholder()
{
    return u"https://…"_s;
}

QString tryThisAddress()
{
    return u"Essayer cette adresse"_s;
}

QString tryingTheAddress()
{
    return u"Leaf essaie la clé de cet appareil à cette adresse…"_s;
}

QString notAnAddress()
{
    return u"Ce n’est pas une adresse de bibliothèque. Elle s’écrit comme "
           u"https://leaf.exemple.fr."_s;
}

QString httpsElsewhere()
{
    return u"Vers une autre machine, l’adresse d’une bibliothèque commence par https://."_s;
}

QString keyUnknownThere()
{
    return u"La bibliothèque à cette adresse ne connaît pas la clé de cet appareil"_s + Nbsp
           + u": ce n’est pas la même. L’adresse n’a pas changé."_s;
}

QString unlinkDevice()
{
    return u"Délier cet appareil"_s;
}

QString unlinkQuestion()
{
    return u"Délier cet appareil"_s + Nbsp + u"?"_s;
}

QString unlinkWhat(const QString &name)
{
    return u"Sa clé est retirée du serveur"_s + Nbsp
           + u": %1 ne pourra plus lire la bibliothèque. Rien n’est supprimé de la "
             u"bibliothèque, et la progression reste sur le serveur."_s.arg(
               name.isEmpty() ? u"cet appareil"_s : name);
}

QString unlinkComeBack()
{
    return u"Pour revenir, il faudra un nouveau code."_s;
}

QString unlinkIfSilent(const QString &name)
{
    // Configured by hand, the device has no name here: the server's list has it. The command
    // that removes it is the README's to give; a screen says who can run it.
    return u"Cet appareil oubliera la bibliothèque. Si le serveur ne répond pas, demandez à la "
           u"personne qui l’a installée de retirer %1 de la liste des appareils de la "
           u"bibliothèque."_s.arg(name.isEmpty() ? u"cet appareil"_s : name);
}

QString unlinkedWithoutTheServer(const QString &name)
{
    return u"Cet appareil a oublié la bibliothèque, mais le serveur n’a pas confirmé"_s + Nbsp
           + u": demandez à la personne qui l’a installée de retirer %1 de la liste des "
             u"appareils de la bibliothèque."_s.arg(name.isEmpty() ? u"cet appareil"_s : name);
}

QString unlinkedAnOldKey()
{
    // No key in the sentence: it is on a screen, and a screen is photographed and shared.
    return u"Cet appareil a oublié la bibliothèque. Il y entrait avec une ancienne clé, que le "
           u"serveur ne peut pas retirer seul"_s
           + Nbsp + u": demandez à la personne qui a installé la bibliothèque de la supprimer."_s;
}

QString unlinkGo()
{
    return u"Délier"_s;
}

QString deviceRevoked()
{
    return u"Cet appareil a été retiré de la bibliothèque. Reliez-le de nouveau pour la lire."_s;
}

QString revokedUnderEnvironment()
{
    return u"La bibliothèque ne reconnaît plus la clé de cet appareil. Elle est fixée au "
           u"lancement de Leaf, qui ne peut pas l’oublier"_s
           + Nbsp + u": demandez à la personne qui a configuré cet appareil de la retirer, puis "
                    u"reliez-le."_s;
}

QString cutShortByANewPin()
{
    return u"La demande a été interrompue parce que l’adresse de la bibliothèque a changé. "
           u"Elle n’est peut-être pas arrivée"_s + Nbsp + u": refaites-la."_s;
}

QString relayAnother()
{
    return u"Relier un autre appareil"_s;
}

QString relayIntro()
{
    // Unbroken between the number and its unit: « 10 » alone at the end of a line reads as
    // ten of something the next line has to say.
    return u"Ouvrez ce lien sur l’appareil à relier, ou scannez le code QR. Valable 10"_s
           + Nbsp + u"minutes, une seule fois."_s;
}

QString copyTheLink()
{
    return u"Copier"_s;
}

QString linkCopied()
{
    return u"Copié"_s;
}

QString relayTimeLeft(int seconds)
{
    const int left = qMax(0, seconds);
    return u"Valable encore %1:%2 · une seule fois"_s.arg(left / 60).arg(left % 60, 2, 10,
                                                                          QChar(u'0'));
}

QString deviceRelayed(const QString &name)
{
    return name + u" relié"_s;
}

QString relaySameNetwork()
{
    return u"Ce lien fonctionne sur le même réseau que la bibliothèque."_s;
}

QString onlyThisComputer()
{
    return u"Cette bibliothèque n’est accessible que depuis cet ordinateur."_s;
}

QString relayRanOut()
{
    return u"Ce lien n’est plus valable. Demandez-en un autre."_s;
}

QString anotherLink()
{
    return u"Un autre lien"_s;
}

QString closeThePanel()
{
    return u"Fermer"_s;
}

QString importing()
{
    return u"Importer"_s;
}

QString dropFilesHere()
{
    return u"Déposez des dossiers ou des CBZ"_s;
}

QString toSendTotal(int files, qint64 bytes)
{
    return counted(files, u"fichier à envoyer"_s, u"fichiers à envoyer"_s) + u" · "_s + size(bytes);
}

QString recapTitle(int volumes)
{
    return u"Prêt à envoyer"_s + Nbsp + u": "_s + counted(volumes, u"tome"_s, u"tomes"_s);
}

QString recapAbout(qint64 bytes, int cards)
{
    return size(bytes) + u" · "_s + counted(cards, u"série"_s, u"séries"_s)
           + u" · rien n’a encore été envoyé"_s;
}

QString recapExplains()
{
    return u"Voici ce que le serveur va recevoir, tel que vous l’avez réglé à l’étape "
           u"précédente. Aucune case à cocher ici"_s
           + Nbsp + u": tout ce qui est dans la liste part."_s;
}

QString recapSeries()
{
    return u"Série"_s;
}

QString recapVolumes()
{
    return u"Tomes"_s;
}

QString recapSize()
{
    return u"Taille"_s;
}

QString recapState()
{
    return u"État"_s;
}

QString recapVerifies()
{
    return u"Choisi à l’étape 1. Chaque page est comparée à l’arrivée, modifications de "
           u"l’étape 2 comprises"_s
           + Nbsp + u"; un tome qui diffère n’est pas installé. Plus long, plus sûr."_s;
}

QString importAll()
{
    return u"Importer tout"_s;
}

QString finishIt()
{
    return u"Terminer"_s;
}

QString sendingTitle(int done, int whole)
{
    return u"Envoi en cours"_s + Nbsp + u": "_s + counted(done, u"tome"_s, u"tomes"_s)
           + u" sur "_s + QString::number(whole);
}

QString sendingAbout(qint64 sent, qint64 whole)
{
    return howFar(sent, whole);
}

QString sendingExplains()
{
    return u"Le bouton rond de chaque ligne lance, met en pause (vraiment) ou reprend. ✕ remet "
           u"la ligne à 0"_s
           + Nbsp + u": elle reste dans la liste et peut être relancée."_s;
}

QString bannerExplains(bool going, bool finished)
{
    if (!going)
        return recapExplains();
    return finished ? QString() : sendingExplains();
}

QString sentTitle(int done, int whole)
{
    // « Envoi terminé : 0 tome sur 76 » read as a success that had sent nothing.
    if (done == whole)
        return u"Envoi terminé"_s + Nbsp + u": "_s + counted(done, u"tome"_s, u"tomes"_s);
    return u"Envoi interrompu"_s + Nbsp + u": "_s + counted(done, u"tome"_s, u"tomes"_s)
           + u" sur "_s + QString::number(whole);
}

QString sentAbout(qint64 bytes, int failed)
{
    return failed > 0 ? size(bytes) + u" envoyés · "_s
                            + counted(failed, u"ligne en échec"_s, u"lignes en échec"_s)
                      : size(bytes) + u" envoyés"_s;
}

QString recapFoot()
{
    return u"Importer tout lance l’envoi. Vous pourrez fermer la fenêtre"_s + Nbsp
           + u": il continue."_s;
}

QString sendingFoot()
{
    return u"Vous pouvez fermer cette fenêtre"_s + Nbsp + u": l’envoi continue."_s;
}

QString percentSent(double fraction)
{
    return QString::number(int(std::clamp(fraction, 0.0, 1.0) * 100.0)) + Nbsp + u"%"_s;
}

QString lineAbout(Manifest::Level level, int works, int editions, const QString &state,
                  int origin)
{
    using enum Manifest::Level;
    QStringList parts{levelTitle(level)};
    if (level == Universe && works > 0)
        parts << counted(works, u"série"_s, u"séries"_s);
    if ((level == Universe || level == Work) && editions > 0)
        parts << counted(editions, u"édition"_s, u"éditions"_s);
    if (!state.isEmpty())
        parts << state;
    if (origin == 0)
        parts << u"JSON Leaf lu"_s;
    else if (origin == 1)
        parts << u"ComicInfo.xml lu"_s;
    return parts.join(u" · "_s);
}

QString lineQueued(const QString &about)
{
    return u"En attente · "_s + about;
}

QString linePaused(const QString &about)
{
    return u"En pause · "_s + about;
}

QString lineResetting()
{
    return u"Remise à zéro…"_s;
}

QString lineStart()
{
    return u"Envoyer cette ligne"_s;
}

QString lineStop()
{
    return u"Mettre en pause"_s;
}

QString lineRetry()
{
    return u"Relancer"_s;
}

QString lineCancel()
{
    return u"Remettre à zéro"_s;
}

QString heldNotResent(int files)
{
    if (files <= 0)
        return {};
    return files == 1 ? u"1 fichier déjà dans la bibliothèque, non renvoyé"_s
                      : u"%1 fichiers déjà dans la bibliothèque, non renvoyés"_s.arg(files);
}

QString readHow(int origin)
{
    if (origin == 0)
        return u"Complet"_s;
    return origin == 1 ? u"Komga"_s : u"Nu"_s;
}

QString heldNamed(const QString &name, int held, int whole)
{
    return name + u" existe déjà dans la bibliothèque"_s + Nbsp + u": "_s
           + counted(held, u"tome"_s, u"tomes"_s) + u" sur "_s + QString::number(whole)
           + u". Cet apport sera traité comme un réimport."_s;
}

QString lineCounting(bool hashing, qint64 done, qint64 whole, bool brief)
{
    const QString word = hashing ? importStage(Importing::Asking) : u"Lecture"_s;
    if (whole <= 0)
        return brief ? QString() : word;
    const QString count = u"%1 / %2"_s.arg(done).arg(whole);
    return brief ? count : word + u" · "_s + count;
}

QString lineChecked(bool hashed, qint64 whole, bool brief)
{
    const QString count = u"%1 / %1"_s.arg(whole);
    if (brief)
        return count;
    return (hashed ? u"Vérifié"_s : u"Lu"_s) + u" · "_s + count;
}

QString lineCheckedFileOnly(bool brief)
{
    return brief ? u"Pages non vérifiées"_s : u"Fichier vérifié, pages non vérifiées"_s;
}

QString checkingAll(bool hashing, qint64 done, qint64 whole)
{
    return (hashing ? importStage(Importing::Asking) : u"Lecture"_s) + Nbsp + u": "_s
           + counted(int(done), u"tome"_s, u"tomes"_s) + u" sur "_s + QString::number(whole);
}

QString readingAll(int done, int whole)
{
    return u"Lecture en cours"_s + Nbsp + u": "_s + counted(done, u"série"_s, u"séries"_s)
           + u" sur "_s + QString::number(whole);
}

QString readyAll(int series)
{
    return u"Prêt"_s + Nbsp + u": "_s + counted(series, u"série à importer"_s, u"séries à importer"_s);
}

QString decidingAll(int series)
{
    return importStage(Importing::Deciding) + Nbsp + u": "_s
           + counted(series, u"série"_s, u"séries"_s);
}

QString dropProgress(const DropCounts &counts)
{
    const auto &[foldersRead, foldersToRead, reading, checked, toCheck, hashing, deciding,
                 listed] = counts;
    // Still reading: the volumes are the finer count as soon as one folder has said how many it
    // holds, and the series are what there is until then.
    if (foldersToRead > 0) {
        return toCheck > 0 ? checkingAll(hashing, checked, toCheck)
                           : readingAll(foldersRead, foldersRead + foldersToRead);
    }
    // Every folder read, and the server still to answer for some: not ready, and not asking.
    if (reading > 0)
        return announcing();
    return deciding > 0 ? decidingAll(deciding) : readyAll(listed);
}

QString startWaits()
{
    return u"Suivant s’active à la fin"_s;
}

QString startWaitsHint()
{
    return u"Disponible quand tout est lu et vérifié"_s;
}

QString dropWhat()
{
    return u"Plusieurs univers, séries ou tomes à la fois. Leaf lit ce qu’ils disent déjà, "
           u"que ce soit du JSON Leaf ou un ComicInfo de Komga."_s;
}

QString readSoFar(int cards)
{
    return u"Déjà lu, "_s + counted(cards, u"élément"_s, u"éléments"_s);
}

QString everythingIsChecked()
{
    return u"Tout passe par la vérification"_s + Nbsp
           + u": rien n’est écrit avant d’avoir été vu."_s;
}

QString emptyTheQueue()
{
    return u"Vider"_s;
}

QString dropHow()
{
    return u"Depuis votre gestionnaire de fichiers, n’importe où sur la fenêtre"_s;
}

QString chooseReassurance()
{
    return u"Aucun octet n’est envoyé à cette étape. Leaf lit d’abord, propose ensuite, et ne "
           u"range qu’après votre accord."_s;
}

QString cancel()
{
    return u"Annuler"_s;
}

QString goBack()
{
    return u"Revenir en arrière"_s;
}

QString next()
{
    return u"Suivant"_s;
}

QString previous()
{
    return u"Précédent"_s;
}

QString importIt()
{
    return u"Importer"_s;
}

QString importStage(Importing stage)
{
    using enum Importing;

    switch (stage) {
    case Asking:
        return u"Vérification"_s;
    case Deciding:
        return u"À vous de dire"_s;
    case Ready:
        // « Prêt » and not « En attente »: a folder read, announced and waiting for somebody
        // to press « Importer » is not the same as one queued behind another, and both said
        // « En attente ».
        return u"Prêt"_s;
    case Sending:
        return u"Envoi en cours"_s;
    case Filing:
        return u"Rangement"_s;
    case Filed:
        // « Envoyé » and not « Rangé »: what a reader watched was an upload, and the word
        // that closes it is the one that named it.
        return u"Envoyé"_s;
    case Paused:
        return u"En pause"_s;
    case Failed:
        return u"Échec"_s;
    case Reviewing:
        return u"À vérifier"_s;
    }
    return {};
}

QString howFar(qint64 sent, qint64 whole)
{
    // One vocabulary for sizes: the transfer bar and the import tree say the same thing
    // about the same bytes, in the same units, rather than each carrying its own.
    return u"%1 sur %2"_s.arg(size(sent), size(whole));
}

QString tryingAgainIn(int seconds)
{
    return u"Nouvelle tentative dans %1 s"_s.arg(seconds);
}

QString noSeriesForThisFile()
{
    return u"Aucune série ne correspond. Un fichier seul rejoint une série existante"_s +
           Nbsp + u"; pour en créer une, déposez le dossier."_s;
}

QString pauseIt()
{
    return u"Pause"_s;
}

QString resumeIt()
{
    return u"Reprendre"_s;
}

QString abandonIt()
{
    return u"Abandonner"_s;
}

QString decisionsWaiting(int many)
{
    if (many <= 0)
        return {};
    return many == 1 ? u"une décision"_s : u"%1 décisions"_s.arg(many);
}

QString whatLanded(int installed, int pending, int corrupt, int orphans)
{
    QStringList said;
    said << counted(installed, u"tome envoyé"_s, u"tomes envoyés"_s);
    if (pending > 0)
        said << counted(pending, u"encore à venir"_s, u"encore à venir"_s);
    if (corrupt > 0)
        said << counted(corrupt, u"mal arrivé"_s, u"mal arrivés"_s);
    // Never deleted, only reported — and the word says so rather than leaving a count a
    // reader could take for a loss.
    if (orphans > 0)
        said << counted(orphans, u"déjà là et non annoncé"_s, u"déjà là et non annoncés"_s);
    return said.join(u" · "_s);
}

QString couldNotBeRead(const QString &name)
{
    return u"« %1 » n’a pas pu être lu."_s.arg(name);
}

QString couldNotCleanUp(const QString &name)
{
    return u"Le nettoyage de %1 a échoué"_s.arg(name) + Nbsp
           + u": ses octets restent sur le serveur."_s;
}

QString acceptCreations()
{
    return u"Accepter"_s;
}

QString alreadyElsewhere(const QString &name, const QString &folder)
{
    if (folder.isEmpty())
        return u"« %1 » est déjà dans la bibliothèque — le ranger ici l’y déplacera."_s.arg(name);
    return u"« %1 » est déjà dans « %2 » — le ranger ici l’y déplacera."_s.arg(name, folder);
}

QString verifyEachFile()
{
    return u"Vérifier que chaque tome arrive intact"_s;
}

QString verifyingMeans()
{
    return u"Calcule l’empreinte de chaque page avant l’envoi et la recompare à l’arrivée, "
           u"y compris après les modifications de la phase 2. Plus long à préparer."_s;
}

QString chooseLead()
{
    return u"ou choisir"_s;
}

QString chooseFiles()
{
    return u"Choisir des fichiers…"_s;
}

QString chooseFilesTitle()
{
    return u"Fichiers à importer"_s;
}

QString chooseFolder()
{
    return u"Choisir un dossier…"_s;
}

QString chooseFolderTitle()
{
    return u"Dossier à importer"_s;
}

QString sameDestination(const QString &one, const QString &other)
{
    return u"« %1 » arriverait au même endroit que « %2 ». Déposez-les séparément."_s
        .arg(one, other);
}

QString declaredOutOfPlace(const QString &name, const QString &file, const QString &why)
{
    return u"« %1 » est dit par %2 là où il ne peut pas être. %3 Corrigez ce fichier, puis "
           u"déposez le dossier à nouveau."_s
        .arg(name, file, why);
}

QString level(Manifest::Level level)
{
    using enum Manifest::Level;

    switch (level) {
    case Universe:
        return u"univers"_s;
    case Work:
        return u"série"_s;
    case Edition:
        return u"édition"_s;
    case Chapter:
        return u"chapitre"_s;
    case Volume:
        return u"tome"_s;
    case Book:
        return u"livre"_s;
    }
    return {};
}

QString levelTitle(Manifest::Level level)
{
    using enum Manifest::Level;

    switch (level) {
    case Universe:
        return u"Univers"_s;
    case Work:
        return u"Série"_s;
    case Edition:
        return u"Édition"_s;
    case Chapter:
        return u"Chapitre"_s;
    case Volume:
        return u"Tome"_s;
    case Book:
        return u"Livre"_s;
    }
    return {};
}

QString levelIcon(Manifest::Level level)
{
    using enum Manifest::Level;

    switch (level) {
    case Universe:
        return u"public"_s;
    case Work:
        return u"collections_bookmark"_s;
    case Edition:
        return u"book_2"_s;
    case Chapter:
        return u"bookmark"_s;
    case Volume:
        return u"book"_s;
    case Book:
        return u"menu_book"_s;
    }
    return {};
}

namespace {

/// Whether the level's own word is feminine — « série », « édition » — so a past participle
/// standing after it agrees. The other three levels, including a tome and a chapter, are
/// both masculine, so they never disagree with each other about it.
bool feminine(Manifest::Level level)
{
    return level == Manifest::Level::Work || level == Manifest::Level::Edition;
}

} // namespace

QString willBeCreated(Manifest::Level level)
{
    return feminine(level) ? u"sera créée"_s : u"sera créé"_s;
}

QString willBeMoved(Manifest::Level level)
{
    return feminine(level) ? u"sera déplacée"_s : u"sera déplacé"_s;
}

QString alreadyInTheLibrary()
{
    return u"déjà là"_s;
}

QString willBeSent()
{
    return u"à envoyer"_s;
}

QString beingSent()
{
    return u"envoi"_s;
}

QString wasFiled(Manifest::Level level)
{
    return feminine(level) ? u"envoyée"_s : u"envoyé"_s;
}

QString failedToSend()
{
    return u"échec"_s;
}

QString wasCreated(Manifest::Level level)
{
    return feminine(level) ? u"créée"_s : u"créé"_s;
}

QString wasMoved(Manifest::Level level)
{
    return feminine(level) ? u"déplacée"_s : u"déplacé"_s;
}

QString wasSent(Manifest::Level level)
{
    return feminine(level) ? u"envoyée"_s : u"envoyé"_s;
}

QString willBeReplaced(Manifest::Level level)
{
    return feminine(level) ? u"sera remplacée"_s : u"sera remplacé"_s;
}

QString levelAnd(Manifest::Level level, const QString &state)
{
    // Qualified: the parameter is named after the function it calls, the same way
    // `willBeCreated`'s own argument is — `Words::level` is unreachable by its bare name
    // once shadowed.
    if (state.isEmpty())
        return Words::levelTitle(level);
    return u"%1 · %2"_s.arg(Words::levelTitle(level), state);
}

QString nodeLine(Manifest::Level level, const QString &state, const QString &holds)
{
    QStringList said{levelTitle(level)};
    if (!state.isEmpty())
        said << state;
    if (!holds.isEmpty())
        said << holds;
    return said.join(u" · "_s);
}

QString holdsReplacements(int count, int chosen)
{
    if (chosen <= 0) {
        // « remplaçable » and not « à remplacer »: nothing here has to be replaced, and a
        // line that reads like a task left undone would push somebody to do it.
        return count == 1 ? u"1 tome remplaçable"_s
                          : u"%1 tomes remplaçables"_s.arg(count);
    }
    return chosen == 1 ? u"1 tome sera remplacé"_s
                       : u"%1 tomes seront remplacés"_s.arg(chosen);
}

QString fileNamed(const QString &title, const QString &fileName)
{
    if (title.isEmpty())
        return fileName;
    return u"%1 · %2"_s.arg(title, fileName);
}

QString declarationDiffers(const QString &presentName, const QStringList &differs)
{
    const QString named = presentName.isEmpty()
        ? u"la déclaration déjà là"_s
        : u"la déclaration de « %1 »"_s.arg(presentName);
    if (differs.isEmpty())
        return u"%1 diffère"_s.arg(named);
    return u"%1 diffère · %2"_s.arg(named, differs.join(u", "_s));
}

QString insteadOf(const QString &title, qint64 bytes, bool read)
{
    QStringList said;
    if (!read)
        said << u"non relu"_s;
    else if (!title.isEmpty())
        said << u"« %1 »"_s.arg(title);
    said << size(bytes);
    return u"à la place de %1"_s.arg(said.join(u" · "_s));
}

QString size(qint64 bytes)
{
    // Binary units, correctly named. The only formatter before this one was the lambda
    // inside `howFar`, dividing by 1024² and writing « Mo » — wrong by five percent, and
    // the import tree would have said « Gio » right beside it.
    if (bytes < 1024)
        return u"%1 o"_s.arg(bytes);

    // Rounded for display before the unit is chosen, not after: comparing the raw value to
    // 1024 let 1 048 575 bytes (one below a mebibyte) round up to display as « 1024 Kio »
    // instead of promoting to « 1,0 Mio », and the same gap sat below the next threshold too.
    if (const qint64 kio = std::llround(bytes / 1024.0); kio < 1024)
        return u"%1 Kio"_s.arg(kio);

    // Mio, Gio, Tio, Pio — a whole library dropped in one folder crosses every one of these
    // before a single volume does, which is exactly the drop this screen exists to accept.
    // The table stops at Pio because nothing this client imports gets there; adding a unit
    // above it is one more entry here, not a new formula.
    static const QStringList Units{u"Mio"_s, u"Gio"_s, u"Tio"_s, u"Pio"_s};
    double value = double(bytes) / (1024.0 * 1024.0);
    qsizetype unit = 0;
    while (unit + 1 < Units.size() && std::llround(value * 10.0) >= 10240) {
        value /= 1024.0;
        ++unit;
    }
    return u"%1 %2"_s.arg(QString::number(value, 'f', 1).replace(u'.', u','), Units.at(unit));
}

QString seriesHolds(int editions, qint64 volumes, qint64 bytes)
{
    return QStringList{counted(editions, u"édition"_s, u"éditions"_s),
                       counted(int(volumes), u"tome"_s, u"tomes"_s), size(bytes)}
        .join(QString(Nbsp) + u"· "_s);
}

QString titled(const QString &name, const QString &title)
{
    // A title that only repeats the name — « Tome 1 » on tome 1, « Tome 01 » as well — says
    // nothing the name did not: « Tome 1 · Tome 1 » was two copies of one word.
    if (const auto bare =
            [](const QString &text) {
                QString out;
                for (const QChar one : text.toCaseFolded()) {
                    // « 01 » is « 1 »: a zero is kept only behind another digit (« 10 »).
                    if (one.isLetter()
                        || (one.isDigit()
                            && (one != u'0' || (!out.isEmpty() && out.back().isDigit()))))
                        out += one;
                }
                return out;
            };
        bare(title) == bare(name))
        return name;
    return name + Nbsp + u"· "_s + title;
}

QString nodeHolds(qint64 volumes, qint64 bytes)
{
    if (volumes <= 0)
        return {};
    return counted(int(volumes), u"tome"_s, u"tomes"_s) + Nbsp + u"· "_s + size(bytes);
}

QString fileDetail(const QString &fileName, qint64 bytes)
{
    return fileName + Nbsp + u"· "_s + size(bytes);
}

QString checking(qint64 done, qint64 whole)
{
    if (whole <= 0)
        return importStage(Importing::Asking);
    // Accorded like every other count in this file — « 0/1 tome », not « 0/1 tomes » — the
    // one line here that never asked `whole` to agree with its own noun.
    const QString noun = whole == 1 ? u"tome"_s : u"tomes"_s;
    return u"%1 · %2/%3 %4"_s.arg(importStage(Importing::Asking)).arg(done).arg(whole).arg(noun);
}

QString reading(qint64 done, qint64 whole)
{
    if (whole <= 0)
        return u"Lecture"_s;
    const QString noun = whole == 1 ? u"tome"_s : u"tomes"_s;
    return u"Lecture · %1/%2 %3"_s.arg(done).arg(whole).arg(noun);
}

QString waitingToBeChecked()
{
    return u"En attente"_s;
}

QString nothingToSend()
{
    return u"Rien à envoyer"_s;
}

QString announcing()
{
    // « Vérifié… » and not « Annonce… »: the announcement is the contract's word for what is
    // happening, and what a reader is watching for is the thing that just ended. The
    // ellipsis is what says something is still running — the pendant of the « Vérification ·
    // 12/68 tomes » that came before it.
    return u"Vérifié…"_s;
}

QString couldNotBeOpened()
{
    return u"Ce fichier n’a pas pu être ouvert."_s;
}

QString notAnArchive()
{
    return u"Ce fichier n’est pas une archive."_s;
}

QString archiveTooBig()
{
    return u"Cette archive est trop grande pour être lue ici."_s;
}

QString catalogueMissing()
{
    return u"Cette archive annonce un catalogue qui n’y est pas."_s;
}

QString sidecarTooBig()
{
    return u"Le fichier de métadonnées de cette archive est trop gros pour en être un."_s;
}

QString sidecarCompressedInAnUnknownWay()
{
    return u"Le fichier de métadonnées de cette archive est compressé d’une façon inconnue."_s;
}

QString memberCompressedInAnUnknownWay()
{
    return u"Un fichier de cette archive est compressé d’une façon inconnue."_s;
}

QString memberCouldNotBeRead()
{
    return u"Un fichier de cette archive n’a pas pu être lu en entier."_s;
}

QString sidecarCouldNotBeInflated()
{
    return u"Le fichier de métadonnées de cette archive n’a pas pu être décompressé."_s;
}

QString folderTooDeep()
{
    // The non-breaking space before the « ? » is why this sentence had to come here: it
    // spent its whole life in `Manifest.cpp` with an ordinary one, where the guard that
    // reads `Words.cpp` could never look at it.
    return u"Ce dossier est trop profond pour être lu — un lien qui boucle"_s + Nbsp + u"?"_s;
}

QString notAFolder()
{
    return u"Ce n’est pas un dossier."_s;
}

QString stageAnd(const QString &stage, const QString &howFar)
{
    if (howFar.isEmpty())
        return stage;
    return u"%1 · %2"_s.arg(stage, howFar);
}

QString concern(const QString &said)
{
    return u"· %1"_s.arg(said);
}

// ——— La fiche d'une série ————————————————————————————————————————————————————

QString number(double value)
{
    if (std::abs(value - std::round(value)) < 0.0001)
        return QString::number(static_cast<qint64>(std::llround(value)));
    return QString::number(value, 'g', 4).replace(u'.', u',');
}

QString tab(Tab which)
{
    using enum Tab;
    switch (which) {
    case Volumes:
        return u"Tomes"_s;
    case Description:
        return u"Description"_s;
    case Elsewhere:
        return u"Voir aussi"_s;
    }
    return {};
}

QString creditDefault(bool drawing, std::optional<Api::Medium> medium)
{
    // The plural of `volumes`, without its count: a BD is held in albums, the rest in tomes.
    const QString held = medium == Api::Medium::Bd ? u"albums"_s : u"tomes"_s;
    return u"Pour les %1 ne précisant pas de %2"_s.arg(
        held, drawing ? u"dessinateur"_s : u"scénariste"_s);
}

QList<CreditLine> creditLines(const QStringList &authors, const QStringList &artists)
{
    QSet<QString> drawing;
    for (const QString &name : artists)
        drawing.insert(Fold::searchKey(name));
    QSet<QString> met;
    QStringList both;
    QStringList script;
    QStringList drawn;
    for (const QString &name : authors) {
        const QString key = Fold::searchKey(name);
        if (!met.contains(key)) {
            met.insert(key);
            (drawing.contains(key) ? both : script) << name;
        }
    }
    for (const QString &name : artists) {
        const QString key = Fold::searchKey(name);
        if (!met.contains(key)) {
            met.insert(key);
            drawn << name;
        }
    }
    QList<CreditLine> lines;
    if (!both.isEmpty())
        lines << CreditLine{both.size() >= 2 ? u"Auteurs"_s : u"Auteur"_s, both};
    if (!script.isEmpty())
        lines << CreditLine{u"Scénario"_s, script};
    if (!drawn.isEmpty())
        lines << CreditLine{u"Dessin"_s, drawn};
    return lines;
}

QString makers(const Api::Series &one, const QStringList &people, const QString &link)
{
    const auto written = [&link](const QString &text) {
        return link.isEmpty() ? text : text.toHtmlEscaped();
    };
    QStringList said;
    QStringList named = people;
    if (named.isEmpty()) {
        // Whoever wrote and whoever drew, each once: Death Note read « Tsugumi Ōba », and
        // Takeshi Obata, who drew every page, was nowhere on it.
        QStringList writers = one.credits.authors;
        if (writers.isEmpty() && one.credits.author && !one.credits.author->isEmpty())
            writers << *one.credits.author;
        for (const CreditLine &line : creditLines(writers, one.credits.artists))
            named << line.names;
    }
    if (!named.isEmpty())
        said << namesWithin(named, 2, link);
    if (one.publication.publisher)
        said << written(*one.publication.publisher);
    if (one.medium)
        said << written(medium(*one.medium));
    if (one.run)
        said << written(editionStatus(Api::spell(*one.run)));
    return said.join(u" · "_s);
}

QString andOthers(int count)
{
    return count >= 2 ? u"et %1 autres"_s.arg(count) : u"et 1 autre"_s;
}

QString namesWithin(const QStringList &names, int most, const QString &link)
{
    const bool all = names.size() <= most + 1;
    QStringList shown;
    for (const QString &name : names.first(all ? names.size() : most))
        shown << (link.isEmpty() ? name : name.toHtmlEscaped());
    if (all)
        return shown.join(u", "_s);
    const QString others = andOthers(int(names.size()) - most);
    return shown.join(u", "_s) + u' '
           + (link.isEmpty() ? others : u"<a href=\"%1\">%2</a>"_s.arg(link, others));
}

QString volumeCredits(const Api::Entry &one)
{
    if (one.authorsInherited && one.artistsInherited)
        return {};
    QStringList lines;
    for (const CreditLine &line : creditLines(one.credits.authors, one.credits.artists))
        lines << line.label + Nbsp + u": "_s + line.names.join(u", "_s);
    return lines.join(Nbsp + u"· "_s);
}

QString peopleTitle(const QString &work)
{
    return u"Les auteurs "_s + ofName(work);
}

QString peopleCount(int count, std::optional<Api::Medium> medium)
{
    return counted(count, u"personne"_s, u"personnes"_s) + Nbsp + u"· rangées par nombre "_s
           + (medium == Api::Medium::Bd ? u"d’albums"_s : u"de tomes"_s);
}

QString weights(const Api::Series &one)
{
    QStringList said;
    // What this library holds, counted in the medium's own word — a BD comes in albums.
    // A webtoon holds chapters and no volume: « 0 tome » over a hundred and twenty chapters
    // said the library was empty, so what is there is counted by its own word.
    if (one.holding.ownedVolumes == 0 && one.counts.entries > 0)
        said << counted(one.counts.entries, u"chapitre"_s, u"chapitres"_s);
    else
        said << volumes(one.holding.ownedVolumes, one.medium);
    if (const QString spread = arcs(one.counts.arcs); !spread.isEmpty())
        said << spread;
    if (one.ageRating && !one.ageRating->isEmpty())
        said << *one.ageRating;
    if (one.readingDirection)
        said << readingDirection(*one.readingDirection);
    return said.join(u" · "_s);
}

QString editions(int count)
{
    // Never below two: there is nothing to choose between when a work has one edition.
    return count < 2 ? QString() : counted(count, u"édition"_s, u"éditions"_s);
}

QString arcs(int count)
{
    return count <= 0 ? QString() : counted(count, u"arc"_s, u"arcs"_s);
}

QString neverRead()
{
    return u"Non lu"_s;
}

QString timesFinished(int times)
{
    // From two. A finished series would otherwise carry « ×1 » on every line for no news.
    return times < 2 ? QString() : u"×%1"_s.arg(times);
}

QString missingLabel(int count)
{
    return count == 1 ? u"Manquant"_s : u"Manquants"_s;
}

QString missingVolumes(const QList<double> &numbers)
{
    if (numbers.isEmpty())
        return {};
    QStringList said;
    said.reserve(numbers.size());
    for (const double one : numbers)
        said << number(one);
    // Commas to the end and no « et »: a list of identifiers is not a sentence.
    return (numbers.size() == 1 ? u"Tome "_s : u"Tomes "_s) + said.join(u", "_s);
}

QString heldOutOf(int owned, int ceiling)
{
    return ceiling > 0 ? u"%1 sur %2"_s.arg(owned).arg(ceiling) : QString::number(owned);
}

QString inThisLibrary()
{
    return u"Dans cette bibliothèque"_s;
}

QString fact(Fact which)
{
    using enum Fact;
    switch (which) {
    case Writers:
        return u"Scénario"_s;
    case Artists:
        return u"Dessin"_s;
    case Publisher:
        return u"Éditeur"_s;
    case Collection:
        return u"Collection"_s;
    case Language:
        return u"Langue"_s;
    case Kind:
        return u"Type"_s;
    case Direction:
        return u"Lecture"_s;
    case Status:
        return u"Statut"_s;
    case Age:
        return u"Âge"_s;
    case Colour:
        return u"Couleur"_s;
    case Held:
        return u"Tomes détenus"_s;
    case Read:
        return u"Lus"_s;
    case FirstReceived:
        return u"Premier reçu"_s;
    case LastReceived:
        return u"Dernier reçu"_s;
    }
    return {};
}

QString readingDirection(Api::ReadingDirection value)
{
    using enum Api::ReadingDirection;
    switch (value) {
    case LeftToRight:
        return u"Gauche à droite"_s;
    case RightToLeft:
        return u"Droite à gauche"_s;
    case Vertical:
        return u"Verticale"_s;
    }
    return {};
}

QString colour(bool coloured)
{
    // Positive on both sides: « pas en couleur » is a double negative nobody reads twice.
    return coloured ? u"Couleur"_s : u"Noir et blanc"_s;
}

QString readEntries(int finished, bool oneOpen, int which)
{
    if (!oneOpen)
        return QString::number(finished);
    // The ordinal is worth the trouble: « le 5 en cours » reads as a quantity.
    return u"%1 · le %2%3 en cours"_s.arg(finished).arg(which).arg(which == 1 ? u"ᵉʳ"_s
                                                                             : u"ᵉ"_s);
}

namespace {

/// « tomes » or « chapitres », and the singular that French keeps at one.
QString unitWord(bool volumes, bool one)
{
    if (volumes)
        return one ? u"tome"_s : u"tomes"_s;
    return one ? u"chapitre"_s : u"chapitres"_s;
}

} // namespace

QString arcRange(Api::Arc::Unit unit, double from, double to)
{
    const bool volumes = unit == Api::Arc::Unit::Volume;
    // A range of one is a range all the same — an arc that covers a single volume says so
    // rather than repeating the number twice.
    if (std::abs(to - from) < 0.0001)
        return u"%1 %2"_s.arg(unitWord(volumes, true), number(from));
    return u"%1 %2 à %3"_s.arg(unitWord(volumes, false), number(from), number(to));
}

QString chapterRange(double from, double to)
{
    return arcRange(Api::Arc::Unit::Chapter, from, to);
}

QString fromChapter(double from)
{
    return u"à partir du chapitre %1"_s.arg(number(from));
}

QString stepRange(const Api::ReadingStep &step)
{
    if (!step.from.has_value() || !step.to.has_value())
        return {};
    // A volume step counts in the edition's own word, a chapter step in chapters. The unit is
    // absent for a whole work, and a whole work has no range to write.
    const bool volumes = step.unit == Api::ReadingStep::Unit::Volume;
    return arcRange(volumes ? Api::Arc::Unit::Volume : Api::Arc::Unit::Chapter, *step.from,
                    *step.to);
}

QString viewAsList()
{
    return u"En liste"_s;
}

QString viewAsGrid()
{
    return u"En grille"_s;
}

QString volumesAxis()
{
    return u"les tomes"_s;
}

QString noVolumeByThatName()
{
    return u"Aucun tome ne porte ce nom."_s;
}

QString sameWorkOtherwise()
{
    return u"La même œuvre, autrement"_s;
}

QString inTheUniverse()
{
    return u"Dans l’univers"_s;
}

QString universeLine(const QString &name, int others)
{
    if (others <= 0 || name.isEmpty())
        return name;
    return name + u" · "_s + counted(others, u"autre"_s, u"autres"_s);
}

QString outsideTheOrder()
{
    return u"Hors parcours"_s;
}

QString seriesCount(int count)
{
    return count <= 0 ? QString() : counted(count, u"série"_s, u"séries"_s);
}

QString command(Command which)
{
    using enum Command;
    switch (which) {
    case MarkSeriesRead:
        return u"Marquer toute la série comme lue"_s;
    case MarkSeriesUnread:
        return u"Marquer toute la série comme non lue"_s;
    case ReimportSeries:
        return u"Réimporter…"_s;
    case AddEditions:
        return u"Ajouter des éditions…"_s;
    case EraseSeries:
        return u"Supprimer la série…"_s;
    case MarkEntryRead:
        return u"Marquer comme lu"_s;
    case MarkEntryUnread:
        return u"Marquer comme non lu"_s;
    case ReimportEntry:
        return u"Réimporter ce tome…"_s;
    case SaveACopy:
        return u"Enregistrer une copie…"_s;
    case EraseEntry:
        return u"Supprimer le tome…"_s;
    case ShowInfo:
        return u"Informations"_s;
    case ChooseDirection:
        return u"Sens de lecture…"_s;
    case EditSeries:
        return u"Modifier la série…"_s;
    case EditEntry:
        return u"Modifier le tome…"_s;
    case Rescan:
        return u"Actualiser"_s;
    case ReplaceChapter:
        return u"Remplacer un chapitre…"_s;
    case EraseEdition:
        return u"Supprimer l’édition…"_s;
    case EditEdition:
        return u"Modifier l’édition…"_s;
    case Export:
        return u"Exporter…"_s;
    case ExportWork:
        return u"Exporter l’œuvre…"_s;
    case ExportUniverse:
        return u"Exporter l’univers…"_s;
    }
    return {};
}

QString selectionCount(int count, bool volumes)
{
    return volumes ? plural(count, u"%1 tome sélectionné"_s, u"%1 tomes sélectionnés"_s)
                   : plural(count, u"%1 série sélectionnée"_s, u"%1 séries sélectionnées"_s);
}

QString markSelection(bool read)
{
    return read ? u"Marquer la sélection comme lue"_s : u"Marquer la sélection comme non lue"_s;
}

QString clearSelection()
{
    return u"Désélectionner"_s;
}

QString eraseEntryQuestion(double number_)
{
    return u"Supprimer le tome %1 ?"_s.arg(number(number_));
}

QString eraseSeriesQuestion(const QString &name)
{
    return u"Supprimer « %1 » ?"_s.arg(name);
}

QString whatGoes(const QString &title, int pages, qint64 bytes)
{
    const QString weighs = pages > 0
                               ? u"%1 pages, %2"_s.arg(pages).arg(size(bytes))
                               : size(bytes);
    // A volume with no title of its own says what it weighs and nothing else: a dash leading
    // nowhere reads as a title that failed to load.
    return title.isEmpty() ? weighs : u"%1 — %2"_s.arg(title, weighs);
}

QString whichFile(const QString &name)
{
    return u"Fichier : %1"_s.arg(name);
}

QString whatWouldRemain(int remaining, std::optional<Api::Medium> medium, double number_,
                        bool between, std::optional<double> nextHeld)
{
    QString said = u"Il restera %1"_s.arg(volumes(remaining, medium));
    // A hole only where there is something either side of it. Erasing the last brings the
    // ceiling down and erasing the first raises the floor, and neither is reported as
    // missing — `store/text.rs::gaps` answers that, and the sentence follows it.
    if (between)
        said += u", et le %1 rejoindra les manquants"_s.arg(number(number_));
    said += u"."_s;
    if (nextHeld.has_value()) {
        said += u" La numérotation ne bouge pas : le %1 reste le %1."_s
                    .arg(number(*nextHeld));
    }
    return said;
}

QString whatAWholeEditionTakes(int files, qint64 bytes, int finished, bool oneOpen, int which)
{
    QString said = u"%1 %2, %3."_s.arg(files)
                       .arg(files == 1 ? u"fichier"_s : u"fichiers"_s, size(bytes));
    if (finished <= 0 && !oneOpen)
        return said;

    // What a deletion carries away besides the files. Said in the same sentence, because a
    // reader who has to look twice to find it is a reader who finds it afterwards.
    QStringList lost;
    if (finished > 0)
        lost << (finished == 1 ? u"Le tome lu"_s : u"Les %1 tomes lus"_s.arg(finished));
    if (oneOpen) {
        lost << u"la position dans le %1%2"_s.arg(which).arg(which == 1 ? u"ᵉʳ"_s : u"ᵉ"_s);
    }
    QString what = lost.join(u" et "_s);
    const bool onlyPosition = lost.size() == 1 && oneOpen;
    if (onlyPosition)
        what = u"La position dans le %1%2"_s.arg(which).arg(which == 1 ? u"ᵉʳ"_s : u"ᵉ"_s);
    // « La position … sera oubliée », « Le tome lu sera oublié », « Les 3 tomes lus seront
    // oubliés »: the verb and the participle follow what is lost, not whether one thing was.
    const bool several = lost.size() > 1 || finished >= 2;
    said += u" %1 %2 %3 avec eux."_s.arg(what, several ? u"seront"_s : u"sera"_s,
                                         several      ? u"oubliés"_s
                                         : onlyPosition ? u"oubliée"_s
                                                        : u"oublié"_s);
    return said;
}

QString noTrash(bool several)
{
    // Each half carries its own `_s`: two raw tokens joined into one literal are two tokens
    // `words_stay_french` cannot read, and it says so rather than passing them silently.
    const QString files = several ? u"Les fichiers sont supprimés du disque."_s
                                  : u"Le fichier est supprimé du disque."_s;
    const QString back = several ? u"un scan ne les ramènera pas."_s
                                 : u"un scan ne le ramènera pas."_s;
    return files + u" Leaf n’a pas de corbeille — "_s + back;
}

QString otherEditionUntouched(const QString &name)
{
    return u"L’édition %1 de la même œuvre n’est pas touchée."_s.arg(name);
}

QString typeToConfirm(const QString &word)
{
    return u"Tapez « %1 » pour confirmer"_s.arg(word);
}

QString eraseButton()
{
    return u"Supprimer"_s;
}

QString couldNotSend(const QString &subject)
{
    return subject.isEmpty() ? u"L’envoi n’a pas abouti"_s
                             : u"%1 n’a pas pu être envoyé"_s.arg(subject);
}

QString scanEnded()
{
    return u"Scan terminé"_s;
}

QString scanStopped()
{
    return u"Le scan s’est arrêté"_s;
}

QString copyKept()
{
    return u"Copie enregistrée"_s;
}

QString alreadyLinked()
{
    return u"Cet appareil est déjà relié à une bibliothèque"_s + Nbsp
           + u": déliez-le d’abord pour en changer."_s;
}

QString commandRefused()
{
    return u"L’action n’a pas abouti"_s;
}

QString isbnCopied()
{
    return u"ISBN copié"_s;
}

QString webpMissing()
{
    return u"Il manque le module d’images WebP de Qt"_s;
}

QString webpMissingRemedy()
{
    return u"Installez qt6-image-formats-plugins pour voir les couvertures et les pages."_s;
}

QString bubbleOffer(Toasts::Offer what)
{
    using enum Toasts::Offer;
    switch (what) {
    case See:
        return u"Voir"_s;
    case Retry:
        return u"Réessayer"_s;
    case OpenFolder:
        return u"Ouvrir le dossier"_s;
    case Nothing:
        break;
    }
    return {};
}

QString moreBubbles(int how)
{
    return how <= 0 ? QString() : counted(how, u"autre"_s, u"autres"_s);
}

QString warns(Preferences::Warns which)
{
    using enum Preferences::Warns;
    switch (which) {
    case Imports:
        return u"Imports terminés"_s;
    case Scans:
        return u"Scans terminés"_s;
    case Downloads:
        return u"Téléchargements"_s;
    case Failures:
        break;
    }
    return u"Échecs"_s;
}

QString warnsAbout(Preferences::Warns which)
{
    using enum Preferences::Warns;
    switch (which) {
    case Imports:
        return u"un dossier installé, un fichier rangé"_s;
    case Scans:
        return u"et ce que le balayage a trouvé"_s;
    case Downloads:
        return u"une copie enregistrée sur le disque"_s;
    case Failures:
        break;
    }
    // Said out loud, because it is the one line that crosses the other three: « Scans
    // terminés » off does not silence a scan that stopped, and a reader who is not told finds
    // out by being surprised.
    return u"quel qu’en soit le sujet, même si sa ligne est éteinte"_s;
}

QString corner(Preferences::Corner where)
{
    using enum Preferences::Corner;
    switch (where) {
    case TopLeft:
        return u"En haut à gauche"_s;
    case Top:
        return u"En haut"_s;
    case TopRight:
        return u"En haut à droite"_s;
    case BottomLeft:
        return u"En bas à gauche"_s;
    case Bottom:
        return u"En bas"_s;
    case BottomRight:
        break;
    }
    return u"En bas à droite"_s;
}

QString notificationSettings()
{
    return u"Notifications"_s;
}

QString whatWarnsYou()
{
    return u"Ce qui vous prévient"_s;
}

QString whereBubblesAppear()
{
    return u"Où les bulles apparaissent"_s;
}

QString eventColumn()
{
    return u"Événement"_s;
}

QString bubbleColumn()
{
    return u"Bulle"_s;
}

QString desktopColumn()
{
    return u"Bureau"_s;
}

QString exportFolderTitle()
{
    return u"Choisir où exporter"_s;
}

QString exportConflictTitle()
{
    return u"Des éléments existent déjà"_s;
}

QString exportConflict(int count, const QString &example)
{
    return plural(count, u"%1 élément existe déjà dans ce dossier, par exemple « %2 »."_s,
                  u"%1 éléments existent déjà dans ce dossier, par exemple « %2 »."_s)
        .arg(example);
}

QString exportReplace()
{
    return u"Remplacer"_s;
}

QString exportComplete()
{
    return u"Compléter"_s;
}

QString exportingNow(const QString &name)
{
    return u"Export de « %1 »"_s.arg(name);
}

QString exportOfSeveral(int count)
{
    return counted(count, u"série"_s, u"séries"_s);
}

QString exportDone()
{
    return u"Export terminé"_s;
}

QString exportDoneDetail(const QString &name, const QStringList &missing)
{
    if (missing.isEmpty())
        return name;
    return name + u" · "_s
           + plural(int(missing.size()), u"%1 fichier absent du disque"_s,
                    u"%1 fichiers absents du disque"_s)
           + Nbsp + u": "_s + missing.join(u", "_s);
}

QString exportStopped()
{
    return u"L’export s’est arrêté"_s;
}

QString nothingToExport()
{
    return u"Il n’y a rien à exporter ici."_s;
}

QString notEnoughRoom(qint64 needed, qint64 free)
{
    return u"Il manque %1 sur ce disque"_s.arg(size(needed - free)) + Nbsp
           + u": %2 à écrire, %3 libres."_s.arg(size(needed), size(free));
}

QString exportUnsafePath(const QString &path)
{
    return u"Le serveur a donné un chemin qui sort du dossier choisi"_s + Nbsp + u": %1"_s.arg(path);
}

QString couldNotMakeFolder(const QString &path)
{
    return u"Le dossier %1 n’a pas pu être créé."_s.arg(path);
}

QString couldNotWrite(const QString &name)
{
    return u"%1 n’a pas pu être écrit."_s.arg(QFileInfo(name).fileName());
}

QString nothingToErase()
{
    return u"Il n’y a rien à supprimer ici."_s;
}

QString wouldNotGo(const QList<QString> &refused)
{
    if (refused.isEmpty())
        return {};
    const QString said = QStringList(refused).join(u", "_s);
    return refused.size() == 1
               ? u"Un fichier n’a pas pu être supprimé : %1"_s.arg(said)
               : u"%1 fichiers n’ont pas pu être supprimés : %2"_s
                     .arg(refused.size()).arg(said);
}

QString hereToo(const QString &detail)
{
    return detail.isEmpty() ? u"ici"_s : detail + u" · ici"_s;
}

QString whoReads()
{
    return u"Qui lit"_s + Nbsp + u"?"_s;
}

QString wrongCode()
{
    return u"Ce code n’est pas le bon."_s;
}

QString profileNote(Api::Kind kind, std::optional<int> maxAge)
{
    switch (kind) {
    case Api::Kind::Principal:
        return u"Principal"_s;
    case Api::Kind::Reader:
        return {};
    case Api::Kind::Restricted:
        if (maxAge)
            return u"Jusqu’à %1"_s.arg(yearsOld(*maxAge));
        break;
    case Api::Kind::Other:
        break;
    }
    return u"Restreint"_s;
}

QString addOne()
{
    return u"Ajouter"_s;
}

QString keepOnThisDevice()
{
    return u"Retenir sur cet appareil"_s;
}

QString manageProfiles()
{
    return u"Gérer les profils"_s;
}

QString switchNeedsCode()
{
    return u"Changer de profil demande le code."_s;
}

QString backToShelf()
{
    return u"Revenir à l’étagère"_s;
}

QString closeWhoReads()
{
    return u"Fermer"_s;
}

QString switchToProfile(const QString &name)
{
    return u"Passer à %1"_s.arg(name);
}

QString leaveNeedsCode()
{
    return u"Quitter votre profil demande le code."_s;
}

QString enterCode()
{
    return u"Saisissez le code."_s;
}

QString addAProfile()
{
    return u"Ajouter un profil"_s;
}

QString addNeedsCode()
{
    return u"L’ajout d’un profil est protégé par le code."_s;
}

QString forgotCode()
{
    return u"Code oublié"_s + Nbsp + u"?"_s;
}

QString resetCodeCommand()
{
    return u"sudo leaf-server profile reset-code"_s;
}

QString back()
{
    return u"Retour"_s;
}

QString confirm()
{
    return u"Valider"_s;
}

QString newProfile()
{
    return u"Nouveau profil"_s;
}

QString yourProfile()
{
    return u"Votre profil"_s;
}

QString editProfile(const QString &name)
{
    return u"Modifier %1"_s.arg(name);
}

QString profileName()
{
    return u"Nom"_s;
}

QString profileColour()
{
    return u"Couleur"_s;
}

QString takenColourNote()
{
    return u"Une couleur déjà prise porte l’initiale de son profil."_s;
}

QString whatItMayDo()
{
    return u"Ce que ce profil peut faire"_s;
}

QString readerRole()
{
    return u"Lecteur"_s;
}

QString restrictedRole()
{
    return u"Restreint"_s;
}

QString mayImport()
{
    return u"Importer"_s;
}

QString mayEdit()
{
    return u"Modifier"_s;
}

QString mayErase()
{
    return u"Supprimer"_s;
}

QString readerExplained()
{
    return u"Ce profil lit, et fait en plus ce que vous cochez."_s;
}

QString restrictedExplained()
{
    return u"Ce profil ne fait que lire, et seulement les séries dont l’âge indiqué ne dépasse pas la limite choisie. Une série sans âge indiqué lui reste cachée."_s;
}

QString seriesUpTo()
{
    return u"Âge maximal"_s;
}

QString ageLimitNote()
{
    return u"De 3 à 17 ans. À 18 ans le profil verrait aussi les séries réservées aux adultes, et le rôle Lecteur le fait déjà."_s;
}

QString yearsOld(int age)
{
    return plural(age, u"%1 an"_s, u"%1 ans"_s);
}

QString codeToCreate()
{
    return u"Le code, à créer"_s;
}

QString recoveryTitle()
{
    return u"Votre code de secours"_s;
}

QString recoveryExplained()
{
    return u"Notez-le et gardez-le à part, pas sur cet appareil"_s + Nbsp
           + u": il ne sera plus montré. Si vous oubliez le code, il permet d’en choisir un "
             u"nouveau, depuis n’importe quel appareil."_s;
}

QString recoveryNoted()
{
    return u"J’ai noté mon code de secours"_s;
}

QString recoveryCardExplained()
{
    return u"Si vous oubliez le code, ce code de secours permet d’en choisir un nouveau, depuis "
           u"n’importe quel appareil, sans passer par le serveur. Il n’est montré qu’une fois."_s;
}

QString recoveryRedraw()
{
    return u"Créer un nouveau code de secours"_s;
}

QString recoveryRedrawNote()
{
    return u"L’ancien cesse de fonctionner tout de suite."_s;
}

QString recoverExplained()
{
    return u"Entrez votre code de secours, puis choisissez un nouveau code."_s;
}

QString recoveryField()
{
    return u"Code de secours"_s;
}

QString newCodeField()
{
    return u"Nouveau code"_s;
}

QString recoveryNotDrawn(const QString &why)
{
    return u"Le code est enregistré, mais son code de secours n’a pas pu être créé"_s + Nbsp
           + u": "_s + why
           + u" Créez-le depuis l’onglet Profils, avec « Créer un nouveau code de secours »."_s;
}

QString currentCode()
{
    return u"Code actuel"_s;
}

QString currentCodeAsked()
{
    return u"Changer le code, ou le retirer, demande celui qui est en place."_s;
}

QString replaceCode()
{
    return u"Remplacer le code"_s;
}

QString wrongRecovery()
{
    return u"Ce code de secours n’est pas le bon."_s;
}

QString noRecoveryCommand()
{
    return u"Pas de code de secours"_s + Nbsp
           + u"? Il ne reste que le serveur, avec cette commande"_s + Nbsp + u":"_s;
}

QString codeFirst()
{
    return u"Le code"_s;
}

QString codeAgain()
{
    return u"Une seconde fois"_s;
}

QString aCode()
{
    return u"Un code"_s;
}

QString whenCodeAsked()
{
    return u"Il est demandé"_s;
}

QString toLeaveRestricted()
{
    return u"Pour quitter un profil restreint"_s;
}

QString alsoToComeBack()
{
    return u"Aussi pour revenir à votre profil"_s;
}

QString principalExplained()
{
    return u"C’est le profil principal"_s + Nbsp
           + u": vous gérez les autres profils, les appareils et la bibliothèque."_s;
}

QString codeHintFree()
{
    return u"Le seul code de Leaf, de 4 à 8 caractères. Facultatif tant qu’aucun profil n’est restreint"_s + Nbsp
           + u"; il devient obligatoire avec le premier."_s;
}

QString codeHintHeld(const QString &name)
{
    return u"Le seul code de Leaf, de 4 à 8 caractères. Il ne peut pas être retiré tant qu’un profil est restreint, ici %1."_s.arg(name);
}

QString principalKind()
{
    return u"Profil principal"_s;
}

QString readerKind()
{
    return u"Profil lecteur"_s;
}

QString restrictedKind()
{
    return u"Profil restreint"_s;
}

QString youMark()
{
    return u"vous"_s;
}

QString retainedHere()
{
    return u"retenu sur cet ordinateur"_s;
}

QString retainedOn(const QString &device)
{
    return u"retenu sur %1"_s.arg(device);
}

QString seriesUpToYears(int age)
{
    return u"séries jusqu’à "_s + plural(age, u"%1 an"_s, u"%1 ans"_s);
}

QString eraseProfile(const QString &name)
{
    return u"Supprimer %1"_s.arg(name) + Nbsp + u"?"_s;
}

QString eraseConsequence()
{
    return u"Les lectures en cours et les préférences de ce profil seront effacées."_s;
}

QString eraseLeavesDevices()
{
    return u"Les appareils qui le retenaient demanderont qui lit. Les tomes de la bibliothèque ne bougent pas."_s;
}

QString eraseProfileButton(const QString &name)
{
    return u"Supprimer %1"_s.arg(name);
}

QString addProfileButton(const QString &name)
{
    return name.isEmpty() ? addOne() : u"Ajouter %1"_s.arg(name);
}

QString saveProfile()
{
    return u"Enregistrer"_s;
}

QString firstCodeHint()
{
    return u"Il n’y a pas encore de code, et un profil restreint en demande un"_s + Nbsp
           + u": c’est lui qui permet d’en sortir. De 4 à 8 caractères, à saisir deux fois. Dans votre profil, vous choisirez s’il protège aussi le retour vers le vôtre."_s;
}

QString principalManages(const QString &principal)
{
    return u"Ce profil pourra changer son nom et sa couleur, comme vous. Les droits et les profils restreints, c’est le profil principal, %1, qui les gère."_s.arg(principal);
}

QString fewerYears()
{
    return u"Un an de moins"_s;
}

QString moreYears()
{
    return u"Un an de plus"_s;
}

QString colourName(const QString &id, const QString &owner)
{
    static const QHash<QString, QString> names{
        {u"emeraude"_s, u"Émeraude"_s}, {u"sapin"_s, u"Sapin"_s},
        {u"sauge"_s, u"Sauge"_s},       {u"olive"_s, u"Olive"_s},
        {u"ocre"_s, u"Ocre"_s},         {u"ambre"_s, u"Ambre"_s},
        {u"corail"_s, u"Corail"_s},     {u"framboise"_s, u"Framboise"_s},
        {u"rose"_s, u"Rose"_s},         {u"prune"_s, u"Prune"_s},
        {u"violet"_s, u"Violet"_s},     {u"indigo"_s, u"Indigo"_s},
        {u"bleu"_s, u"Bleu"_s},         {u"azur"_s, u"Azur"_s},
        {u"ardoise"_s, u"Ardoise"_s},   {u"terre"_s, u"Terre"_s}};
    const QString name = names.value(id, id);
    return owner.isEmpty() ? name : u"%1, déjà prise par %2"_s.arg(name, owner);
}

QString profilesTab()
{
    return u"Profils"_s;
}

QString whatEachMayDo()
{
    return u"Ce que chacun peut faire"_s;
}

QString devicesTab()
{
    return u"Appareils"_s;
}

QString aboutTab()
{
    return u"À propos"_s;
}

QString atOpening()
{
    return u"À l’ouverture"_s;
}

QString opensOnThisComputer()
{
    return u"Sur cet ordinateur, Leaf s’ouvre sur"_s;
}

QString askWhoReads()
{
    return u"Demander qui lit"_s;
}

QString otherDevices()
{
    return u"Les autres appareils"_s;
}

QString deviceSeen(qint64 milliseconds)
{
    const QString when = moment(milliseconds);
    return when.isEmpty() ? QString() : u"Vu le %1"_s.arg(when);
}

QString opensOn(const QString &name)
{
    return u"S’ouvre sur %1"_s.arg(name);
}

QString removeDevice()
{
    return u"Retirer cet appareil"_s;
}

QString seriesForYou(int count)
{
    return plural(count, u"%1 série pour vous"_s, u"%1 séries pour vous"_s);
}

QString shortcuts()
{
    return u"Raccourcis clavier"_s;
}

QString thisClient()
{
    return u"Ce client"_s;
}

QString serverHeading()
{
    return u"Le serveur"_s;
}

QString profileLine(Api::Kind kind, std::optional<int> maxAge, const QStringList &marks)
{
    QStringList parts;
    switch (kind) {
    case Api::Kind::Principal:
        parts << principalKind();
        break;
    case Api::Kind::Reader:
        parts << readerKind();
        break;
    case Api::Kind::Restricted:
    case Api::Kind::Other:
        parts << restrictedKind();
        if (kind == Api::Kind::Restricted && maxAge)
            parts << seriesUpToYears(*maxAge);
        break;
    }
    for (const QString &mark : marks) {
        if (!mark.isEmpty())
            parts << mark;
    }
    return parts.join(u" · "_s);
}

QString managedByPrincipal(const QString &principal)
{
    return u"Gérer les autres profils et leurs droits"_s + Nbsp
           + u": c’est le rôle du profil principal, %1."_s.arg(principal);
}

QString onlyReads(const QString &name)
{
    return u"Le profil de %1, restreint, ne fait que lire, quoi qu’il arrive."_s.arg(name);
}

QString rightsExplained()
{
    return u"Modifier, c’est changer les informations d’une série ou d’un tome. Vous, profil principal, pouvez tout faire"_s
           + Nbsp + u"; un nouveau profil ne fait que lire tant que rien n’est coché."_s;
}

QString devicesExplained()
{
    return u"Retirer un appareil coupe sa clé tout de suite"_s + Nbsp
           + u": une tablette perdue ne lit plus rien. Chacun peut faire retenir son propre profil depuis « Qui lit"_s
           + Nbsp + u"? »"_s + Nbsp
           + u"; qu’un appareil s’ouvre sur un profil restreint, vous seul le décidez, ici."_s;
}

QString eraseExplained()
{
    return u"Supprimer un profil efface ses lectures en cours et ses préférences"_s + Nbsp
           + u"; les appareils qui le retenaient demanderont qui lit."_s;
}

QString asksWhoReadsThere()
{
    return u"Demande qui lit à l’ouverture"_s;
}

QString chooseItsProfile()
{
    return u"Choisir son profil"_s;
}

QString removeDeviceQuestion(const QString &name)
{
    return u"Retirer %1"_s.arg(name) + Nbsp + u"?"_s;
}

QString removeDeviceConsequence()
{
    return u"Sa clé cesse de fonctionner tout de suite. Pour qu’il lise de nouveau, il faudra le relier."_s;
}

QString appShortcut(AppShortcut which)
{
    switch (which) {
    case AppShortcut::Search:
        return u"Chercher"_s;
    case AppShortcut::Back:
        return u"Revenir en arrière"_s;
    case AppShortcut::Move:
        return u"Aller d’un élément à l’autre"_s;
    case AppShortcut::Activate:
        return u"Ouvrir, activer"_s;
    case AppShortcut::SeriesMenu:
        return u"Le menu d’une série"_s;
    case AppShortcut::ReaderKeys:
        return u"Les raccourcis du lecteur, quand un tome est ouvert"_s;
    }
    return {};
}

QString appShortcutsScope()
{
    return u"Ces touches valent partout, sauf dans le lecteur, qui a les siennes."_s;
}

QString aboutFact(AboutFact which)
{
    switch (which) {
    case AboutFact::Version:
        return u"Version de Leaf"_s;
    case AboutFact::Device:
        return u"Cet appareil"_s;
    case AboutFact::Qt:
        return u"Qt, la bibliothèque graphique"_s;
    case AboutFact::System:
        return u"Système"_s;
    case AboutFact::State:
        return u"État"_s;
    case AboutFact::Access:
        return u"Accès"_s;
    case AboutFact::Api:
        return u"Version de l’API"_s;
    case AboutFact::Format:
        return u"Version du format"_s;
    case AboutFact::ServerVersion:
        return u"Version du serveur"_s;
    }
    return {};
}

QString libraryAccess(bool closed)
{
    // « Accès » is masculine: fermé, ouvert — the lines beside it are not about the library.
    return closed ? u"Fermé"_s + Nbsp + u": il faut une clé"_s
                  : u"Ouvert, sur cet ordinateur seulement"_s;
}

QString apiExplained()
{
    return u"L’API est la langue que parlent le client et le serveur"_s + Nbsp
           + u": s’ils n’ont pas la même, ils ne se comprennent pas. Le format est celui des "
             u"fichiers de la bibliothèque, lus par le serveur."_s;
}

// ——— Les doutes d'une fiche d'import ————————————————————————————————————————————

QString sameNumberTwice(double number_)
{
    return u"Un autre fichier porte aussi le numéro "_s + number(number_);
}

QString orderRefused(const QString &order, int step, Orders::Refusal why, const QString &target)
{
    using enum Orders::Refusal;
    QString because;
    switch (why) {
    case NamesNothing:
        because = u"« %1 » n’est pas un ordre de cet univers"_s.arg(target);
        break;
    case CarriesMore:
        because = u"une inclusion ne porte rien d’autre"_s + Nbsp +
                  u": ni œuvre, ni unité, ni édition, ni plage"_s;
        break;
    case ClosesLoop:
        because = u"inclure « %1 » ramène à cet ordre"_s.arg(target);
        break;
    }
    return u"Ordre « %1 », étape %2"_s.arg(order).arg(step) + Nbsp + u": "_s + because;
}

QString ordersPastTheCeiling()
{
    return u"Cet univers a plus d’ordres, ou d’étapes, que le serveur n’en lit"_s + Nbsp +
           u": le reste est laissé de côté"_s;
}

QString orderLine(int works, int flat, int copied, bool includes)
{
    QString line = counted(works, u"série"_s, u"séries"_s) + u" · "_s +
                   counted(flat, u"étape"_s, u"étapes"_s);
    if (copied > 0) {
        QString how;
        if (includes)
            how = copied >= 2 ? u"incluses"_s : u"incluse"_s;
        else
            how = copied >= 2 ? u"recopiées"_s : u"recopiée"_s;
        line += u", dont "_s + QString::number(copied) + u' ' + how;
    }
    return line;
}

QString archiveUnreadable(const QString &why)
{
    // The archive's own sentence first, since it names the cause; then what it costs, which
    // is the « plus faible » the checking step promises to say.
    const QString cost = u"Son numéro est tiré de son nom, et rien d’autre n’a pu y être lu."_s;
    return why.isEmpty() ? cost : why + u' ' + cost;
}

QString comicInfoUnreadable()
{
    return u"Le ComicInfo.xml de cette archive est illisible : il y reste, et rien n’en est "
           u"repris."_s;
}

QString oneShotUnreadable()
{
    return u"Le oneshot.json de cette archive est illisible"_s + Nbsp
        + u": il y reste, et elle n’est pas lue comme un livre seul."_s;
}

QString sidecarUnreadable(const QString &name)
{
    return u"Le fichier « %1 » n’est pas un objet JSON : il ne peut être ni lu ni complété."_s
        .arg(name);
}

} // namespace Words

namespace Words {

// ——— The checking frame —————————————————————————————————————————————————————————————

namespace {

/// Where the values came from, as the line under the title ends. `nothing` differs: once the
/// roles are read from the shape, the line says what is absent by its two names.
QString sourceNamed(Source source, const QString &nothing)
{
    using enum Source;
    switch (source) {
    case Leaf:
        return u"JSON Leaf"_s;
    case ComicInfo:
        return u"ComicInfo.xml"_s;
    case Nothing:
        return nothing;
    case Several:
        return u"plusieurs sources"_s;
    }
    return {};
}

} // namespace

QString apportSummary(int universes, int series, int files, Source source)
{
    QStringList parts;
    // Universes are rare, so an absent one says nothing rather than « 0 univers ».
    if (universes > 0)
        parts << counted(universes, u"univers"_s, u"univers"_s);
    parts << counted(series, u"série"_s, u"séries"_s) << counted(files, u"fichier"_s, u"fichiers"_s);
    parts << sourceNamed(source, u"aucune information"_s);
    return parts.join(u" · "_s);
}

QString missing()
{
    return u"Manquant"_s;
}

QString fieldLabel(const QString &key, Manifest::Level level)
{
    // The same two words on every level and every screen: a series' « Auteurs » beside its
    // volumes' « Scénario » read as two different things for one field.
    Q_UNUSED(level)
    static const QHash<QString, QString> Labels{
        {u"authors"_s, u"Scénario"_s},
        {u"name"_s, u"Nom"_s},
        {u"title"_s, u"Titre"_s},
        {u"artists"_s, u"Dessin"_s},
        {u"genres"_s, u"Genres"_s},
        {u"tags"_s, u"Étiquettes"_s},
        {u"publisher"_s, u"Éditeur"_s},
        {u"language"_s, u"Langue"_s},
        {u"readingDirection"_s, u"Sens de lecture"_s},
        {u"status"_s, u"Statut"_s},
        {u"volumeCount"_s, u"Tomes prévus"_s},
        {u"number"_s, u"Numéro"_s},
        {u"publishedOn"_s, u"Parution"_s},
        {u"isbn"_s, u"ISBN"_s},
        {u"summary"_s, u"Résumé"_s},
        {u"chapters"_s, u"Chapitres"_s},
        {u"chapterLabel"_s, u"Nom des chapitres"_s},
    };
    return Labels.value(key, key);
}

QString notFilledIn()
{
    return u"Non renseigné"_s;
}

QString reviewFilter(Reviewed which)
{
    using enum Reviewed;
    switch (which) {
    case All:
        return u"Tout"_s;
    case ToReview:
        return u"À revoir"_s;
    case Ready:
        return u"Prêtes"_s;
    case Identical:
        return u"Identiques"_s;
    }
    return {};
}

QString reviewGroup(Reviewed which, int count)
{
    return u"%1 · %2"_s.arg(reviewFilter(which)).arg(count);
}

QString identicalGroup(int count, qint64 bytes)
{
    // « non renvoyés » agrees with the bytes and not with the count, so it reads the same over
    // one file as over twelve.
    return u"%1 · %2 non renvoyés"_s.arg(reviewGroup(Reviewed::Identical, count), size(bytes));
}

QString showIt()
{
    return u"Afficher"_s;
}

QString hideIt()
{
    return u"Masquer"_s;
}

QString searchTheDrop()
{
    return u"Chercher une série, un auteur…"_s;
}

QString identicalSpared(int count, qint64 bytes)
{
    // « non renvoyés » agrees with the bytes, as on the folded group.
    return plural(count, u"%1 identique"_s, u"%1 identiques"_s) + Nbsp
           + u": %1 non renvoyés"_s.arg(size(bytes));
}

QString forTicked(int count)
{
    return count == 1 ? u"Pour la série cochée"_s : u"Pour les %1 cochées"_s.arg(count);
}

QString applyToTicked(int count)
{
    return count == 1 ? u"Appliquer à la série"_s : u"Appliquer aux %1"_s.arg(count);
}

QString leaveUnchanged()
{
    return u"Ne pas changer"_s;
}

QString groupedActions()
{
    return u"actions groupées"_s;
}

QString groupedNote()
{
    return u"Une valeur choisie ici est écrite dans chacune des séries cochées. Les noms et "
           u"les numéros se règlent une série à la fois"_s
           + Nbsp + u": ils ne se copient pas."_s;
}

QString tickAll()
{
    return u"Tout"_s;
}

QString reviewNext()
{
    return u"Prochain point à vérifier"_s;
}

QString tickedCount(int count)
{
    if (count == 0)
        return u"Aucune série cochée"_s;
    return plural(count, u"%1 série cochée"_s, u"%1 séries cochées"_s);
}

QString groupedOutcome(int changed, int alreadyThere, int refused)
{
    QStringList said{changed == 0
                         ? u"Aucune série changée"_s
                         : plural(changed, u"%1 série changée"_s, u"%1 séries changées"_s)};
    if (alreadyThere > 0)
        said << plural(alreadyThere, u"%1 qui portait déjà ces valeurs"_s,
                       u"%1 qui portaient déjà ces valeurs"_s);
    if (refused > 0)
        said << plural(refused, u"%1 qui ne peut plus être corrigée"_s,
                       u"%1 qui ne peuvent plus être corrigées"_s);
    return said.join(u", "_s) + u'.';
}

QString failedChip()
{
    return u"Échec"_s;
}

QString editInformation()
{
    return u"Modifier les informations…"_s;
}

QString removeIt()
{
    return u"Retirer"_s;
}

QString apport()
{
    return u"Apport"_s;
}

QString wholeApport()
{
    return u"Tout l’apport"_s;
}

QString withoutUniverse()
{
    return u"Sans univers"_s;
}

QString seriesOfTheUniverse()
{
    return u"Séries de l’univers"_s;
}

QString volumesOf(const QString &name)
{
    return u"Tomes "_s + ofName(name);
}

QString comicInfoConverted(int files)
{
    const QString who = files >= 2 ? u"Les %1 nouveaux fichiers ont un ComicInfo.xml"_s.arg(files)
                                   : u"Le nouveau fichier a un ComicInfo.xml"_s;
    return who + u" : il sera converti en entry.json, puis retiré."_s;
}

QString everythingSaid()
{
    return u"Univers, séries, éditions et tomes disent déjà ce qu’ils sont."_s;
}

QString alreadyHeld(const QString &name, int held, int whole)
{
    return u"%1 existe déjà : %2 sur %3"_s.arg(name, counted(held, u"tome"_s, u"tomes"_s))
        .arg(whole);
}

QString sheetSubtitle(Manifest::Level level, const QString &from, bool guessedFromName)
{
    // The participle agrees with the level: a series and an edition are feminine, the rest
    // are not — « Série · lue », « Tome · lu ».
    const bool feminine = level == Manifest::Level::Work || level == Manifest::Level::Edition;
    QString how;
    if (from.isEmpty())
        how = u"rien n’est dit"_s;
    else if (guessedFromName)
        how = (feminine ? u"devinée d’après son nom"_s : u"deviné d’après son nom"_s);
    else
        how = (feminine ? u"lue dans %1"_s : u"lu dans %1"_s).arg(from);
    return u"%1 · %2"_s.arg(levelTitle(level), how);
}

QString readingDirectionUnsaid()
{
    return u"Le sens de lecture n’est dit nulle part. Il faut le choisir : une série lue à "
           u"l’envers ne se remarque pas."_s;
}

QString creditLacking(const QString &key, int volumes_)
{
    const QString role = key == u"artists"_s ? u"Dessin"_s : u"Scénario"_s;
    return u"%1 manquant pour %2"_s.arg(role, volumes(volumes_, std::nullopt));
}

QString notANumber(const QString &typed, bool whole)
{
    return (whole ? u"« %1 » ne se lit pas comme un nombre entier."_s
                  : u"« %1 » ne se lit pas comme un nombre."_s)
        .arg(typed);
}

QString notAChoice(const QString &typed)
{
    return u"« %1 » n’est pas une des valeurs proposées."_s.arg(typed);
}

QString lackingValues(int missing_)
{
    if (missing_ <= 0)
        return u"Rien ne manque"_s;
    return missing_ == 1 ? u"1 valeur manque encore"_s
                         : u"%1 valeurs manquent encore"_s.arg(missing_);
}

QString reviewFooter(int missing_, int rewritten)
{
    const QString lacking = lackingValues(missing_);
    QString rewriting = u"aucune archive réécrite"_s;
    if (rewritten > 0)
        rewriting = rewritten == 1 ? u"1 archive sera réécrite"_s
                                   : u"%1 archives seront réécrites"_s.arg(rewritten);
    return u"%1 · %2"_s.arg(lacking, rewriting);
}

QString importVolumes(int count)
{
    if (count <= 0)
        return u"Importer"_s;
    return u"Importer %1"_s.arg(counted(count, u"tome"_s, u"tomes"_s));
}

QString nothingToImport()
{
    return u"Rien à importer"_s;
}

QString fileTitle(Manifest::Level level, std::optional<double> number_, const QString &fileName)
{
    if (!number_.has_value())
        return fileName;
    return entry(level == Manifest::Level::Chapter ? Api::UpNext::Kind::Chapter
                                                   : Api::UpNext::Kind::Volume,
                 *number_, false);
}

QString importStep(int step)
{
    switch (step) {
    case 1:
        return u"Choisir"_s;
    case 2:
        return u"Vérifier"_s;
    case 3:
        return u"Envoyer"_s;
    default:
        return {};
    }
}

QString moveVolume()
{
    return u"Déplacer le tome"_s;
}

QString closeIt()
{
    return u"Fermer"_s;
}

// ——— What the shape of a folder suggests ————————————————————————————————————————————

namespace {

/// « deux », « trois » — a small count inside a sentence is written out, and a large one is
/// not: « Une série, quarante-deux éditions » reads worse than « 42 éditions ».
QString spelled(int count, bool feminine)
{
    static const QStringList small{u"zéro"_s, u"un"_s,  u"deux"_s, u"trois"_s,
                                   u"quatre"_s, u"cinq"_s, u"six"_s, u"sept"_s,
                                   u"huit"_s, u"neuf"_s, u"dix"_s};
    if (count == 1 && feminine)
        return u"une"_s;
    return count >= 0 && count < small.size() ? small.at(count) : QString::number(count);
}

/// « Elfes et Mages », « A, B, C et 2 autres » — names in a sentence, a few of them at most:
/// a shelf of nine series listed in full is a clue nobody reads to its end.
QString listed(const QStringList &names, qsizetype most)
{
    if (names.size() <= 1)
        return names.value(0);
    if (names.size() <= most)
        return names.first(names.size() - 1).join(u", "_s) + u" et "_s + names.last();
    const qsizetype others = names.size() - most;
    return names.first(most).join(u", "_s) + u" et "_s
           + plural(int(others), u"%1 autre"_s, u"%1 autres"_s);
}

} // namespace

QString role(Shape::Role role)
{
    using enum Shape::Role;

    switch (role) {
    case Shelf:
        return u"Étagère"_s;
    case Universe:
        return u"Univers"_s;
    case Work:
        return u"Série"_s;
    case Edition:
        return u"Édition"_s;
    case Volume:
        return u"Tome"_s;
    case Book:
        return u"Livre"_s;
    case Unread:
        return u"Non lu"_s;
    }
    return {};
}

QString confidence(Shape::Confidence confidence)
{
    using enum Shape::Confidence;
    switch (confidence) {
    case Certain:
        return {};
    case Likely:
        return u"probable"_s;
    case ToConfirm:
        return u"à confirmer"_s;
    }
    return {};
}

QString readingSummary(int universes, int works, int editions, qint64 volumes)
{
    QStringList said;
    if (universes > 0)
        said << counted(universes, u"univers"_s, u"univers"_s);
    if (works > 0)
        said << counted(works, u"série"_s, u"séries"_s);
    if (editions > 0)
        said << counted(editions, u"édition"_s, u"éditions"_s);
    if (volumes > 0)
        said << u"%1 %2"_s.arg(volumes).arg(volumes >= 2 ? u"tomes"_s : u"tome"_s);
    return said.join(QString(Nbsp) + u"· "_s);
}

QString readingName(Shape::Role root, int universes, int works, int editions)
{
    using enum Shape::Role;
    switch (root) {
    case Work:
        return editions <= 1 ? u"Une série, une seule édition"_s
                             : u"Une série, %1 éditions"_s.arg(spelled(editions, true));
    case Universe:
        return works <= 1 ? u"Un univers, une seule série"_s
                          : u"Un univers, %1 séries"_s.arg(spelled(works, true));
    case Shelf: {
        QStringList holds;
        if (universes > 0)
            holds << spelled(universes, false) + u" univers"_s;
        if (works > 0)
            holds << spelled(works, true) + (works >= 2 ? u" séries"_s : u" série"_s);
        return holds.isEmpty() ? u"Une étagère"_s : u"Une étagère, "_s + holds.join(u" et "_s);
    }
    case Edition:
    case Volume:
    case Book:
    case Unread:
        break;
    }
    return role(root);
}

QString clueEditionWords(const QStringList &names)
{
    if (names.size() == 1)
        return u"%1 est un mot d’édition connu."_s.arg(names.first());
    return u"%1 sont des mots d’édition connus."_s.arg(listed(names, names.size()));
}

QString clueSameNumbers(int folders, double from, double to)
{
    const QString how = spelled(folders, false);
    return u"Les %1 numérotent leurs tomes de %2 à %3"_s.arg(how, number(from), number(to))
           + Nbsp + u": probablement la même œuvre en %1 présentations."_s.arg(how);
}

QString clueOtherNumbers(const QList<int> &counts)
{
    QStringList each;
    for (const int one : counts)
        each << QString::number(one);
    return u"Des numérotations différentes, %1 tomes"_s.arg(listed(each, each.size())) + Nbsp
           + u": plutôt des séries distinctes."_s;
}

QString clueProperNames()
{
    return u"Deux séries distinctes auraient plutôt deux noms propres, comme Elfes et Mages."_s;
}

QString clueDistinctNames(const QStringList &names)
{
    return u"%1 sont des noms propres, sans mot d’édition"_s.arg(listed(names, 3)) + Nbsp
           + u": plutôt des séries distinctes."_s;
}

QString clueArchivesHere(double from, double to)
{
    return u"Des fichiers directement dedans, numérotés de %1 à %2"_s.arg(number(from),
                                                                         number(to))
           + Nbsp + u": des tomes d’une même œuvre."_s;
}

QString clueArchivesHere()
{
    return u"Des fichiers directement dedans"_s + Nbsp + u": des tomes d’une même œuvre."_s;
}

QString clueNoSubfolder()
{
    return u"Aucun sous-dossier"_s + Nbsp + u": une seule édition suffit."_s;
}

QString clueGenericName(const QString &name)
{
    return u"« %1 » ne ressemble pas au titre d’une œuvre."_s.arg(name);
}

QString clueNameToGive(const QString &name)
{
    return u"« %1 » ne ressemble pas au titre d’une œuvre"_s.arg(name) + Nbsp
           + u": il faut en donner un."_s;
}

QString clueTooDeep()
{
    return u"Des fichiers trop profonds"_s + Nbsp
           + u": sous un univers, une série et une édition, il n’y a plus de dossier."_s;
}

QString clueHoldsUniverse(const QString &name)
{
    return u"« %1 » est un univers, et un univers ne se range que sur une étagère."_s.arg(name);
}

QString clueHoldsAShelf(const QString &name)
{
    return u"« %1 » est une étagère, et un univers ne range que des séries."_s.arg(name);
}

QString clueManySeries(int count)
{
    return counted(count, u"série"_s, u"séries"_s) + Nbsp
           + u": plus qu’un univers n’en range d’habitude."_s;
}

QString clueNoUniverseInUniverse()
{
    return u"Pas d’univers dans un univers"_s + Nbsp
           + u": dans un univers, un dossier est une série."_s;
}

QString clueDropped()
{
    return u"C’est le dossier déposé lui-même"_s + Nbsp
           + u": un dossier qui range toutes ses séries ressemble aussi à un univers."_s;
}

QString clueAllDeclared()
{
    return u"Tout ce qu’il contient se déclare déjà"_s + Nbsp
           + u": un rangement, pas un univers."_s;
}

QString clueEditionsDeclared()
{
    return u"Ce qu’il contient se déclare édition"_s + Nbsp
           + u": une édition se range dans une série."_s;
}

QString roleDetail(Shape::Role role)
{
    using enum Shape::Role;
    switch (role) {
    case Shelf:
        return u"Un rangement : la bibliothèque le traverse sans le nommer."_s;
    case Universe:
        return u"Un monde que partagent plusieurs séries."_s;
    case Work:
        return u"Une œuvre, avec une seule édition si rien d’autre n’est dit."_s;
    case Edition:
        return u"Une présentation de la série qui la range."_s;
    case Volume:
        return u"Rangé dans une série, à un numéro."_s;
    case Book:
        return u"Œuvre, édition et tome à la fois."_s;
    case Unread:
        return u"Aucun rôle : il ne sera pas lu."_s;
    }
    return {};
}

QString saidBy(const QString &file)
{
    return u"Dit par %1 : à changer dans le fichier."_s.arg(file);
}

namespace {

QString impossible(const QString &why)
{
    return u"Impossible"_s + Nbsp + u": "_s + why;
}

} // namespace

QString impossibleNoSubfolder()
{
    return impossible(u"ce dossier n’a pas de sous-dossier."_s);
}

QString impossibleHoldsFiles()
{
    return impossible(u"ce dossier contient des fichiers."_s);
}

QString impossibleEditionAlone()
{
    return impossible(u"une édition se range dans une série."_s);
}

QString impossibleUniverseInUniverse()
{
    return impossible(u"pas d’univers dans un univers."_s);
}

QString impossibleFolderInUniverse()
{
    return impossible(u"dans un univers, un dossier est une série."_s);
}

QString impossibleShelfInSeries()
{
    return impossible(u"une série ne range pas d’étagère."_s);
}

QString impossibleUniverseInSeries()
{
    return impossible(u"une série ne range pas d’univers."_s);
}

QString impossibleSeriesInSeries()
{
    return impossible(u"une série ne contient pas d’autre série."_s);
}

QString impossibleInsideAnEdition()
{
    return impossible(u"une édition ne contient que ses fichiers."_s);
}

QString impossibleFilesTooDeep()
{
    return impossible(u"ses fichiers sont plus bas qu’une série et ses éditions."_s);
}

QString impossibleBeyondTheModel()
{
    return impossible(u"rien n’est lu à cette profondeur."_s);
}

QString roleBeyondTheModel()
{
    return u"Hors du modèle"_s + Nbsp + u": rien n’est lu à cette profondeur."_s;
}

QString impossibleFilesTooDeepForAUniverse()
{
    return impossible(u"ses fichiers sont plus bas qu’un univers, une série et une édition."_s);
}

QString impossibleNotAFolder()
{
    return impossible(u"ce n’est pas un dossier."_s);
}

QString impossibleBookIsAnArchive()
{
    return impossible(u"un livre seul est une archive."_s);
}

QString impossibleFilingMoves()
{
    return u"Impossible ici : ranger ce fichier dans une série, c’est le déplacer dans son "
           u"dossier."_s;
}

QString impossibleDeclaredBelow(const QString &name, const QString &file)
{
    return impossible(u"« %1 », plus bas, est dit par %2."_s.arg(name, file));
}

// ——— The checking frame, once roles are read from the shape ————————————————————————————

namespace {

/// What « · » joins in a line drawn: the space before it never breaks, so a count is never
/// left alone at the start of the next line.
QString dotted(const QStringList &parts)
{
    return parts.join(QString(Nbsp) + u"· "_s);
}

/// « Un univers », « Une série » — a role in a sentence, with its article.
QString withArticle(Shape::Role role)
{
    using enum Shape::Role;
    switch (role) {
    case Shelf:
        return u"Une étagère"_s;
    case Universe:
        return u"Un univers"_s;
    case Work:
        return u"Une série"_s;
    case Edition:
        return u"Une édition"_s;
    case Volume:
        return u"Un tome"_s;
    case Book:
        return u"Un livre"_s;
    case Unread:
        break;
    }
    return u"Non lu"_s;
}

} // namespace

QString proposedApport()
{
    return dotted({apport(), u"rôle proposé"_s});
}

QString roleHint()
{
    return u"Le rôle d’un dossier se change d’un clic sur son étiquette. Les dossiers qu’il "
           u"contient se relisent aussitôt."_s;
}

QString nothingSaid()
{
    return u"Rien n’est dit"_s + Nbsp + u": tout est lu d’après la forme des dossiers"_s;
}

QString onlyABookAtTheRoot()
{
    return u"Seul un livre seul peut se poser à la racine d’un univers"_s;
}

QString dropSummary(int folders, int files, Source source)
{
    return dotted({counted(folders, u"dossier"_s, u"dossiers"_s),
                   counted(files, u"fichier"_s, u"fichiers"_s),
                   sourceNamed(source, u"ni JSON ni ComicInfo.xml"_s)});
}

QString rootSummary(int universes, int series, int atRoot)
{
    const QString where = universes >= 2 ? u"à leur racine"_s : u"à sa racine"_s;
    return dotted({counted(universes, u"univers"_s, u"univers"_s),
                   counted(series, u"série"_s, u"séries"_s),
                   counted(atRoot, u"archive"_s, u"archives"_s) + u' ' + where});
}

QString readingChip(Shape::Role role, Shape::Confidence confidence_, bool titleMissing)
{
    const QString how = titleMissing ? u"nom à donner"_s : confidence(confidence_);
    if (how.isEmpty())
        return withArticle(role);
    return dotted({withArticle(role), how});
}

QString foldersHeld(const QList<std::pair<QString, qint64>> &folders, qint64 filesHere)
{
    QStringList parts;
    if (!folders.isEmpty()) {
        QStringList each;
        for (const auto &[name, volumes] : folders)
            // One call, three arguments: chained, a folder named `Rayon 100%2` had its own %2
            // taken for the count.
            each << u"%1 (%2 %3)"_s.arg(name, QString::number(volumes),
                                        volumes >= 2 ? u"tomes"_s : u"tome"_s);
        // Three named, then how many others: a shelf of nine folders listed in full is a line
        // nobody reads to its end.
        constexpr qsizetype Named = 3;
        QString list = each.first(std::min(each.size(), Named)).join(u", "_s);
        if (each.size() > Named)
            list += u" et "_s + plural(int(each.size() - Named), u"%1 autre"_s, u"%1 autres"_s);
        parts << counted(int(folders.size()), u"dossier"_s, u"dossiers"_s) + Nbsp + u": "_s
                     + list;
    }
    if (filesHere > 0 || folders.isEmpty())
        parts << counted(int(filesHere), u"fichier directement dedans"_s,
                         u"fichiers directement dedans"_s);
    return dotted(parts);
}

QString mostLikely()
{
    return u"Plus probable"_s;
}

QString nameToGive()
{
    return u"Nom à donner"_s;
}

QString whyTheFirst()
{
    return u"Pourquoi la première"_s;
}

QString why()
{
    return u"Pourquoi"_s;
}

QString writtenTimes(const QString &name, int count)
{
    return name + Nbsp + u"×"_s + Nbsp + QString::number(count);
}

QString writesNote()
{
    return u"Une fois confirmée, la lecture est écrite dans ces fichiers"_s + Nbsp
           + u": la forme du disque n’aura plus à être devinée."_s;
}

QString greyedRolesHint()
{
    return u"Les rôles impossibles à cet endroit sont grisés, avec la raison."_s;
}

QString roleWaitsForTheCards()
{
    return u"Pas maintenant"_s + Nbsp
           + u": un dossier que ce choix touche est encore lu, ou déjà parti."_s;
}

QString roleSpoken(const QString &name, const QString &role)
{
    return u"%1, %2"_s.arg(name, role);
}

QString rolesGreyed(const QList<std::pair<QString, QString>> &greyed)
{
    QStringList said;
    for (const auto &[label, reason] : greyed)
        said << u"Grisé"_s + Nbsp + u": "_s + label + u". "_s + reason;
    return said.join(u' ');
}

QString readingsPossible(int readings)
{
    QString count = spelled(readings, true);
    count[0] = count[0].toUpper();
    return count + u" lectures possibles"_s;
}

QString anArchive()
{
    return u"Archive"_s;
}

QString whatWillBeWritten()
{
    return u"Ce qui sera écrit"_s;
}

QString clueForMark()
{
    return u"✓"_s;
}

QString clueAgainstMark()
{
    return u"✕"_s;
}

QString roleOption(Shape::Role role_)
{
    using enum Shape::Role;
    switch (role_) {
    case Book:
        return u"Livre seul"_s;
    case Volume:
        return u"Tome d’une série"_s;
    case Shelf:
    case Universe:
    case Work:
    case Edition:
    case Unread:
        break;
    }
    return role(role_);
}

QString oneShotUnreadableReason()
{
    return impossible(u"son oneshot.json ne se lit pas, et rien n’est écrit par-dessus."_s);
}

QString declareABook()
{
    return u"Le déclarer livre seul"_s;
}

QString declareABookDetail()
{
    return u"oneshot.json est écrit dans l’archive"_s + Nbsp + u"; elle reste à la racine."_s;
}

QString fileInASeries()
{
    return u"Le ranger dans une série"_s;
}

QString unreadAtTheRoot()
{
    return u"Posé à la racine d’un univers sans se déclarer livre"_s + Nbsp
           + u": il ne sera pas lu. Il n’apparaîtra nulle part tant qu’il n’a pas de rôle."_s;
}

QString noRoleNotRead()
{
    return u"Aucun rôle"_s + Nbsp + u": il ne sera pas lu"_s;
}

QString atTheRoot(int count)
{
    return dotted({u"À la racine"_s, QString::number(count)});
}

QString insideOf(const QString &name)
{
    return u"Dans « %1 »"_s.arg(name);
}

QString leftOutCount(int archives)
{
    return archives >= 2 ? u"%1 archives sans rôle ne seront pas lues"_s.arg(archives)
                         : u"%1 archive sans rôle ne sera pas lue"_s.arg(archives);
}

QString confirmReadings(int readings)
{
    return readings >= 2 ? u"Confirmer les %1 lectures"_s.arg(readings)
                         : u"Confirmer la lecture"_s;
}

QString nothingSentBeforeReadings(int readings)
{
    return readings >= 2
               ? u"Rien n’est envoyé avant que les %1 lectures soient confirmées."_s.arg(readings)
               : u"Rien n’est envoyé avant que la lecture soit confirmée."_s;
}

QString afterLeftOut(int leftOut, const QString &rest)
{
    if (leftOut <= 0)
        return rest;
    return dotted({leftOutCount(leftOut), rest});
}

QString foldersRead(int read, int of)
{
    // Unbreakable around the slash: « 23 / » at the end of a line and « 50 dossiers lus » at
    // the start of the next read as two numbers.
    return QString::number(read) + Nbsp + u'/' + Nbsp
           + plural(of, u"%1 dossier lu"_s, u"%1 dossiers lus"_s);
}

QString whileRead(const QString &summary, int read, int of)
{
    return dotted({summary, foldersRead(read, of)});
}

QString waitingSeries(int blocked, int reading, int failed)
{
    const int all = blocked + reading + failed;
    if (all <= 0)
        return {};
    QStringList why;
    if (blocked > 0)
        why << u"%1 à compléter"_s.arg(blocked);
    if (reading > 0)
        why << u"%1 encore en lecture"_s.arg(reading);
    if (failed > 0)
        why << u"%1 en échec"_s.arg(failed);
    return plural(all, u"%1 série attend"_s, u"%1 séries attendent"_s) + Nbsp + u": "_s
           + why.join(u", "_s) + u". "_s
           + (all >= 2 ? u"Elles ne retiennent pas les autres."_s
                       : u"Elle ne retient pas les autres."_s);
}

QString creditsLine(const QStringList &authors, const QStringList &artists)
{
    // As the series page says them, so one person who writes and draws is one author here too.
    QStringList parts;
    for (const CreditLine &line : creditLines(authors, artists))
        parts << line.label + Nbsp + u": "_s + line.names.join(u", "_s);
    return dotted(parts);
}

QString volumesCredited(const QString &name)
{
    return dotted({u"Tomes "_s + ofName(name), u"crédits"_s});
}

QString creditsFooter(int people, int lacking_)
{
    QStringList parts{counted(people, u"auteur"_s, u"auteurs"_s) + u" au total"_s};
    if (lacking_ > 0)
        parts.prepend(lackingValues(lacking_));
    return dotted(parts);
}

QString creditsListed(const QStringList &people)
{
    return listed(people, 3);
}

QString ordersSummary(int universes, int series, int orders)
{
    QStringList parts;
    if (universes > 0)
        parts << counted(universes, u"univers"_s, u"univers"_s);
    parts << counted(series, u"série"_s, u"séries"_s);
    parts << u"universe.json et "_s
                 + counted(orders, u"ordre de lecture"_s, u"ordres de lecture"_s);
    return parts.join(u" · "_s);
}

QString ordersCopied(const QString &of, const QString &into)
{
    return u"« %1 » recopie les étapes de « %2 »"_s.arg(into, of) + Nbsp
           + u"; les inclure à la place laisserait un seul endroit à tenir à jour."_s;
}

QString addWord(const QString &key)
{
    if (key == u"tags"_s)
        return u"+ Étiquette"_s;
    if (key == u"genres"_s)
        return u"+ Genre"_s;
    return u"Ajouter un nom"_s;
}

QString requiredLegend()
{
    return u"obligatoire"_s;
}

QString removeWordFrom(const QString &field, const QString &word)
{
    return field + Nbsp + u": retirer « %1 »"_s.arg(word);
}

QString seriesCredits()
{
    return u"Auteurs de la série"_s;
}

QString showEveryCredit()
{
    return u"Voir tous les auteurs"_s;
}

QString foldTheCredits()
{
    return u"Replier les auteurs"_s;
}

QString arcsSection()
{
    return u"Arcs"_s;
}

QString arcsEdit(int count)
{
    if (count <= 0)
        return u"Ajouter des arcs"_s;
    return u"Éditer les arcs"_s + Nbsp + u"· "_s + QString::number(count);
}

QString arcsWindowTitle()
{
    return u"Arcs de la série"_s;
}

QString arcsAdd()
{
    return u"+"_s + Nbsp + u"Un arc"_s;
}

QString arcsFallsInChapters(const QString &fromVolume, std::optional<int> fromPage, double toChapter,
                            const QString &toVolume, std::optional<int> toPage)
{
    const auto at = [](const QString &volume, std::optional<int> page) {
        return page ? volume + u","_s + Nbsp + u"p."_s + Nbsp + QString::number(*page) : volume;
    };
    return at(fromVolume, fromPage) + Nbsp + u"→ chapitre "_s + number(toChapter) + u", "_s
           + at(toVolume, toPage);
}

QString arcsFallsInVolumes(int from, int to)
{
    if (from == to)
        return u"tome"_s + Nbsp + QString::number(from);
    return u"tomes"_s + Nbsp + QString::number(from) + u" à "_s + QString::number(to);
}

QString chaptersSection()
{
    return u"Chapitres"_s;
}

QString chaptersEdit(int count)
{
    if (count <= 0)
        return u"Ajouter des chapitres"_s;
    return u"Éditer les chapitres"_s + Nbsp + u"· "_s + QString::number(count);
}

QString chaptersWindowTitle(const QString &volume)
{
    // « Chapitres du tome 2 », as drawn: « de tome 2 » was the article left out.
    if (volume.startsWith(u"Tome "_s))
        return u"Chapitres du "_s + volume.toLower();
    return u"Chapitres de « %1 »"_s.arg(volume);
}

QString chaptersAdd()
{
    return u"+"_s + Nbsp + u"Chapitre"_s;
}

QString chaptersCount(int chapters, int pages)
{
    const QString count = QString::number(chapters) + (chapters >= 2 ? u" chapitres"_s : u" chapitre"_s);
    if (pages < 0)
        return count;
    return count + Nbsp + u"· "_s + QString::number(pages) + (pages >= 2 ? u" pages"_s : u" page"_s);
}

QString chaptersArcRuns(double from, double to, int volumes)
{
    return u"chapitres "_s + number(from) + u" à "_s + number(to) + u", sur "_s
           + QString::number(volumes) + (volumes >= 2 ? u" tomes"_s : u" tome"_s);
}

QString chaptersArcStarts(double chapter, std::optional<int> page)
{
    const QString where = u"chapitre "_s + number(chapter);
    return page ? where + u", page "_s + QString::number(*page) : where;
}

QString chaptersArcState(bool starts)
{
    return starts ? u"commence ici"_s : u"se poursuit"_s;
}

QString chaptersArcsNote()
{
    return u"Les arcs courent d’un tome à l’autre et peuvent s’arrêter au milieu de celui-ci"_s
           + Nbsp + u": ils sont montrés ici, et se modifient dans la série."_s;
}

QString chaptersArcsEdit()
{
    return u"Modifier les arcs"_s;
}

QString chaptersArcLabel()
{
    return u"Arc"_s;
}

QString defaultChapterLabel()
{
    return u"Chap."_s + Nbsp + u"{n}"_s;
}

QString chapterLabelHint()
{
    return u"{n} est le numéro, {n:000} le complète de zéros (Chap.007)"_s + Nbsp
           + u"; sans {n}, le numéro suit le nom."_s;
}

QString chaptersNumberFrom()
{
    return u"Numéroter à partir de"_s;
}

QString chaptersKeep()
{
    return u"Garder les chapitres"_s;
}

QString chaptersColumn(const QString &which)
{
    static const QHash<QString, QString> words{{u"number"_s, u"N°"_s},
                                               {u"title"_s, u"Titre"_s},
                                               {u"page"_s, u"Début"_s}};
    return words.value(which);
}

QString chaptersRefused()
{
    return u"Un numéro chacun, écrit en chiffres, et une page de début qui est un nombre entier."_s;
}

QString arcsAddSaga()
{
    return u"+"_s + Nbsp + u"Une saga"_s;
}

QString arcsAddNone()
{
    return u"+"_s + Nbsp + arcsNoSaga();
}

QString arcsKind(const QString &kind)
{
    return kind == u"saga"_s ? u"Saga"_s : u"Arc"_s;
}

QString arcsCount(int sagas, int arcs)
{
    QStringList parts;
    if (sagas > 0)
        parts << counted(sagas, u"saga"_s, u"sagas"_s);
    if (arcs > 0)
        parts << counted(arcs, u"arc"_s, u"arcs"_s);
    return parts.join(Nbsp + u"· "_s);
}

QString arcsSagaSpan(int count, const QString &unit, int from, int to)
{
    if (count <= 0)
        return {};
    return counted(count, u"arc"_s, u"arcs"_s) + Nbsp + u"· "_s
           + arcRange(unit == u"VOLUME"_s ? Api::Arc::Unit::Volume : Api::Arc::Unit::Chapter,
                      from, to);
}

QString arcsGap(int from, int to)
{
    return arcRange(Api::Arc::Unit::Chapter, from, to) + Nbsp + u"· aucun arc"_s;
}

QString arcsNoSaga()
{
    return u"Aucune saga"_s;
}

QString arcsStartAt()
{
    return u"Commencer à l’arc…"_s;
}

QString arcsEditionPick()
{
    return u"Pages et tomes de"_s;
}

QString arcsInEdition()
{
    return u"Dans cette édition"_s;
}

QString arcsMoveHint()
{
    return u"Séparateur choisi"_s + Nbsp + u": ↑ ↓ pour le déplacer, ou"_s;
}

QString arcsKeep()
{
    return u"Garder les arcs"_s;
}

QString arcsRemove()
{
    return u"Retirer"_s;
}

QString arcsColumn(const QString &which)
{
    static const QHash<QString, QString> words{{u"name"_s, u"Nom"_s},
                                               {u"parent"_s, u"Dans la saga"_s},
                                               {u"unit"_s, u"Compté en"_s},
                                               {u"from"_s, u"De"_s},
                                               {u"to"_s, u"À"_s}};
    return words.value(which);
}

QString arcsUnit(const QString &unit)
{
    return unit == u"VOLUME"_s ? u"Tomes"_s : u"Chapitres"_s;
}

QString arcsNote()
{
    return u"Les arcs se rangent seuls par leur premier chapitre ; on ne les déplace pas. "
           u"Une saga est un séparateur : un arc appartient au séparateur au-dessus de lui, "
           u"et la saga couvre les chapitres de ses arcs. Glisser un séparateur (ou ↑ ↓) "
           u"déplace sa frontière ; « Aucune saga » arrête la précédente. Retirer une "
           u"saga rend ses arcs à celle d’avant."_s;
}

QString arcsRefused()
{
    return u"Un nom chacun, un début et une fin écrits en chiffres, une fin qui ne précède pas "
           u"le début, et des sagas qui ne mêlent pas chapitres et tomes."_s;
}

QString readingOrders()
{
    return u"Ordres de lecture"_s;
}

QString defaultOrder()
{
    return u"Par défaut"_s;
}

QString subsetOrder()
{
    return u"Sous-ensemble"_s;
}

QString orderFooter(int written, int flat)
{
    return (written >= 2 ? u"%1 étapes écrites"_s : u"%1 étape écrite"_s).arg(written) + Nbsp
           + u"· %1 une fois aplati"_s.arg(flat);
}

QString saveOrders(int orders)
{
    return orders >= 2 ? u"Enregistrer les ordres"_s : u"Enregistrer l’ordre"_s;
}

QString includeWouldLoop(const QString &of, const QString &through)
{
    return u"Dans « %1 », inclure « %2 » ramènerait à « %1 »."_s.arg(of, through);
}

QString inheritedFrom(const QString &order)
{
    return u"Lu dans « %1 »"_s.arg(order) + Nbsp + u"· se modifie là-bas"_s;
}

QString includeStep(const QString &order)
{
    return u"Inclure « %1 »"_s.arg(order);
}

QString orderUnit(const QString &unit)
{
    if (unit == u"VOLUME"_s)
        return u"Tomes"_s;
    if (unit == u"CHAPTER"_s)
        return u"Chapitres"_s;
    return u"Œuvre entière"_s;
}

QString rangeBackwards()
{
    return u"La plage va à rebours"_s + Nbsp + u": son premier numéro passe le dernier."_s;
}

QString rangeNeedsEdition(const QString &work)
{
    return u"« %1 » a plusieurs éditions, et leurs tomes ne se comptent pas de la même façon"_s
               .arg(work)
           + Nbsp + u": choisissez laquelle."_s;
}

QString chapterRangeNamesNoEdition()
{
    return u"Un chapitre est le même dans chaque édition"_s + Nbsp
           + u": une plage de chapitres n’en nomme aucune."_s;
}

QString orderPastTheCeiling()
{
    return u"Le serveur ne lirait pas cet ordre en entier"_s + Nbsp
           + u": il dépasserait ce qu’un univers peut tenir une fois aplati."_s;
}

QString orderDragHint()
{
    return u"Glisser une étape pour la déplacer"_s;
}

QString orderStepColumn()
{
    return u"Étape"_s;
}

QString orderUnitColumn()
{
    return u"Unité"_s;
}

QString orderFromColumn()
{
    return u"De"_s;
}

QString orderToColumn()
{
    return u"À"_s;
}

QString proposedStep()
{
    return u"Proposé"_s;
}

QString addWorkStep()
{
    return u"+ Une œuvre"_s;
}

QString addIncludeStep()
{
    return u"+ Inclure un ordre"_s;
}

QString universeHeading()
{
    return u"Univers"_s;
}

QString ordersHeading()
{
    return u"Ordres"_s;
}

QString seriesHeading()
{
    return u"Séries"_s;
}

QString stepNumbered(int step)
{
    return u"Étape %1"_s.arg(step);
}

QString includeAnother()
{
    return u"Inclure un autre ordre"_s;
}

QString includeInstead()
{
    return u"Inclure au lieu de recopier."_s;
}

QString includeExplained()
{
    return u"L’étape nomme un ordre du même univers"_s + Nbsp
           + u"; ses étapes sont lues à sa place, à jour."_s;
}

QString includedOrder()
{
    return u"Ordre inclus"_s;
}

QString inheritedSteps()
{
    return u"Étapes héritées"_s;
}

QString flatOrder()
{
    return u"Ordre aplati"_s;
}

QString flatSteps(int steps)
{
    return counted(steps, u"étape"_s, u"étapes"_s);
}

QString followsIt(const QString &included, const QString &order)
{
    return u"Si « %1 » change, « %2 » suit."_s.arg(included, order);
}

QString nothingCopied()
{
    return u"Rien n’est recopié"_s + Nbsp + u": il n’y a qu’un endroit à tenir à jour."_s;
}

QString loopRefused()
{
    return u"Une boucle est refusée"_s;
}

QString loopExplained()
{
    return u"Un ordre ne peut pas s’inclure lui-même, même par un autre."_s;
}

QString orderDisregarded(const QString &order)
{
    return u"« %1 » porte l’identifiant d’un ordre écrit avant lui : le serveur ne le lit pas,"
           u" et rien ne peut l’inclure."_s.arg(order);
}

QString noEdition()
{
    return u"Aucune"_s;
}

QString chooseIncludedOrder()
{
    return u"Choisir l’ordre à inclure"_s;
}

QString keepInclusion()
{
    return u"Garder l’inclusion"_s;
}

QString stepWork()
{
    return u"Œuvre"_s;
}

QString stepEdition()
{
    return u"Édition"_s;
}

QString absentFromDrop()
{
    return u"Absente de ce dépôt"_s + Nbsp
           + u": l’étape est gardée, l’œuvre est peut-être déjà sur le serveur."_s;
}

QString removeStep(const QString &step)
{
    return u"Retirer « %1 »"_s.arg(step);
}

QString stepKeys()
{
    return u"Ctrl+Haut et Ctrl+Bas la déplacent, Suppr la retire"_s;
}

// ——— Réimporter : le cadre d'un tome ou d'une série visés ——————————————————————————————

QString reimportVolume(const QString &number)
{
    return u"Réimporter le %1"_s.arg(called(u"tome"_s, number));
}

QString reimportTitle(const QString &name)
{
    return u"Réimporter %1"_s.arg(name);
}

QString reimportVolumeUnder(const QString &work, const QString &edition)
{
    return u"%1 · %2"_s.arg(work, edition);
}

QString addEditionsTitle(const QString &name)
{
    return u"Ajouter des éditions à %1"_s.arg(name);
}

QString editionNameHelp()
{
    return u"Ses tomes iront dans un dossier à ce nom et seront renommés. La lecture en cours est conservée."_s;
}

QString unnamedEdition()
{
    return u"Édition sans nom"_s;
}

QString currentEdition()
{
    return u"Édition actuelle"_s;
}

QString editionNameMissing()
{
    return u"Un nom est nécessaire"_s;
}

QString editionNameTaken(const QString &with)
{
    return u"Ce nom est déjà celui de « %1 »"_s.arg(with);
}

QString addEditionsSubtitle()
{
    return u"Chaque dossier déposé devient une nouvelle édition de la série"_s;
}

QString reimportSeriesHolds(const QString &edition, int volumes)
{
    return u"Série · %1 · %2 dans la bibliothèque"_s.arg(edition,
                                                        counted(volumes, u"tome"_s, u"tomes"_s));
}

QString reimportHolds(int volumes)
{
    return u"Série · %1 dans la bibliothèque"_s.arg(counted(volumes, u"tome"_s, u"tomes"_s));
}

QString aimedRail()
{
    return u"Cible"_s;
}

QString aimedRailNoteVolume()
{
    return u"La cible est tenue : l’arbre montre où l’on se trouve, et le reste de la "
           u"bibliothèque n’est pas touché."_s;
}

QString aimedRailNoteSeries()
{
    return u"Aucune progression de lecture n’est touchée."_s;
}

QString aimedVolumeNote(const QString &number)
{
    return u"Cible : le %1"_s.arg(called(u"tome"_s, number));
}

QString aimedChanges(int added, int replaced, int absent)
{
    QStringList parts;
    if (added > 0)
        parts << u"%1 %2"_s.arg(added).arg(added >= 2 ? u"ajoutés"_s : u"ajouté"_s);
    if (replaced > 0)
        parts << u"%1 %2"_s.arg(replaced).arg(replaced >= 2 ? u"remplacés"_s : u"remplacé"_s);
    if (absent > 0)
        parts << u"%1 %2"_s.arg(absent).arg(absent >= 2 ? u"absents"_s : u"absent"_s);
    return parts.join(u" · "_s);
}

QString aimedListVolume()
{
    return u"Ce que vous apportez"_s;
}

QString aimedListSeries()
{
    return u"Tomes qui changent"_s;
}

QString volumeNumbered(const QString &number)
{
    return called(u"Tome"_s, number);
}

QString chapterNamed(double number)
{
    return u"Chapitre "_s + Words::number(number);
}

QString tileCaption(const Api::Entry &file)
{
    const QString title = file.title.value_or(QString()).trimmed();
    const std::optional<double> shown = Api::numberOf(file);
    if (!shown.has_value())
        return title.isEmpty() ? file.file : title;
    const QString number = Words::number(*shown);
    // « Tome 3 » titled « Tome 3 » is untitled; « 3 · Tome 3 » was the same word twice.
    if (const QString named = file.kind == Api::Entry::Kind::Chapter ? chapterNamed(*shown)
                                                                     : volumeNumbered(number);
        title.isEmpty() || titled(named, title) == named)
        return named;
    return titled(number, title);
}

QString coverWord(Api::Entry::Kind kind)
{
    return kind == Api::Entry::Kind::Chapter ? u"CHAPITRE"_s : u"TOME"_s;
}

QString percentRead(double fraction)
{
    return percentSent(fraction);
}

QString aimChip(Aim which)
{
    using enum Aim;
    switch (which) {
    case Replace:
        return u"Remplacer"_s;
    case Update:
        return u"Mettre à jour"_s;
    case Add:
        return u"Ajouter"_s;
    case Keep:
        return u"Garder"_s;
    case OtherNumber:
        return u"Autre tome"_s;
    case OtherSeries:
        return u"Autre série"_s;
    case Unnumbered:
        return u"Sans numéro"_s;
    case Twice:
        return u"Deux fois"_s;
    case Same:
        break;
    }
    return u"Identique"_s;
}

QString pagesGoFromTo(int before, int after, bool placeKept)
{
    const QString pages = u"%1 → %2 pages"_s.arg(before).arg(after);
    return placeKept ? pages + u" · progression gardée"_s : pages;
}

QString titleAdded(const QString &title)
{
    return u"titre ajouté « %1 »"_s.arg(title);
}

QString titleChanged(const QString &title)
{
    return u"titre changé « %1 »"_s.arg(title);
}

QString chaptersChanged()
{
    return u"chapitres changés"_s;
}

QString newVolume(int pages)
{
    return pages > 0 ? u"nouveau · %1 pages"_s.arg(pages) : u"nouveau"_s;
}

QString absentFromTheDrop()
{
    return u"absent de l’apport"_s;
}

QString sameAsHeld()
{
    return u"identique à la bibliothèque"_s;
}

QString comicInfoSays(const QString &number, int pages)
{
    return u"ComicInfo : %1 · %2 p."_s.arg(called(u"Tome"_s, number)).arg(pages);
}

QString saysItBelongsTo(const QString &series)
{
    return u"dit appartenir à « %1 »"_s.arg(series);
}

QString withoutNumber()
{
    return u"aucun numéro lu"_s;
}

QString numberTwice()
{
    return u"même numéro qu’un autre fichier"_s;
}

QString heldSide()
{
    return u"Bibliothèque"_s;
}

QString arrivingSide()
{
    return u"Apport"_s;
}

QString pagesLabel()
{
    return u"Pages"_s;
}

QString chaptersLabel()
{
    return u"Chapitres"_s;
}

QString progressLabel()
{
    return u"Progression"_s;
}

QString comicInfoLabel()
{
    return u"ComicInfo"_s;
}

QString chaptersSpan(int count, const QString &first, const QString &last)
{
    if (count <= 0)
        return u"0"_s;
    return u"%1 (%2 à %3)"_s.arg(count).arg(first, last);
}

QString pageNumbered(int page)
{
    return u"page %1"_s.arg(page);
}

QString comicInfoRemoved()
{
    return u"sera retiré"_s;
}

QString fileIsAnotherVolume(const QString &own, const QString &aimed)
{
    return u"Ce fichier est le %1, pas le %2."_s.arg(called(u"tome"_s, own),
                                                     called(u"tome"_s, aimed));
}

QString nothingWasModified()
{
    return u"Son ComicInfo et son nom le disent. Rien n’a été modifié."_s;
}

QString fileBelongsToAnotherSeries(const QString &series)
{
    return u"Ce fichier dit appartenir à « %1 »."_s.arg(series);
}

QString filePutAtVolume(const QString &number)
{
    return u"Ranger au %1"_s.arg(called(u"tome"_s, number));
}

QString recommended()
{
    return u"Conseillé"_s;
}

QString volumeIsAdded(const QString &number, const QString &aimed)
{
    return u"Le %1 n’existe pas : il est ajouté. Le %2 reste intact."_s.arg(
        called(u"tome"_s, number), called(u"tome"_s, aimed));
}

QString volumeIsReplaced(const QString &number, const QString &aimed)
{
    return u"Le %1 est remplacé. Le %2 reste intact."_s.arg(called(u"tome"_s, number),
                                                            called(u"tome"_s, aimed));
}

QString replaceAnyway(const QString &aimed)
{
    return u"Remplacer quand même le %1"_s.arg(called(u"tome"_s, aimed));
}

QString pagesGoFromToSentence(int before, int after)
{
    return u"Les pages passent de %1 à %2."_s.arg(before).arg(after);
}

QString progressStays(const QString &number)
{
    return u"Votre progression appartient au %1 : elle ne bouge pas."_s.arg(
        called(u"tome"_s, number));
}

QString keepProgress()
{
    return u"Garder la progression"_s;
}

QString resetProgress()
{
    return u"La remettre à zéro"_s;
}

QString volumeNotInTheDrop(const QString &number)
{
    return u"Le %1 n’est pas dans l’apport."_s.arg(called(u"tome"_s, number));
}

QString volumeStaysInTheLibrary()
{
    return u"Il reste dans la bibliothèque : une série ne perd jamais un tome sans que vous "
           u"le demandiez."_s;
}

QString notDeclared()
{
    return u"Non renseigné"_s;
}

QString infosChanged(int count)
{
    return u"%1 %2"_s.arg(count).arg(count >= 2 ? u"infos modifiées"_s : u"info modifiée"_s);
}

QString aimedFooter()
{
    return u"Les interrupteurs choisissent ce qui est appliqué."_s;
}

QString applyChanges(int count)
{
    return u"Appliquer %1"_s.arg(counted(count, u"changement"_s, u"changements"_s));
}

QString aimedDrop(const QString &kind)
{
    if (kind == u"SERIES"_s)
        return u"Un dossier de série"_s;
    if (kind == u"UNIVERSE"_s)
        return u"Un dossier d’univers"_s;
    if (kind == u"SERIES_NEW_EDITIONS"_s)
        return u"Un dossier par édition"_s;
    return u"Un fichier"_s;
}

QString anotherFile()
{
    return u"Autre fichier"_s;
}

QString newValue()
{
    return u"Nouveau"_s;
}

QString reimportAsked(const QString &kind, const QString &name)
{
    if (kind == u"ENTRY"_s || kind == u"CHAPTER"_s)
        return reimportVolume(name);
    if (kind == u"SERIES_NEW_EDITIONS"_s)
        return addEditionsTitle(name);
    return reimportTitle(name);
}

QString joinDetails(const QStringList &parts)
{
    return parts.join(u" · "_s);
}

QString identicalNothingToDo(int count)
{
    return u"Identiques · %1 · rien à faire"_s.arg(count);
}

// ——— Remplacer un chapitre ———————————————————————————————————————————————————————————

QString replaceChapterTitle(const QString &volume)
{
    return u"Remplacer un chapitre du %1"_s.arg(called(u"tome"_s, volume));
}

QString chaptersOfVolume(const QString &volume)
{
    return u"Chapitres du %1"_s.arg(called(u"tome"_s, volume));
}

QString chapterListed(const QString &number, const QString &title)
{
    if (number.isEmpty())
        return title;
    return title.isEmpty() ? number : u"%1 · %2"_s.arg(number, title);
}

QString pagesRange(int from, int to)
{
    return u"pages %1 à %2"_s.arg(from).arg(to);
}

QString pagesRangeChange(int from, int to, int becomesFrom, int becomesTo)
{
    return u"pages %1 à %2 → %3 à %4"_s.arg(from).arg(to).arg(becomesFrom).arg(becomesTo);
}

QString chapterReplaced()
{
    return u"Remplacer"_s;
}

QString chapterKept()
{
    return u"Garder"_s;
}

QString chapterShifted(int by)
{
    return u"Décalé de %1"_s.arg(by);
}

QString spliceExtra(int delta)
{
    if (delta == 0)
        return u"Même longueur"_s;
    return delta > 0 ? plural(delta, u"%1 page en plus"_s, u"%1 pages en plus"_s)
                     : plural(-delta, u"%1 page en moins"_s, u"%1 pages en moins"_s);
}

QString spliceVolumeNote(const QString &volume, int before, int after)
{
    if (before == after)
        return u"Le %1 garde ses %2 pages."_s.arg(called(u"tome"_s, volume)).arg(before);
    return u"Le %1 passe de %2 à %3 pages ; les chapitres suivants sont décalés."_s
        .arg(called(u"tome"_s, volume))
        .arg(before)
        .arg(after);
}

QString spliceProgressKept(int page, const QString &chapter)
{
    return u"Votre progression, page %1, reste dans le chapitre %2 : elle est gardée."_s
        .arg(page)
        .arg(chapter);
}

QString spliceProgressBrought(int page, int becomes)
{
    return u"Votre progression, page %1, est ramenée à la page %2."_s.arg(page).arg(becomes);
}

QString spliceSource(const QString &file)
{
    return u"Source : %1"_s.arg(file);
}

QString spliceFooter(const QString &volume)
{
    return u"L’archive du %1 est réécrite une fois."_s.arg(called(u"tome"_s, volume));
}

QString replaceChapterLabel()
{
    return u"Remplacer le chapitre"_s;
}

QString spliceFromLabel()
{
    return u"De"_s;
}

QString spliceToLabel()
{
    return u"à"_s;
}

QString spliceCurrentSide()
{
    return u"Actuel"_s;
}

QString spliceSourceSide()
{
    return u"Source"_s;
}

// ——— Un univers visé, et une série déjà là ——————————————————————————————————————————

QString reimportUniverse(const QString &name)
{
    return u"Réimporter l’univers « %1 »…"_s.arg(name);
}

QString universeSubtitle(int held, int arriving)
{
    return u"Univers · %1 dans la bibliothèque · %2 dans l’apport"_s.arg(
        counted(held, u"série"_s, u"séries"_s)).arg(arriving);
}

QString moveDetail(const QString &from, int held, int arriving)
{
    const QString where = from.isEmpty() ? u"à la racine"_s : u"dans « %1 »"_s.arg(from);
    return u"%1, %2 → %3 tomes dans l’univers"_s.arg(where).arg(held).arg(arriving);
}

QString newSeriesDetail(int volumes)
{
    return u"nouvelle série · %1"_s.arg(counted(volumes, u"tome"_s, u"tomes"_s));
}

QString absentSeriesDetail(int volumes)
{
    return u"absente de l’apport · %1"_s.arg(counted(volumes, u"tome"_s, u"tomes"_s));
}

QString changedSeriesDetail(int changed, int added)
{
    QStringList parts;
    if (changed > 0)
        parts << plural(changed, u"%1 tome modifié"_s, u"%1 tomes modifiés"_s);
    if (added > 0)
        parts << plural(added, u"%1 tome ajouté"_s, u"%1 tomes ajoutés"_s);
    return parts.join(u" · "_s);
}

QString identicalSeriesDetail(int volumes)
{
    return plural(volumes, u"%1 tome identique"_s, u"%1 tomes identiques"_s);
}

QString seriesMoveChip()
{
    return u"Déplacer"_s;
}

QString seriesNewChip()
{
    return u"Ajouter"_s;
}

QString seriesKeepChip()
{
    return u"Garder"_s;
}

QString seriesChangedChip()
{
    return u"Modifier"_s;
}

QString seriesIdenticalChip()
{
    return u"Identique"_s;
}

QString seriesExistsChip()
{
    return u"Existe déjà"_s;
}

QString aimedListUniverse()
{
    return u"Séries"_s;
}

QString universeRailNote()
{
    return u"Aucune série n’est retirée de l’univers sans que vous le demandiez."_s;
}

QString universeFooter()
{
    return u"Les séries identiques ne sont ni lues ni renvoyées."_s;
}

QString universeOrdersNote()
{
    return u"Univers + ordre nommé ajouté"_s;
}

QString existsAlreadyAt(const QString &from)
{
    return from.isEmpty() ? u"Elle existe déjà, à la racine."_s
                          : u"Elle existe déjà, dans « %1 »."_s.arg(from);
}

QString moveItWithReadings()
{
    return u"On la déplace dedans avec ses lectures, au lieu d’en créer une seconde."_s;
}

QString moveIntoUniverse()
{
    return u"Déplacer dans l’univers"_s;
}

QString moveKeepsDetail(int kept, int added)
{
    return u"%1 ; %2."_s
        .arg(plural(kept, u"%1 tome garde sa progression"_s,
                    u"%1 tomes gardent leur progression"_s))
        .arg(plural(added, u"%1 est ajouté"_s, u"%1 sont ajoutés"_s));
}

QString createSecondSeries()
{
    return u"Créer une seconde série"_s;
}

QString existingStaysPut()
{
    return u"L’existante ne bouge pas."_s;
}

QString alreadyInLibrary(const QString &name)
{
    return u"« %1 » est déjà dans la bibliothèque."_s.arg(name);
}

QString reimportOnto(const QString &name)
{
    return u"Réimporter sur « %1 »"_s.arg(name);
}

QString newEditionOf(const QString &name)
{
    return u"Nouvelle édition de « %1 »"_s.arg(name);
}

QString newEditionDetail()
{
    return u"Le dossier devient une édition de plus de la série, qui garde les siennes"_s;
}

QString filedUnder(const QString &folder)
{
    return u"Sera rangé sous %1"_s.arg(folder);
}

QString reshelved(int count, const QString &edition)
{
    return (count == 1 ? u"%1 tome → passe dans %2 et prend ce nom, lecture conservée"_s
                       : u"%1 tomes → passent dans %2 et prennent ce nom, lecture conservée"_s)
        .arg(count)
        .arg(edition);
}

QString renamedExample(const QString &was, const QString &now)
{
    return u"%1 → %2"_s.arg(was, now);
}

QString namesMeet(const QString &first, const QString &second)
{
    return (u"Deux fichiers prendraient le même nom"_s + Nbsp + u": %1 et %2."_s)
        .arg(first, second);
}

} // namespace Words
