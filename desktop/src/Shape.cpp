#include "Shape.h"

#include "Words.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

Shape::Cost &spent()
{
    static Shape::Cost total;
    return total;
}

using Shape::Role;

/// Lowercase, accents taken off: « INTÉGRALE », « Intégrale » and « integrale » are one word
/// typed three ways, and a folder is named by whoever ripped it, on whatever keyboard they had.
/// NFKD splits a letter from its accent, and the accent is the mark left behind.
QString folded(const QString &text)
{
    const QString apart = text.normalized(QString::NormalizationForm_KD);
    QString kept;
    kept.reserve(apart.size());
    for (const QChar ch : apart) {
        const QChar::Category category = ch.category();
        if (category != QChar::Mark_NonSpacing && category != QChar::Mark_SpacingCombining
            && category != QChar::Mark_Enclosing)
            kept.append(ch);
    }
    return kept.toCaseFolded();
}

/// The words of a name, folded. A word is a run of letters and digits: matching inside the
/// name instead found « perfect » in « Perfectionniste », somebody's series and no edition.
QStringList wordsOf(const QString &name)
{
    QStringList words;
    QString word;
    for (const QChar ch : folded(name)) {
        if (ch.isLetterOrNumber()) {
            word.append(ch);
        } else if (!word.isEmpty()) {
            words << word;
            word.clear();
        }
    }
    if (!word.isEmpty())
        words << word;
    return words;
}

/// What a name is once its numbers are dropped — « Nouveau dossier (2) » is still the folder
/// the system named, and « BD 2024 » still a shelf.
QString withoutNumbers(const QString &name)
{
    QStringList kept;
    for (const QString &word : wordsOf(name)) {
        if (!std::ranges::all_of(word, [](QChar ch) { return ch.isDigit(); }))
            kept << word;
    }
    return kept.join(u' ');
}

QSet<QString> foldedSet(const QStringList &said)
{
    QSet<QString> out;
    for (const QString &one : said)
        out.insert(withoutNumbers(one));
    return out;
}

/// The words that name a presentation of a work rather than a work: the ones French and
/// Japanese publishers print on the spine of a reissue, and the ones a library ripped from
/// either keeps in its folder names. A word here and a word missing cost differently — one
/// missing leaves two editions read as two series, which the reader corrects; one too many
/// reads two series as editions, which then offer each other in the edition switcher.
const QSet<QString> &editionWords()
{
    static const QSet<QString> words = foldedSet(
        {u"perfect"_s, u"original"_s, u"originale"_s, u"deluxe"_s, u"intégrale"_s,
         u"ultimate"_s, u"collector"_s, u"couleur"_s, u"colorée"_s, u"color"_s, u"colour"_s,
         u"kanzenban"_s, u"bunko"_s, u"wideban"_s, u"aizōban"_s, u"shinsōban"_s, u"omnibus"_s,
         u"édition"_s, u"edition"_s, u"remastered"_s, u"prestige"_s, u"anniversaire"_s,
         u"double"_s});
    return words;
}

/// The names people give a place they put things away — a medium, a download folder, the
/// name a system gives a new folder — and never a title. A universe is named after the world
/// its series share; a folder called « Mangas » holding Bleach and Naruto is not one.
const QSet<QString> &genericNames()
{
    static const QSet<QString> names = foldedSet(
        {u"bd"_s, u"bandes dessinées"_s, u"manga"_s, u"mangas"_s, u"comics"_s,
         u"bibliothèque"_s, u"library"_s, u"livres"_s, u"books"_s, u"séries"_s, u"series"_s,
         u"vrac"_s, u"divers"_s, u"misc"_s, u"à trier"_s, u"nouveau dossier"_s,
         u"new folder"_s, u"téléchargements"_s, u"downloads"_s, u"import"_s, u"scans"_s,
         u"tmp"_s, u"temp"_s});
    return names;
}

/// Where a folder's children are read: under a universe a folder is a series, under a series
/// an edition, and anywhere else it says for itself. `Beyond` is under an edition, or under a
/// shelf inside a universe or a series — a folder there that holds no archive it can file:
/// the scan stops there, and what sits below has no role the model can give it.
enum class Within { Nothing, Universe, Work, Beyond };

Within within(Role role, Within where)
{
    // An edition holds archives and nothing else: `Bonus/Extra` under a series came back as
    // an edition inside an edition.
    if (where == Within::Beyond || role == Role::Edition
        || (where != Within::Nothing && role == Role::Shelf))
        return Within::Beyond;
    switch (role) {
    case Role::Universe:
        return Within::Universe;
    case Role::Work:
    case Role::Edition:
        return Within::Work;
    case Role::Shelf:
    case Role::Volume:
    case Role::Book:
    case Role::Unread:
        break;
    }
    return Within::Nothing;
}

Role roleOf(Manifest::Level level)
{
    switch (level) {
    case Manifest::Level::Universe:
        return Role::Universe;
    case Manifest::Level::Work:
        return Role::Work;
    case Manifest::Level::Edition:
        return Role::Edition;
    case Manifest::Level::Book:
        return Role::Book;
    case Manifest::Level::Chapter:
    case Manifest::Level::Volume:
        break;
    }
    return Role::Volume;
}

/// The numbers a folder's file names give, sorted, each once.
QList<double> numbersIn(const Manifest::Node &folder)
{
    QList<double> numbers;
    for (const Manifest::Entry &file : folder.files) {
        if (const std::optional<double> number = Manifest::numberOf(file.path.section(u'/', -1)))
            numbers << *number;
    }
    std::ranges::sort(numbers);
    numbers.erase(std::ranges::unique(numbers).begin(), numbers.end());
    return numbers;
}

/// How far down the deepest archive sits: 0 in the folder itself, -1 when there is none.
int depthOf(const Manifest::Node &folder)
{
    int deepest = folder.files.isEmpty() ? -1 : 0;
    for (const Manifest::Node &child : folder.children) {
        if (const int below = depthOf(child); below >= 0)
            deepest = std::max(deepest, below + 1);
    }
    return deepest;
}

