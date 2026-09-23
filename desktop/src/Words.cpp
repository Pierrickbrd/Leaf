#include "Words.h"

#include <QDateTime>
#include <QLocale>

#include <QStringList>

#include <cmath>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

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

    if (number_.has_value())
        parts << entry(kind, *number_, briefly);

    if (page.has_value() && pageCount > 0)
        parts << u"Page %1/%2"_s.arg(*page).arg(pageCount);

    // A chapter entry already said which chapter it is; repeating it would read as two.
    if (chapter && !chapter->isEmpty() && kind != Api::UpNext::Kind::Chapter)
        parts << *chapter;

    return parts.join(u" · "_s);
}

} // namespace

namespace Words {

const QChar Nbsp = QChar(0x00A0);

QString volumes(int count, std::optional<Api::Medium> medium)
{
    // Zero and one both take the singular in French; two and above do not.
    const bool many = count >= 2;
    if (medium == Api::Medium::Bd)
        return u"%1 %2"_s.arg(count).arg(many ? u"albums"_s : u"album"_s);
    return u"%1 %2"_s.arg(count).arg(many ? u"tomes"_s : u"tome"_s);
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

QString searchWithin(const QString &axisTitle)
{
    return u"Chercher dans %1…"_s.arg(axisTitle.toLower());
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

QString backTo(const QString &destination)
{
    return destination.isEmpty() ? u"Retour"_s : u"Retour à %1"_s.arg(destination.toLower());
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

namespace {

/// « 6 séries », « 1 série ». Written once because seven counts follow the same rule and
/// six of them would otherwise each grow their own plural.
QString counted(int how, const QString &one, const QString &many)
{
    return u"%1 %2"_s.arg(how).arg(how == 1 ? one : many);
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
    // The server may learn a third — « hiatus » was refused once and may not always be — and
    // a word this client has not been taught is shown rather than dropped: the reader can
    // still choose it, and it is their own library that is saying it.
    return word;
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
        && *hit.entryTitle != container)
        parts << *hit.entryTitle;
    if (hit.entryPageCount.has_value() && *hit.entryPageCount > 0) {
        parts << (u"%1 page"_s.arg(*hit.entryPageCount)
                  + (*hit.entryPageCount >= 2 ? u"s"_s : QString()));
    }
    return parts.join(u" · "_s);
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
    return u"Le serveur a répondu quelque chose d’illisible."_s;
}

QString keyRefused(const QString &said)
{
    if (said.isEmpty())
        return u"La clé a été refusée."_s;
    return u"La clé a été refusée"_s + Nbsp + u": "_s + said;
}

QString tooManyWrongKeys(const QString &seconds)
{
    return u"Trop de clés fausses ont été essayées. Attendez %1 secondes."_s
        .arg(seconds);
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
    return u"Encore %1 secondes avant de redemander."_s.arg(seconds);
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

QString importing()
{
    return u"Importer"_s;
}

QString dropFilesHere()
{
    return u"Déposez vos fichiers ici"_s;
}

QString dropHow()
{
    return u"Depuis votre gestionnaire de fichiers, n’importe où sur la fenêtre"_s;
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

QString startImport()
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

QString willCreate(const QString &kind, const QString &name)
{
    QString what = kind;
    if (kind == u"UNIVERSE"_s)
        what = u"l’univers"_s;
    else if (kind == u"WORK"_s)
        what = u"la série"_s;
    else if (kind == u"EDITION"_s)
        what = u"l’édition"_s;
    return u"créera %1 « %2 »"_s.arg(what, name);
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
    return u"Calcule l’empreinte de chaque tome avant l’envoi et la recompare à l’arrivée. "
           u"Plus long à préparer"_s +
           Nbsp + u"; sans elle, un tome abîmé en route s’installe sans que rien le dise."_s;
}

QString chooseLead()
{
    return u"ou choisir"_s;
}

QString chooseFiles()
{
    return u"Des fichiers"_s;
}

QString chooseFilesTitle()
{
    return u"Fichiers à importer"_s;
}

QString chooseFolder()
{
    return u"Un dossier"_s;
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

QString nodeHolds(qint64 volumes, qint64 bytes)
{
    if (volumes <= 0)
        return {};
    return u"%1 · %2"_s.arg(counted(int(volumes), u"tome"_s, u"tomes"_s), size(bytes));
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

QString waitingToBeChecked()
{
    return u"En attente"_s;
}

QString dropSomethingElse()
{
    return u"Déposer autre chose"_s;
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
    return u"Le sidecar de cette archive est trop gros pour en être un."_s;
}

QString sidecarCompressedInAnUnknownWay()
{
    return u"Le sidecar de cette archive est compressé d’une façon inconnue."_s;
}

QString sidecarCouldNotBeInflated()
{
    return u"Le sidecar de cette archive n’a pas pu être décompressé."_s;
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

QString makers(const Api::Series &one)
{
    QStringList said;
    if (!one.credits.authors.isEmpty())
        said << one.credits.authors.join(u", "_s);
    else if (one.credits.author && !one.credits.author->isEmpty())
        said << *one.credits.author;
    if (one.publication.publisher)
        said << *one.publication.publisher;
    if (one.medium)
        said << medium(*one.medium);
    if (one.run)
        said << editionStatus(*one.run == Api::Run::Completed ? u"completed"_s : u"ongoing"_s);
    return said.join(u" · "_s);
}

QString weights(const Api::Series &one)
{
    QStringList said;
    // What this library holds, counted in the medium's own word — a BD comes in albums.
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

QString hereToo(const QString &detail)
{
    return detail.isEmpty() ? u"ici"_s : detail + u" · ici"_s;
}

} // namespace Words
