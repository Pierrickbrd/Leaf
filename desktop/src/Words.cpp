#include "Words.h"

#include <QDateTime>
#include <QLocale>

#include <QStringList>

#include <cmath>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// A number as French writes it: no decimal point, and no trailing zero on a whole one.
///
/// Volume numbers are halves as often as not — a 3.5 is a side story — and « Tome 3.5 » is
/// English. « Tome 3,5 » is French, and « Tome 12,0 » is neither.
QString number(double value)
{
    if (std::abs(value - std::round(value)) < 0.0001)
        return QString::number(static_cast<qint64>(std::llround(value)));
    return QString::number(value, 'g', 4).replace(u'.', u',');
}

/// Only a volume shortens. A chapter's number is the entry's own identity, and « Ch. 98 » is
/// an abbreviation nobody has agreed on — so the short line drops the chapter segment rather
/// than inventing a spelling for it.
QString entry(Api::UpNext::Kind kind, double value, bool briefly)
{
    if (kind == Api::UpNext::Kind::Chapter)
        return u"Chapitre "_s + number(value);
    return (briefly ? u"T"_s : u"Tome "_s) + number(value);
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
    return u"Rien à filtrer ici : tout ce que vous avez porte les mêmes réponses."_s;
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
    return u"Le scan a échoué : %1"_s.arg(why);
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

} // namespace Words