/// Whether two numberings say « two series »: nothing in common, or different lengths with
/// neither inside the other. 1 to 40 inside 1 to 42 is what an edition missing two volumes
/// looks like, and two series both counting from 1 look exactly the same — so it says
/// neither, rather than one of them by default.
bool numberedApart(const QList<double> &one, const QList<double> &other)
{
    QList<double> common;
    std::ranges::set_intersection(one, other, std::back_inserter(common));
    if (common.isEmpty())
        return true;
    return one.size() != other.size() && !std::ranges::includes(one, other)
           && !std::ranges::includes(other, one);
}

/// Whether every two of `numberings` are `numberedApart`.
bool pairwiseApart(const QList<QList<double>> &numberings)
{
    for (qsizetype i = 0; i < numberings.size(); ++i) {
        for (qsizetype j = i + 1; j < numberings.size(); ++j) {
            if (!numberedApart(numberings.at(i), numberings.at(j)))
                return false;
        }
    }
    return true;
}

/// One clue weighing editions against series, on the side it counts for.
struct Indicator {
    bool forEditions = true;
    QString text;
};

/// The points of a folder of folders, editions against series, and why.
struct Points {
    int editions = 0;
    int series = 0;
    QList<Indicator> said;
};

Points pointsOf(const Manifest::Node &folder)
{
    Points points;
    const QList<Manifest::Node> &children = folder.children;

    QStringList names;
    QStringList withEditionWord;
    QSet<QString> distinct;
    for (const Manifest::Node &child : children) {
        names << child.name;
        distinct.insert(folded(child.name));
        if (Shape::isEditionWord(child.name))
            withEditionWord << child.name;
    }
    if (!withEditionWord.isEmpty() && withEditionWord.size() * 2 >= children.size()) {
        points.editions += 2;
        points.said << Indicator{true, Words::clueEditionWords(withEditionWord)};
    }

    QList<QList<double>> numberings;
    for (const Manifest::Node &child : children)
        numberings << numbersIn(child);
    const bool everyOneNumbered =
        std::ranges::none_of(numberings, [](const QList<double> &n) { return n.isEmpty(); });
    // Siblings compared with each other, so a lone subfolder is compared with nothing: its
    // numbers alone would otherwise be « the same in every folder » and make it an edition.
    const bool siblings = numberings.size() >= 2;
    const bool allTheSame =
        siblings && std::ranges::all_of(numberings, [&numberings](const QList<double> &n) {
            return n.size() >= 2 && n == numberings.constFirst();
        });
    const bool allApart = siblings && everyOneNumbered && pairwiseApart(numberings);
    if (allTheSame) {
        const QList<double> &same = numberings.constFirst();
        points.editions += 2;
        points.said << Indicator{true, Words::clueSameNumbers(int(numberings.size()),
                                                              same.constFirst(), same.constLast())};
    } else if (allApart) {
        points.series += 2;
        QList<int> counts;
        for (const QList<double> &n : std::as_const(numberings))
            counts << int(n.size());
        points.said << Indicator{false, Words::clueOtherNumbers(counts)};
    }

    // Two proper names numbered alike may be two editions of one work; from three, they
    // weigh as much as the numbers and the series win — three trilogies were one work. A
    // work in three editions is then three series of a universe, still « à confirmer » with
    // the editions offered beside it: the less serious mistake, merged again in a click.
    if (withEditionWord.isEmpty() && children.size() >= 2 && distinct.size() == children.size()) {
        points.series += children.size() >= 3 ? 2 : 1;
        points.said << Indicator{false, Words::clueDistinctNames(names)};
    }
    return points;
}

/// The floors of the model under a folder the scan classifies — universe, series, edition —
/// `layout::MODEL_DEPTH` on the server. Archives further down than that are read by nobody.
constexpr int ModelDepth = 3;

/// How near the nearest archive sits: 0 in the folder itself, -1 when there is none.
///
/// The nearest and not the deepest: `Naruto` holds its volumes and a `Bonus/Extra` too deep
/// for any role, and is a series all the same — the scan reads its volumes and leaves the
/// rest, which its menu then says.
int nearestOf(const Manifest::Node &folder)
{
    if (!folder.files.isEmpty())
        return 0;
    int nearest = -1;
    for (const Manifest::Node &child : folder.children) {
        if (const int below = nearestOf(child); below >= 0)
            nearest = nearest < 0 ? below + 1 : std::min(nearest, below + 1);
    }
    return nearest;
}

/// What sits at a universe's root, or where no role reaches, is filed nowhere.
bool filesUnread(Role folderRole)
{
    return folderRole == Role::Universe || folderRole == Role::Shelf;
}

/// Whether a name says how a work is presented and nothing else — « Intégrale », « Perfect
/// Edition ». « Dragon Ball Perfect » is a title with an edition word in it, and keeps it.
bool namedByItsEdition(const QString &name)
{
    const QStringList words = withoutNumbers(name).split(u' ', Qt::SkipEmptyParts);
    return !words.isEmpty() && std::ranges::all_of(words, [](const QString &word) {
        return editionWords().contains(word);
    });
}

/// A series under a name that is no title asks for one.
bool titleMissing(const QString &name)
{
    return Shape::isGenericName(name) || namedByItsEdition(name);
}

/// Reads a drop from its whole tree: each folder's role where it stands, and the suggestion
/// on each folder whose role a reading decides.
class Reader
{
public:
    /// `choices`: a role somebody chose stands in what this reads, as a sidecar's does. Off
    /// for the first reading of a drop and for `rereadBelow`, which weighs choices itself.
    explicit Reader(Shape::Drop &drop, bool choices = false) : m_drop(drop), m_choices(choices)
    {
    }

    void run() { place(m_drop.whole, Within::Nothing); }

    /// Makes again the suggestion of `folder`, read where `where` puts it, from the roles as
    /// they now stand below it — or drops it where none is made there.
    void suggestAgain(const Manifest::Node &folder, Within where)
    {
        m_drop.suggestions.remove(folder.at);
        if (folder.declared)
            return;
        if (where == Within::Nothing) {
            if (const Free read = alone(folder); read.suggests)
                m_drop.suggestions.insert(folder.at, read.said);
        } else if (where == Within::Universe && roleIn(folder, where) == Role::Work) {
            m_drop.suggestions.insert(folder.at, inUniverse(folder));
        }
    }

    /// What a folder that declares nothing would be with no universe or series above it —
    /// what a change of role above reads it as, once it is under a shelf again.
    Role freeRole(const Manifest::Node &folder) { return alone(folder).role; }

private:
    /// `freeRole`'s answer, with the suggestion that comes with it.
    struct Free {
        Role role = Role::Shelf;
        bool suggests = false;
        Shape::Suggestion said;
        /// The universe somewhere under it, which makes it a shelf for certain.
        QString universeBelow;
    };

