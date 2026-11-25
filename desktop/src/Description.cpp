#include "Description.h"

#include "Fold.h"
#include "Series.h"
#include "Words.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

/// One `{label, value}` pair, or nothing when the value is not recorded. « Collection : — »
/// is a line that says a fact is missing; leaving it out says the same and takes no room.
void pair(QVariantList &into, Words::Fact which, const QString &value)
{
    if (value.trimmed().isEmpty())
        return;
    into.append(QVariantMap{{u"label"_s, Words::fact(which)}, {u"value"_s, value}});
}

/// The same, for the one line that is painted in the alert colour rather than in ink.
void alarming(QVariantList &into, const QString &label, const QString &value)
{
    if (value.isEmpty())
        return;
    into.append(QVariantMap{{u"label"_s, label}, {u"value"_s, value}, {u"alarming"_s, true}});
}

QVariantList credits(const Api::Series &one, const QStringList &writers,
                     const QStringList &drawers)
{
    using enum Words::Fact;
    QVariantList said;
    // One person who writes and draws is one « Auteur », not the same name under « Scénario »
    // and again under « Dessin ». Three names a line, else two and the others counted: Sept
    // listed fourteen writers on one line of the description.
    for (const Words::CreditLine &line : Words::creditLines(writers, drawers))
        said.append(QVariantMap{{u"label"_s, line.label},
                                {u"value"_s, Words::namesWithin(line.names, 3)},
                                {u"linked"_s, Words::namesWithin(line.names, 3, u"people"_s)}});
    pair(said, Publisher, one.publication.publisher.value_or(QString()));
    pair(said, Collection, one.publication.collection.value_or(QString()));
    if (const auto &tag = one.publication.language; tag && !tag->isEmpty())
        pair(said, Language, Words::language(*tag));
    return said;
}

QVariantList nature(const Api::Series &one)
{
    using enum Words::Fact;
    QVariantList said;
    if (one.medium.has_value())
        pair(said, Kind, Words::medium(*one.medium));
    if (one.readingDirection.has_value())
        pair(said, Direction, Words::readingDirection(*one.readingDirection));
    if (one.run.has_value()) {
        QString said_ =
            Words::editionStatus(Api::spell(*one.run));
        // What the publisher announces, beside the state of the run — the two are one line
        // because « en cours » alone leaves « of how many? » unanswered.
        if (const auto declared = one.publication.declaredVolumes; declared.has_value())
            said_ = Words::runAnnounced(said_, *declared, one.medium);
        pair(said, Status, said_);
    }
    pair(said, Age, one.ageRating.value_or(QString()));
    if (one.colour.has_value())
        pair(said, Colour, Words::colour(*one.colour));
    return said;
}

QVariantList holding(const Api::Series &one)
{
    using enum Words::Fact;
    QVariantList said;
    const Api::Holding &has = one.holding;
    const int ceiling = one.publication.declaredVolumes.value_or(0);
    pair(said, Held, Words::heldOutOf(has.ownedVolumes, ceiling));
    // The only place that says a volume is missing, and the only line painted in alert.
    alarming(said, Words::missingLabel(int(has.missingVolumes.size())),
             Words::missingVolumes(has.missingVolumes));
    if (has.readEntries > 0 || has.partRead > 0.0) {
        const bool open = has.partRead > 0.0;
        pair(said, Read,
             Words::readEntries(has.readEntries, open, has.readEntries + 1));
    }
    return said;
}

QVariantList received(const Api::Series &one)
{
    using enum Words::Fact;
    QVariantList said;
    const Api::Holding &has = one.holding;
    if (has.addedAt.has_value())
        pair(said, FirstReceived, Words::moment(*has.addedAt));
    if (has.lastAddedAt.has_value())
        pair(said, LastReceived, Words::moment(*has.lastAddedAt));
    return said;
}

/// Who wrote and drew each of `volumes`, and in how many of them, through `person`, which
/// finds or adds the one a name means.
template <typename Find>
void countVolumes(const QList<Api::Entry> &volumes, const Find &person)
{
    for (const Api::Entry &volume : volumes) {
        QSet<QString> here;
        const auto credit = [&](const QString &name, bool wrote) {
            auto &one = person(name);
            (wrote ? one.wrote : one.drew) = true;
            if (!here.contains(Fold::searchKey(name))) {
                here.insert(Fold::searchKey(name));
                ++one.volumes;
            }
        };
        for (const QString &name : volume.credits.authors)
            credit(name, true);
        for (const QString &name : volume.credits.artists)
            credit(name, false);
    }
}

} // namespace