    Shape::Drop &m_drop;
    bool m_choices = false;
    /// A folder's free reading depends on its children's, and the readings of its ancestors
    /// ask again: kept, so each folder is read once.
    QHash<QString, Free> m_free;

    Free alone(const Manifest::Node &folder)
    {
        if (const auto known = m_free.constFind(folder.at); known != m_free.cend())
            return *known;
        Free decided = decideFree(folder);
        m_free.insert(folder.at, decided);
        return decided;
    }

    Role roleIn(const Manifest::Node &folder, Within where)
    {
        if (folder.declared)
            return roleOf(folder.level);
        if (m_choices) {
            if (const Shape::Choice chosen = m_drop.roles.value(folder.at);
                chosen.origin == Shape::Origin::Chosen)
                return chosen.role;
        }
        switch (where) {
        case Within::Universe:
            // A series whose nearest archive is further down than its editions holds
            // nothing the scan reads: read as a series, it had a role its own menu greyed.
            return nearestOf(folder) > 1 ? Role::Shelf : Role::Work;
        case Within::Work:
            // Only a folder holding archives is an edition: one holding none is nothing the
            // scan reads, and neither is anything under it.
            return folder.files.isEmpty() ? Role::Shelf : Role::Edition;
        case Within::Beyond:
            return Role::Shelf;
        case Within::Nothing:
            break;
        }
        return alone(folder).role;
    }

    void assign(const Manifest::Node &folder, Role role, Within where,
                QHash<QString, Role> &into)
    {
        into.insert(folder.at, role);
        const Within below = within(role, where);
        for (const Manifest::Node &child : folder.children)
            assign(child, roleIn(child, below), below, into);
    }

    static void count(const Manifest::Node &folder, Shape::Reading &reading)
    {
        using enum Shape::Role;
        const Role role = reading.roles.value(folder.at);
        reading.universes += role == Universe ? 1 : 0;
        reading.works += role == Work ? 1 : 0;
        reading.editions += role == Edition ? 1 : 0;
        if (!filesUnread(role))
            reading.volumes += folder.files.size();
        for (const Manifest::Node &child : folder.children)
            count(child, reading);
    }

    Shape::Reading reading(const Manifest::Node &folder, Role role)
    {
        Shape::Reading made;
        assign(folder, role, Within::Nothing, made.roles);
        count(folder, made);
        return made;
    }

    /// Shelf past `MostUniverseSeries` or under a generic name, universe otherwise.
    static Role seriesHolder(const Manifest::Node &folder)
    {
        const bool many = folder.children.size() > Shape::MostUniverseSeries;
        return many || Shape::isGenericName(folder.name) ? Role::Shelf : Role::Universe;
    }

    static void addShelfClues(const Manifest::Node &folder, Shape::Suggestion &said)
    {
        if (Shape::isGenericName(folder.name))
            said.clues << Shape::Clue{true, Words::clueGenericName(folder.name)};
        if (folder.children.size() > Shape::MostUniverseSeries)
            said.clues << Shape::Clue{true, Words::clueManySeries(int(folder.children.size()))};
    }

    /// A universe that would be the dropped folder itself is always asked about: dropping the
    /// folder that holds every series made one card named after it before this existed.
    static void reserveTheDropped(const Manifest::Node &folder, Shape::Suggestion &said)
    {
        if (!folder.at.isEmpty())
            return;
        said.confidence = Shape::Confidence::ToConfirm;
        said.clues << Shape::Clue{false, Words::clueDropped()};
    }

    Shape::Suggestion suggestion(const Manifest::Node &folder, Shape::Confidence confidence,
                                 QList<Role> readings)
    {
        Shape::Suggestion said;
        said.at = folder.at;
        said.confidence = confidence;
        for (const Role role : readings)
            said.readings << reading(folder, role);
        return said;
    }

    /// Archives sitting in the folder: a series, the scan's own rule, so nothing to confirm —
    /// but « Vrac » is no title, and a series called « Vrac » is one nobody finds again.
    Shape::Suggestion evidentWork(const Manifest::Node &folder)
    {
        Shape::Suggestion said = suggestion(folder, Shape::Confidence::Certain, {Role::Work});
        const QList<double> numbers = numbersIn(folder);
        said.clues << Shape::Clue{true, numbers.size() >= 2
                                            ? Words::clueArchivesHere(numbers.constFirst(),
                                                                      numbers.constLast())
                                            : Words::clueArchivesHere()};
        if (folder.children.isEmpty())
            said.clues << Shape::Clue{true, Words::clueNoSubfolder()};
        said.titleMissing = titleMissing(folder.name);
        if (said.titleMissing)
            said.clues << Shape::Clue{false, Words::clueNameToGive(folder.name)};
        return said;
    }

    /// The clues settle editions against series; then the name and the count of the series
    /// settle what holds them, a universe or a shelf. The alternative is the neighbour on
    /// editions — universe — shelf.
    Free contest(const Manifest::Node &folder)
    {
        const Points points = pointsOf(folder);
        // An edition holds archives and no folder: a subfolder with folders of its own can
        // only be a series, whatever the points say.
        const bool editionsFit = std::ranges::all_of(
            folder.children, [](const Manifest::Node &child) { return depthOf(child) == 0; });
        const Role holder = seriesHolder(folder);
        // What says shelf says it whatever the numbers: « Mangas/{Bleach, Naruto} », both 1
        // to 3, and a « BD » of a thousand series of 1 to 50 were each read as one work.
        const bool editions =
            editionsFit && points.editions > points.series && holder != Role::Shelf;

        Free out;
        out.suggests = true;
        if (editions) {
            // Always « à confirmer »: two series taken for two editions offer each other in
            // the edition switcher afterwards, the worse of the two mistakes `layout.rs` names.
            out.role = Role::Work;
            out.said = suggestion(folder, Shape::Confidence::ToConfirm, {Role::Work, holder});
            out.said.titleMissing = titleMissing(folder.name);
        } else {
            out.role = holder;
            // « Probable » only when every clue points one way; no clue at all is no way.
            const bool oneWay = points.series > 0 && points.editions == 0;
            const Role universeOther = editionsFit ? Role::Work : Role::Shelf;
            const Role other = holder == Role::Universe ? universeOther : Role::Universe;
            out.said = suggestion(folder,
                                  oneWay ? Shape::Confidence::Likely : Shape::Confidence::ToConfirm,
                                  {holder, other});
        }
        for (const Indicator &one : points.said)
            out.said.clues << Shape::Clue{one.forEditions == editions, one.text};
        if (editions && std::ranges::none_of(points.said, [](const Indicator &one) {
                return !one.forEditions;
            })) {
            out.said.clues << Shape::Clue{false, Words::clueProperNames()};
        }
        if (out.role == Role::Shelf)
            addShelfClues(folder, out.said);
        if (out.role == Role::Universe)
            reserveTheDropped(folder, out.said);
        return out;
    }

    /// A folder whose children are series, some with editions of their own: there is no
    /// contest left, only what holds them.
    ///
    /// « Probable » only on a clue of its own, and never surer than the series it holds: a
    /// universe of guessed series was « probable » with nothing under « Pourquoi », on top of a
    /// series still « à confirmer ».
    Free holderOfSeries(const Manifest::Node &folder)
    {
        using enum Shape::Role;
        Free out;
        out.suggests = true;
        out.role = seriesHolder(folder);
        const Role other = out.role == Universe ? Shelf : Universe;
        out.said = suggestion(folder, Shape::Confidence::ToConfirm, {out.role, other});
        if (out.role == Shelf)
            addShelfClues(folder, out.said);
        Shape::Confidence confidence = std::ranges::any_of(out.said.clues, &Shape::Clue::forIt)
                                           ? Shape::Confidence::Likely
                                           : Shape::Confidence::ToConfirm;
        for (const Manifest::Node &child : folder.children) {
            if (child.declared)
                continue;
            if (const Free read = alone(child); read.suggests)
                confidence = std::max(confidence, read.said.confidence);
        }
        out.said.confidence = confidence;
        if (out.role == Universe)
            reserveTheDropped(folder, out.said);
        return out;
    }

    /// A shelf nothing else can be: what it holds is no series. Certain, as the scan would
    /// read it, and with one reading.
    Free forcedShelf(const Manifest::Node &folder, const QString &universe)
    {
        Free out;
        out.suggests = true;
        out.universeBelow = universe;
        out.said = suggestion(folder, Shape::Confidence::Certain, {Role::Shelf});
        out.said.clues << Shape::Clue{true, universe.isEmpty()
                                                ? Words::clueTooDeep()
                                                : Words::clueHoldsUniverse(universe)};
        return out;
    }

    Free allDeclared(const Manifest::Node &folder)
    {
        Free out;
        out.suggests = true;
        out.said = suggestion(folder, Shape::Confidence::Certain, {Role::Shelf});
        out.said.clues << Shape::Clue{true, Words::clueAllDeclared()};
        return out;
    }

    Free seriesOfDeclaredEditions(const Manifest::Node &folder)
    {
        Free out;
        out.suggests = true;
        out.role = Role::Work;
        out.said = suggestion(folder, Shape::Confidence::Certain, {Role::Work});
        out.said.clues << Shape::Clue{true, Words::clueEditionsDeclared()};
        out.said.titleMissing = titleMissing(folder.name);
        return out;
    }

    QString universeBelow(const Manifest::Node &folder)
    {
        for (const Manifest::Node &child : folder.children) {
            const Role role = roleIn(child, Within::Nothing);
            if (role == Role::Universe)
                return child.name;
            if (role == Role::Shelf && !child.declared) {
                if (const QString below = alone(child).universeBelow; !below.isEmpty())
                    return below;
            }
        }
        return {};
    }

    Free decideFree(const Manifest::Node &folder)
    {
        if (!folder.files.isEmpty())
            return {Role::Work, true, evidentWork(folder), {}};
        // An empty drop, or a node `Manifest::whole` kept for being the root: nothing to read.
        if (folder.children.isEmpty())
            return {};

        if (const QString universe = universeBelow(folder); !universe.isEmpty())
            return forcedShelf(folder, universe);
        // Every subfolder declares a series or a universe: the scan walks this one through and
        // records nothing. Guessed a universe, an untouched, fully declared library would have
        // had a `universe.json` written into it that nobody asked for.
        if (std::ranges::all_of(folder.children, [](const Manifest::Node &child) {
                return child.declared
                       && (child.level == Manifest::Level::Work
                           || child.level == Manifest::Level::Universe);
            }))
            return allDeclared(folder);
        // Every subfolder declares an edition. `Manifest::found` makes each one a card of its
        // own, and an edition with no series is one the scan files nowhere — the shelf this
        // read as before said « tout ce qu’il contient se déclare déjà » over two orphans. The
        // one reading that files them is this folder as their series: certain, since the
        // editions say it, and confirming it writes the `work.json` they were missing.
        if (std::ranges::all_of(folder.children, [](const Manifest::Node &child) {
                return child.declared && child.level == Manifest::Level::Edition;
            }))
            return seriesOfDeclaredEditions(folder);
        // A universe holds series, a series editions, an edition archives: two folders below a
        // universe, then files. Past that it is deeper than any role, whatever the folders are
        // called — `> 3` let a universe sit one level too deep, since a folder's own archives
        // are at depth 0.
        if (depthOf(folder) > 2)
            return forcedShelf(folder, {});

        if (const bool flat =
                std::ranges::all_of(folder.children, [](const Manifest::Node &child) {
                    return !child.declared && !child.files.isEmpty();
                });
            flat)
            return contest(folder);
        if (std::ranges::all_of(folder.children, [this](const Manifest::Node &child) {
                return roleIn(child, Within::Nothing) == Role::Work;
            }))
            return holderOfSeries(folder);
        // A shelf or a declared edition among what it holds: a universe holds series only.
        Free out = forcedShelf(folder, {});
        for (const Manifest::Node &child : folder.children) {
            if (!child.declared && roleIn(child, Within::Nothing) == Role::Shelf) {
                out.said.clues = {Shape::Clue{true, Words::clueHoldsAShelf(child.name)}};
                break;
            }
        }
        return out;
    }