Description::Description(Series *of)
    : QObject(of)
    , m_of(of)
{
    // Everything here is read off the page's answer, so it changed whenever the page did.
    connect(of, &Series::changed, this, &Description::changed);
}

const std::optional<Api::Series> &Description::series() const
{
    return m_of->m_one;
}

QString Description::makers() const
{
    return series() ? Words::makers(*series(), namesOf(true, true)) : QString();
}

QString Description::makersLinked() const
{
    return series() ? Words::makers(*series(), namesOf(true, true), u"people"_s) : QString();
}

QList<Description::Person> Description::peopleOrdered() const
{
    if (!series())
        return {};
    QList<Person> out;
    QHash<QString, qsizetype> at;
    int met = 0;
    const auto person = [&out, &at, &met](const QString &name) -> Person & {
        const QString key = Fold::searchKey(name);
        if (!at.contains(key)) {
            at.insert(key, out.size());
            out << Person{name, 0, false, met++, false, false};
        }
        return out[at.value(key)];
    };
    if (m_volumesOf == series()->id && !m_volumes.isEmpty())
        countVolumes(m_volumes, person);
    // Whoever the series is credited with, counted or not: the volumes are one edition's, and
    // somebody credited only in the other vanished from the page once they had answered.
    QStringList writers = series()->credits.authors;
    if (writers.isEmpty() && series()->credits.author)
        writers << *series()->credits.author;
    for (const QString &name : writers)
        person(name).wrote = true;
    for (const QString &name : series()->credits.artists)
        person(name).drew = true;
    for (const QString &name : series()->declared.authors + series()->declared.artists)
        if (at.contains(Fold::searchKey(name)))
            out[at.value(Fold::searchKey(name))].declared = true;
    std::ranges::stable_sort(out, [](const Person &one, const Person &other) {
        return std::tuple(!one.declared, -one.volumes, one.firstRead)
               < std::tuple(!other.declared, -other.volumes, other.firstRead);
    });
    return out;
}

QStringList Description::namesOf(bool wrote, bool drew) const
{
    QStringList out;
    for (const Person &one : peopleOrdered())
        if ((wrote && one.wrote) || (drew && one.drew))
            out << one.name;
    return out;
}

QVariantList Description::people() const
{
    if (!series())
        return {};
    QHash<QString, int> volumes;
    for (const Person &one : peopleOrdered())
        volumes.insert(Fold::searchKey(one.name), one.volumes);
    QVariantList out;
    for (const Words::CreditLine &line :
         Words::creditLines(namesOf(true, false), namesOf(false, true))) {
        // « Auteur » for the section even when several share it: it is a heading, not a name.
        const QString section = line.label == u"Auteurs"_s ? u"Auteur"_s : line.label;
        for (const QString &name : line.names) {
            const int count = volumes.value(Fold::searchKey(name));
            out << QVariantMap{{u"section"_s, section},
                               {u"name"_s, name},
                               {u"count"_s, count > 0 ? Words::volumes(count, series()->medium)
                                                      : QString()}};
        }
    }
    return out;
}

QString Description::peopleTitle() const
{
    return series() ? Words::peopleTitle(series()->work) : QString();
}

QString Description::peopleCount() const
{
    return series() ? Words::peopleCount(int(peopleOrdered().size()), series()->medium) : QString();
}

void Description::takeVolumes(const QString &seriesId, const QList<Api::Entry> &volumes)
{
    if (seriesId.isEmpty() || seriesId != m_of->m_id)
        return;
    m_volumesOf = seriesId;
    m_volumes = volumes;
    // The page's own signal and not this object's: the credits column of `facts` moves
    // with the count, and `Series::changed` is what tells this object too.
    emit m_of->changed();
}

QString Description::weights() const
{
    return series() ? Words::weights(*series()) : QString();
}

QStringList Description::genres() const
{
    if (!series())
        return {};
    // Genres and tags, in that order and never folded together: measured, a work carries one
    // genre and seven tags with nothing in common between the two lists.
    QStringList said = series()->genres;
    said += series()->tags;
    return said;
}

QString Description::summary() const
{
    return series() && series()->summary ? *series()->summary : QString();
}

QVariantMap Description::facts() const
{
    if (!series())
        return {};
    return {{u"credits"_s, credits(*series(), namesOf(true, false), namesOf(false, true))},
            {u"nature"_s, nature(*series())},
            {u"holding"_s, holding(*series())},
            {u"received"_s, received(*series())}};
}