    /// Under a universe, said or guessed, a folder is a series and nothing else — no universe
    /// inside a universe — so it is offered one reading: a series and its editions.
    Shape::Suggestion inUniverse(const Manifest::Node &folder)
    {
        if (!folder.files.isEmpty())
            return evidentWork(folder);
        Shape::Suggestion said = suggestion(folder, Shape::Confidence::ToConfirm, {Role::Work});
        for (const Indicator &one : pointsOf(folder).said)
            said.clues << Shape::Clue{one.forEditions, one.text};
        said.clues << Shape::Clue{false, Words::clueNoUniverseInUniverse()};
        said.titleMissing = titleMissing(folder.name);
        return said;
    }

    void place(const Manifest::Node &folder, Within where)
    {
        using enum Shape::Origin;
        const Role role = roleIn(folder, where);
        Shape::Origin origin = Guessed;
        if (folder.declared)
            origin = Said;
        else if (role == Role::Work && !folder.files.isEmpty())
            origin = Evident;
        m_drop.roles.insert(folder.at, {role, origin});

        if (!folder.declared && where == Within::Nothing) {
            if (const Free read = alone(folder); read.suggests)
                m_drop.suggestions.insert(folder.at, read.said);
        } else if (!folder.declared && where == Within::Universe && role == Role::Work) {
            m_drop.suggestions.insert(folder.at, inUniverse(folder));
        }

        // At a universe's root, an archive without `oneshot.json` is one the scan reads
        // nobody into; `Facts` corrects it to a book when the archive says so.
        const Shape::Choice file = filesUnread(role)
                                       ? Shape::Choice{Role::Unread, Guessed}
                                       : Shape::Choice{Role::Volume, Evident};
        for (const Manifest::Entry &one : folder.files)
            m_drop.roles.insert(one.path, file);
        const Within below = within(role, where);
        for (const Manifest::Node &child : folder.children)
            place(child, below);
    }
};

// ——— What a role can be where it stands, and what choosing one reads again ————————————

/// The menu's order on a folder. Livre seul comes last and is always greyed: shown there, the
/// menu on a folder and on an archive is visibly the same menu.
constexpr std::array FolderRoles{Role::Work, Role::Edition, Role::Universe, Role::Shelf,
                                 Role::Book};
/// The folder roles an archive's menu lists greyed, after its own.
constexpr std::array NotForArchives{Role::Work, Role::Universe, Role::Shelf, Role::Edition};

/// Where `at` sits: its folder (an archive's own folder when `at` is an archive) and the
/// folders above that one, root first.
struct Place {
    const Manifest::Node *folder = nullptr;
    const Manifest::Entry *file = nullptr;
    QList<const Manifest::Node *> above;
};

/// The folder of `whole` at `at`, by the drop's index of places, made the first time — and
/// the folders above it into `above` when asked: a card is a few steps down from the root,
/// and finding it costs those steps and not a walk.
const Manifest::Node *folderOf(const Shape::Drop &drop, const QString &at,
                               QList<const Manifest::Node *> *above)
{
    if (drop.places.isEmpty()) {
        QList<std::pair<const Manifest::Node *, QList<qsizetype>>> waiting{{&drop.whole, {}}};
        while (!waiting.isEmpty()) {
            auto [folder, place] = waiting.takeLast();
            ++spent().looks;
            for (qsizetype child = 0; child < folder->children.size(); ++child)
                waiting.append({&folder->children.at(child), place + QList<qsizetype>{child}});
            drop.places.insert(folder->at, place);
        }
    }
    const auto place = drop.places.constFind(at);
    if (place == drop.places.cend())
        return nullptr;
    const Manifest::Node *folder = &drop.whole;
    for (const qsizetype child : *place) {
        ++spent().looks;
        if (child >= folder->children.size())
            return nullptr;
        if (above)
            *above << folder;
        folder = &folder->children.at(child);
    }
    return folder->at == at ? folder : nullptr;
}

/// A folder by the index; an archive by the folder its path names and its files. Each
/// folder's line in the tree located itself by comparing every folder beside it on the way
/// down, a thousand series on one shelf a thousand times.
std::optional<Place> placeOf(const Shape::Drop &drop, const QString &at)
{
    Place place;
    if ((place.folder = folderOf(drop, at, &place.above)))
        return place;
    place.above.clear();
    const qsizetype slash = at.lastIndexOf(u'/');
    place.folder = folderOf(drop, slash < 0 ? QString() : at.left(slash), &place.above);
    if (!place.folder)
        return std::nullopt;
    for (const Manifest::Entry &one : place.folder->files) {
        ++spent().looks;
        if (one.path == at) {
            place.file = &one;
            return place;
        }
    }
    return std::nullopt;
}

/// Where a folder's own role is read, from the roles above it as they are now.
Within contextOf(const QList<const Manifest::Node *> &above, const Shape::Roles &roles)
{
    Within where = Within::Nothing;
    for (const Manifest::Node *folder : above)
        where = within(roles.value(folder->at).role, where);
    return where;
}

bool anyBelow(const Manifest::Node &folder, const Shape::Roles &roles,
              const std::function<bool(const Shape::Choice &)> &test)
{
    return std::ranges::any_of(folder.children, [&](const Manifest::Node &child) {
        return test(roles.value(child.at)) || anyBelow(child, roles, test);
    });
}

/// Under an edition, under a series without an archive of its own, or under a universe with
/// no archive within a series and its editions: the scan has stopped before. The role
/// `Reader` gives such a folder is Shelf — not a shelf anybody made, the one role that says
/// « nothing here is read » — so Shelf is all its menu offers,
/// and every other role is greyed for the depth. Offering the series' roles instead would
/// let somebody choose one the scan then never reads.
bool outOfTheModel(const Manifest::Node &folder, Within where)
{
    using enum Within;
    return where == Beyond || (where == Work && folder.files.isEmpty())
           || (where == Universe && nearestOf(folder) > 1);
}

/// Why Universe does not fit an undeclared folder where it stands, the first reason that
/// holds; empty when it fits.
QString universeReason(const Manifest::Node &folder, Within where, const Shape::Roles &roles)
{
    if (folder.children.isEmpty())
        return Words::impossibleNoSubfolder();
    // `holds_archives_within(folder, MODEL_DEPTH)`: the scan reads nothing of a universe
    // whose nearest archive is deeper, and says so in `disregarded`.
    if (nearestOf(folder) >= ModelDepth)
        return Words::impossibleFilesTooDeepForAUniverse();
    if (where == Within::Universe)
        return Words::impossibleUniverseInUniverse();
    if (where == Within::Work)
        return Words::impossibleUniverseInSeries();
    if (anyBelow(folder, roles,
                 [](const Shape::Choice &one) { return one.role == Role::Universe; }))
        return Words::impossibleUniverseInUniverse();
    return {};
}

/// Why `role` does not fit an undeclared folder where it stands, the first reason that
/// holds; empty when it fits.
QString folderReason(const Manifest::Node &folder, Role role, Within where,
                     const Shape::Roles &roles)
{
    if (role == Role::Book)
        return Words::impossibleBookIsAnArchive();
    if (outOfTheModel(folder, where))
        return role == Role::Shelf ? QString() : Words::impossibleBeyondTheModel();
    switch (role) {
    case Role::Work:
        if (where == Within::Work)
            return Words::impossibleSeriesInSeries();
        // A series holds archives here or one folder down: `layout::holds_archives`.
        if (nearestOf(folder) >= ModelDepth - 1)
            return Words::impossibleFilesTooDeep();
        return {};
    case Role::Edition:
        return where == Within::Work ? QString() : Words::impossibleEditionAlone();
    case Role::Universe:
        return universeReason(folder, where, roles);
    case Role::Shelf:
        if (!folder.files.isEmpty())
            return Words::impossibleHoldsFiles();
        if (where == Within::Universe)
            return Words::impossibleFolderInUniverse();
        if (where == Within::Work)
            return Words::impossibleShelfInSeries();
        return {};
    case Role::Volume:
    case Role::Book:
    case Role::Unread:
        break;
    }
    return Words::impossibleNotAFolder();
}

/// A declared level where the roles above leave it no room. Universe or series under a
/// universe or a series is what `coherent_declarations` refuses on the server; a declared
/// edition with no series over it is this client's own rule, which the server accepts (it
/// cannot tell whether the series is already in the library) — but a role chosen here that
/// takes the series from over an edition makes it one the scan files nowhere, so it is
/// greyed too. Only where the model puts the folder, never its contents — a declaration says
/// what the folder is, whatever it holds.
QString contextReason(Role role, Within where)
{
    switch (role) {
    case Role::Universe:
        if (where == Within::Universe)
            return Words::impossibleUniverseInUniverse();
        if (where == Within::Work)
            return Words::impossibleUniverseInSeries();
        break;
    case Role::Work:
        if (where == Within::Work)
            return Words::impossibleSeriesInSeries();
        break;
    case Role::Edition:
        return where == Within::Work ? QString() : Words::impossibleEditionAlone();
    case Role::Shelf:
    case Role::Volume:
    case Role::Book:
    case Role::Unread:
        return {};
    }
    return where == Within::Beyond ? Words::impossibleBeyondTheModel() : QString();
}

QString sidecarOf(Manifest::Level level)
{
    using enum Manifest::Level;
    switch (level) {
    case Universe:
        return u"universe.json"_s;
    case Edition:
        return u"edition.json"_s;
    case Work:
    case Chapter:
    case Volume:
    case Book:
        break;
    }
    return u"work.json"_s;
}

/// An archive's menu, from its folder's role.
QList<Shape::Option> fileOptions(Role folder)
{
    QList<Shape::Option> out;
    const auto with = [&out](Role role, const QString &reason) {
        out << Shape::Option{role, reason.isEmpty(),
                             reason.isEmpty() ? Words::roleDetail(role) : reason};
    };
    if (folder == Role::Universe) {
        with(Role::Book, {});
        // Filing it in a series moves it into that series' folder: not this step.
        with(Role::Volume, Words::impossibleFilingMoves());
        with(Role::Unread, {});
    } else if (folder == Role::Work || folder == Role::Edition) {
        // A book can sit in a series too: the scan lifts it out (`visit_edition`).
        with(Role::Volume, {});
        with(Role::Book, {});
    } else {
        // Under a shelf nothing is read — `filesUnread` — so « Non lu » is the one role
        // there, offered so that the menu shows the role the archive has.
        with(Role::Volume, Words::impossibleBeyondTheModel());
        with(Role::Book, Words::impossibleBeyondTheModel());
        with(Role::Unread, {});
    }
    for (const Role role : NotForArchives)
        with(role, Words::impossibleNotAFolder());
    return out;
}

bool offers(const QList<Shape::Option> &options, Role role)
{
    return std::ranges::any_of(options, [role](const Shape::Option &one) {
        return one.role == role && one.possible;
    });
}

/// What a folder becomes under a new parent when nothing kept its role: the rule `Reader`
/// reads it with, so a change above lands where reading the drop that way would have.
///
/// Guessed, and Evident only for archives under no universe or series — the scan's own rule.
/// Dragon Ball's `Perfect` holds archives too, but is a series only because Dragon Ball was
/// made a universe; made a series again, it is an edition.
Shape::Choice reread(Reader &reader, const Manifest::Node &folder, Within where,
                     const Shape::Roles &roles)
{
    using enum Shape::Origin;
    switch (where) {
    case Within::Universe:
        return {nearestOf(folder) > 1 ? Role::Shelf : Role::Work, Guessed};
    case Within::Work:
        return {folder.files.isEmpty() ? Role::Shelf : Role::Edition, Guessed};
    case Within::Beyond:
        return {Role::Shelf, Guessed};
    case Within::Nothing:
        break;
    }
    if (!folder.files.isEmpty())
        return {Role::Work, Evident};
    // An edition somebody chose needs a series over it, and the free reading cannot know it
    // was chosen: under a shelf again, a series read as a universe had its chosen edition
    // read back into a series.
    if (std::ranges::any_of(folder.children, [&roles](const Manifest::Node &child) {
            const Shape::Choice one = roles.value(child.at);
            return one.role == Role::Edition && one.origin == Chosen;
        })
        && folderReason(folder, Role::Work, Within::Nothing, roles).isEmpty())
        return {Role::Work, Guessed};
    const Role free = reader.freeRole(folder);
    // The free reading is made from the disk alone, and knows nothing of a universe chosen
    // below: a universe guessed over one somebody chose would undo their choice.
    if (free == Role::Universe && anyBelow(folder, roles, [](const Shape::Choice &one) {
            return one.role == Role::Universe && one.origin == Chosen;
        }))
        return {Role::Shelf, Guessed};
    return {free, Guessed};
}

/// Reads again everything under `folder`, whose own role is already set. A role declared is
/// kept; a role chosen is kept while it still fits; the rest follows the new parent.
void rereadBelow(Reader &reader, const Manifest::Node &folder, Within where,
                 Shape::Roles &roles)
{
    using enum Shape::Origin;
    const Role role = roles.value(folder.at).role;
    const QList<Shape::Option> forFiles = fileOptions(role);
    for (const Manifest::Entry &file : folder.files) {
        if (const Shape::Choice now = roles.value(file.path);
            now.origin == Chosen && offers(forFiles, now.role))
            continue;
        roles.insert(file.path, filesUnread(role)
                                    ? Shape::Choice{Role::Unread, Guessed}
                                    : Shape::Choice{Role::Volume, Evident});
    }
    const Within below = within(role, where);
    for (const Manifest::Node &child : folder.children) {
        const Shape::Choice now = roles.value(child.at);
        if (const bool kept = child.declared
                              || (now.origin == Chosen
                                  && folderReason(child, now.role, below, roles).isEmpty());
            !kept)
            roles.insert(child.at, reread(reader, child, below, roles));
        rereadBelow(reader, child, below, roles);
    }
}

/// Every declared folder under `folder`, with where the roles put it.
void declaredUnder(const Manifest::Node &folder, Within where, const Shape::Roles &roles,
                   QHash<QString, Within> &into)
{
    const Within below = within(roles.value(folder.at).role, where);
    for (const Manifest::Node &child : folder.children) {
        if (child.declared)
            into.insert(child.at, below);
        declaredUnder(child, below, roles, into);
    }
}

bool declaresBelow(const Manifest::Node &folder)
{
    return std::ranges::any_of(folder.children, [](const Manifest::Node &child) {
        return child.declared || declaresBelow(child);
    });
}

const Manifest::Node *nodeAt(const Manifest::Node &folder, const QString &at)
{
    ++spent().looks;
    if (folder.at == at)
        return &folder;
    for (const Manifest::Node &child : folder.children) {
        if (const Manifest::Node *found = nodeAt(child, at))
            return found;
    }
    return nullptr;
}

/// `folderOf` without the folders above.
const Manifest::Node *folderOf(const Shape::Drop &drop, const QString &at)
{
    return folderOf(drop, at, nullptr);
}

/// Gives the folder `place` its role and reads again what is under it — on `drop`, unchecked.
void setFolder(Shape::Drop &drop, const Place &place, Role role)
{
    drop.roles.insert(place.folder->at, {role, Shape::Origin::Chosen});
    Reader reader(drop);
    rereadBelow(reader, *place.folder, contextOf(place.above, drop.roles), drop.roles);
}

/// Makes again the suggestions of the folders above the one changed, from the roles as they
/// now stand: they were made from the disk alone, and the shelf over Dragon Ball made a
/// universe went on counting it a series of two editions, and the one over Terres d’Arran
/// made a shelf went on naming it a universe as its reason. Only the suggestions — not a
/// role, and not the suggestion of the folder changed, whose readings are what was chosen
/// among.
void suggestAbove(Shape::Drop &drop, const Place &place)
{
    Reader reader(drop, true);
    for (qsizetype at = place.above.size() - 1; at >= 0; --at)
        reader.suggestAgain(*place.above.at(at), contextOf(place.above.first(at), drop.roles));
}

/// The declared folder below that `role` would leave with no room — the server refuses the
/// whole import for it, so the role is greyed here and names the folder that says otherwise.
/// Only a contradiction this role makes: a declaration already out of place is refused by
/// the server whatever role is chosen above it, so it stops its card (`outOfPlace`) instead
/// of greying every role it would make no better.
QString clashBelow(const Shape::Drop &drop, const Place &place, Role role)
{
    if (!declaresBelow(*place.folder))
        return {};
    // A copy of the whole drop per role tried, counted as one look: what a menu costs.
    ++spent().looks;
    Shape::Drop trial = drop;
    setFolder(trial, place, role);
    const Within where = contextOf(place.above, drop.roles);
    QHash<QString, Within> before;
    QHash<QString, Within> after;
    declaredUnder(*place.folder, where, drop.roles, before);
    declaredUnder(*place.folder, where, trial.roles, after);
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        const Manifest::Node *folder = nodeAt(*place.folder, it.key());
        const Role declared = roleOf(folder->level);
        if (contextReason(declared, before.value(it.key())).isEmpty()
            && !contextReason(declared, *it).isEmpty())
            return Words::impossibleDeclaredBelow(folder->name, sidecarOf(folder->level));
    }
    return {};
}

QString rebased(const QString &at, const QString &card)
{
    if (card.isEmpty())
        return at;
    return at == card ? QString() : at.mid(card.size() + 1);
}

Manifest::Node rebasedNode(const Manifest::Node &folder, const QString &card,
                           const Shape::Roles &roles)
{
    Manifest::Node out = folder;
    out.at = rebased(folder.at, card);
    switch (roles.value(folder.at).role) {
    case Role::Universe:
        out.level = Manifest::Level::Universe;
        break;
    case Role::Work:
        out.level = Manifest::Level::Work;
        break;
    case Role::Edition:
        out.level = Manifest::Level::Edition;
        break;
    case Role::Shelf:
    case Role::Volume:
    case Role::Book:
    case Role::Unread:
        // A folder out of the model has no level to be at: it keeps the node's, and its
        // role in `rolesOf` says that nothing in it is read.
        break;
    }
    for (Manifest::Entry &file : out.files)
        file.path = rebased(file.path, card);
    for (Manifest::Node &child : out.children)
        child = rebasedNode(child, card, roles);
    return out;
}

} // namespace

namespace Shape {

QString folded(const QString &text)
{
    return ::folded(text);
}

bool isEditionWord(const QString &folderName)
{
    const QStringList words = wordsOf(folderName);
    return std::ranges::any_of(words,
                               [](const QString &word) { return editionWords().contains(word); });
}

bool isGenericName(const QString &folderName)
{
    const QString name = withoutNumbers(folderName);
    return !name.isEmpty() && genericNames().contains(name);
}

void suggest(Drop &drop)
{
    drop.roles.clear();
    drop.suggestions.clear();
    Reader(drop).run();
}

Drop read(const QString &path)
{
    Drop drop;
    drop.path = path;
    drop.whole = Manifest::whole(path);
    suggest(drop);
    return drop;
}

QList<Option> options(const Drop &drop, const QString &at)
{
    const std::optional<Place> place = placeOf(drop, at);
    if (!place)
        return {};
    if (place->file)
        return fileOptions(drop.roles.value(place->folder->at).role);

    const Manifest::Node &folder = *place->folder;
    QList<Option> out;
    for (const Role role : FolderRoles) {
        QString reason;
        if (folder.declared) {
            if (role != roleOf(folder.level))
                reason = Words::saidBy(sidecarOf(folder.level));
        } else {
            reason = folderReason(folder, role, contextOf(place->above, drop.roles),
                                  drop.roles);
            if (reason.isEmpty())
                reason = clashBelow(drop, *place, role);
        }
        QString detail = reason.isEmpty() ? Words::roleDetail(role) : reason;
        // Out of the model, Étagère is no shelf anybody made: its line says nothing is read.
        if (reason.isEmpty() && role == Role::Shelf && !folder.declared
            && outOfTheModel(folder, contextOf(place->above, drop.roles)))
            detail = Words::roleBeyondTheModel();
        out << Option{role, reason.isEmpty(), detail};
    }
    return out;
}

bool apply(Drop &drop, const QString &at, Role role)
{
    if (!offers(options(drop, at), role))
        return false;
    const Place place = *placeOf(drop, at);
    if (place.file) {
        drop.roles.insert(place.file->path, {role, Origin::Chosen});
        return true;
    }
    // Its sidecar says it, and choosing what it already says changes nothing: marked Chosen,
    // a declared folder would be one the import thinks it has to confirm.
    if (place.folder->declared)
        return true;
    setFolder(drop, place, role);
    suggestAbove(drop, place);
    return true;
}

bool applyReading(Drop &drop, const QString &at, int index)
{
    const auto said = drop.suggestions.constFind(at);
    if (said == drop.suggestions.cend() || index < 0 || index >= said->readings.size())
        return false;
    const Reading &reading = said->readings.at(index);
    // A reading is about the whole folder: what was chosen below gives way to it, and the
    // rule that made the reading then reads each folder the way the reading did.
    Drop trial = drop;
    for (auto it = reading.roles.cbegin(); it != reading.roles.cend(); ++it) {
        if (it.key() != at && trial.roles.value(it.key()).origin == Origin::Chosen)
            trial.roles[it.key()].origin = Origin::Guessed;
    }
    if (!apply(trial, at, reading.roles.value(at)))
        return false;
    drop = std::move(trial);
    return true;
}

QStringList cards(const Drop &drop)
{
    using enum Role;
    QStringList out;
    QList waiting{&drop.whole};
    while (!waiting.isEmpty()) {
        const Manifest::Node *folder = waiting.takeFirst();
        // An edition with nothing above it is one only a sidecar makes — the menu never
        // offers it there — and `found` gives it a card of its own, so this does too.
        if (const Role role = drop.roles.value(folder->at).role;
            role == Universe || role == Work || role == Edition) {
            out << folder->at;
            continue;
        }
        for (const Manifest::Node &child : folder->children)
            waiting << &child;
    }
    return out;
}

Manifest::Node cardNode(const Drop &drop, const QString &card)
{
    const Manifest::Node *folder = folderOf(drop, card);
    return folder ? rebasedNode(*folder, card, drop.roles) : Manifest::Node{};
}

Cost cost()
{
    return spent();
}

namespace {

/// Why a folder declaring `inner` cannot stand under the nearest declared one, `outer`;
/// empty when it can.
QString declaredInside(Role outer, Role inner)
{
    using enum Role;
    if (outer == Edition)
        return Words::impossibleInsideAnEdition();
    if (outer == Work && inner == Universe)
        return Words::impossibleUniverseInSeries();
    if (outer == Work && inner == Work)
        return Words::impossibleSeriesInSeries();
    if (outer == Universe && inner == Universe)
        return Words::impossibleUniverseInUniverse();
    return {};
}

} // namespace

QString outOfPlace(const Drop &drop, const QString &card)
{
    const Manifest::Node *folder = folderOf(drop, card);
    if (!folder)
        return {};
    // The server's own table (`coherent_declarations`), and on what it reads it on: each
    // declaration against the nearest declared folder above it, whatever declares nothing
    // in between. Read through the roles instead, a universe holding `Cycle 1/` and its
    // declared series was stopped here for a series in a series, and sent by the server.
    // A card sits under shelves only, so nothing above its root is declared.
    QList<std::pair<const Manifest::Node *, std::optional<Role>>> waiting{
        {folder, folder->declared ? std::optional(roleOf(folder->level)) : std::nullopt}};
    while (!waiting.isEmpty()) {
        const auto [one, outer] = waiting.takeFirst();
        ++spent().looks;
        for (const Manifest::Node &child : one->children) {
            const std::optional<Role> inner =
                child.declared ? std::optional(roleOf(child.level)) : std::nullopt;
            if (const QString wrong = outer.has_value() && inner.has_value()
                                          ? declaredInside(*outer, *inner)
                                          : QString();
                !wrong.isEmpty())
                return Words::declaredOutOfPlace(child.name, sidecarOf(child.level), wrong);
            waiting.append({&child, inner.has_value() ? inner : outer});
        }
    }
    return {};
}

Roles rolesOf(const Drop &drop, const QString &card)
{
    // The roles of what the card holds, looked up from its own tree: walking every role of
    // the drop for each card read a thousand universes' 53 001 roles a thousand times.
    Roles out;
    const Manifest::Node *folder = folderOf(drop, card);
    if (!folder)
        return out;
    QList waiting{folder};
    while (!waiting.isEmpty()) {
        const Manifest::Node *one = waiting.takeLast();
        ++spent().looks;
        if (const auto role = drop.roles.constFind(one->at); role != drop.roles.cend())
            out.insert(rebased(one->at, card), *role);
        for (const Manifest::Entry &file : one->files) {
            ++spent().looks;
            if (const auto role = drop.roles.constFind(file.path); role != drop.roles.cend())
                out.insert(rebased(file.path, card), *role);
        }
        for (const Manifest::Node &child : one->children)
            waiting << &child;
    }
    return out;
}

} // namespace Shape
