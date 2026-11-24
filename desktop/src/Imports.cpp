#include "Imports.h"

#include "Api.h"
#include "Card.h"
#include "Carrier.h"
#include "Manifest.h"
#include "Words.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>

#include <algorithm>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// The folders of the card rooted at `within` whose suggestion says their name is no title,
/// by their `at` in the card.
QSet<QString> untitledOf(const Shape::Drop &drop, const QString &within)
{
    QSet<QString> out;
    for (auto it = drop.suggestions.cbegin(); it != drop.suggestions.cend(); ++it) {
        if (!it->titleMissing)
            continue;
        if (within.isEmpty())
            out.insert(it.key());
        else if (it.key() == within)
            out.insert(QString());
        else if (it.key().size() > within.size() && it.key().at(within.size()) == u'/'
                 && it.key().startsWith(within))
            out.insert(it.key().mid(within.size() + 1));
    }
    return out;
}

/// Whether two cards' roles say the same thing. `Shape::Choice` has no `==` of its own, and
/// comparing the role alone would miss a guess turned choice — which writes a sidecar.
bool sameRoles(const Shape::Roles &one, const Shape::Roles &other)
{
    if (one.size() != other.size())
        return false;
    for (auto it = one.cbegin(); it != one.cend(); ++it) {
        const auto there = other.constFind(it.key());
        if (there == other.cend() || there->role != it->role || there->origin != it->origin)
            return false;
    }
    return true;
}

Imports::Cost &spent()
{
    static Imports::Cost total;
    return total;
}

} // namespace

Imports *Imports::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — nothing can be "
                              "imported");
    }
    return new Imports(server);
}

Imports::Imports(Server *server, QObject *parent)
    : ImportRows(server, parent)
{
    // Here and not in `ImportRows`: the carrier is made for an `Imports`, which is the type
    // that corrects the cards it carries.
    m_carrier = new Carrier(this, server);
}

bool Imports::setRole(quint64 drop, const QString &at, Shape::Role role)
{
    return reshape(drop,
                   [&at, role](Shape::Drop &trial) { return Shape::apply(trial, at, role); });
}

bool Imports::setReading(quint64 drop, const QString &at, int index)
{
    return reshape(drop,
                   [&at, index](Shape::Drop &trial) {
                       return Shape::applyReading(trial, at, index);
                   });
}

Imports::Cost Imports::cost()
{
    return spent();
}

bool Imports::roleWaits(quint64 drop, const QString &at, Shape::Role role) const
{
    // A role chosen at `at` changes the card holding it or the cards under it, and nothing
    // else: when every one of those can be corrected — the common case — nothing waits, and
    // no copy of the drop is made to find it out.
    if (const auto touched =
            [&at](const QString &within) {
                return at.isEmpty() || within.isEmpty() || within == at
                       || (within.size() > at.size() && within.at(at.size()) == u'/'
                           && within.startsWith(at))
                       || (at.size() > within.size() && at.at(within.size()) == u'/'
                           && at.startsWith(within));
            };
        std::ranges::all_of(m_rows, [&](const Card &card) {
            return !card.folder || card.road.drop != drop || !touched(card.road.within)
                   || Carrier::correctable(card);
        }))
        return false;
    Shape::Drop trial;
    QList<int> reread;
    QList<int> gone;
    return !tried(drop, [&at, role](Shape::Drop &one) { return Shape::apply(one, at, role); },
                  trial, reread, gone)
           && !std::ranges::all_of(reread + gone, [this](int row) {
                  return Carrier::correctable(m_rows.at(row));
              });
}

template <typename Change>
bool Imports::tried(quint64 token, const Change &change, Shape::Drop &trial, QList<int> &reread,
                    QList<int> &gone) const
{
    const auto kept = std::ranges::find(m_drops, token, &Shape::Drop::token);
    if (token == 0 || kept == m_drops.cend())
        return false;
    // Tried on a copy: refused by `Shape`, or by a card that already went, the drop must be
    // exactly what it was — a role half applied is a menu that says one thing and a card
    // that announces another.
    ++spent().trials;
    trial = *kept;
    if (!change(trial))
        return false;

    const QStringList listed = Shape::cards(trial);
    const QSet<QString> after(listed.cbegin(), listed.cend());
    for (int row = 0; row < m_rows.size(); ++row) {
        const Card &card = m_rows.at(row);
        if (!card.folder || card.road.drop != token)
            continue;
        if (!after.contains(card.road.within))
            gone << row;
        else if (!sameRoles(card.road.roles, Shape::rolesOf(trial, card.road.within)))
            reread << row;
    }
    return std::ranges::all_of(reread + gone,
                               [this](int row) { return Carrier::correctable(m_rows.at(row)); });
}

bool Imports::reshape(quint64 token, const std::function<bool(Shape::Drop &)> &change)
{
    using enum Stage;
    Shape::Drop trial;
    QList<int> reread;
    QList<int> gone;
    if (!tried(token, change, trial, reread, gone))
        return false;
    const auto kept = std::ranges::find(m_drops, token, &Shape::Drop::token);
    const QStringList before = Shape::cards(*kept);
    const QStringList after = Shape::cards(trial);
    *kept = std::move(trial);

    // Inside a card: the walk already made holds every archive, so the sheets are read again
    // from it and nothing touches the disk. `cardNode` is made again first — the level, the
    // sidecar's name and the fields come from the node, and the old node read with the new
    // roles mixes two readings. Edited, so « Importer » drops the session and announces the
    // card again, which is how a correction already travels.
    //
    // The drop is looked up again after every call that emits: a slot answering `changed`
    // may change the queue, and a reference held across it may point at nothing.
    for (const int row : reread) {
        const Shape::Drop *drop = dropOf(token);
        if (!drop || row >= m_rows.size())
            break;
        Card &card = m_rows[row];
        card.road.node = Shape::cardNode(*drop, card.road.within);
        card.road.roles = Shape::rolesOf(*drop, card.road.within);
        card.road.untitled = untitledOf(*drop, card.road.within);
        Carrier::readSheets(card);
        card.road.edited = true;
        card.road.confirmed = false;
        if (card.stage == Reviewing)
            m_carrier->announce(row);
        else
            m_carrier->settle(row, Reviewing);
    }

    // The cards themselves change — a shelf turned universe is one card where there were two.
    // Stitching two walks and two sessions into one would carry ticks given against answers
    // that no longer exist, and corrections typed on sheets that are now another card's: the
    // cards touched are abandoned, their sessions dropped, and the new ones read as a drop
    // is, one walk each. Abandoned before the new ones are added, so a card leaving does not
    // stand in the way of the one that replaces it under the rule of one name, one place.
    for (qsizetype at = gone.size() - 1; at >= 0; --at)
        m_carrier->abandon(gone.at(at));
    bool added = false;
    for (const QString &within : after) {
        if (before.contains(within))
            continue;
        // A copy: `addCard` may abandon a failed row, which emits.
        const Shape::Drop *drop = dropOf(token);
        if (!drop)
            break;
        added = addCard(Shape::Drop(*drop), within) || added;
    }
    if (added)
        m_carrier->describeNext();
    forgetDropsWithoutCards();
    // A row reread or abandoned already said so; the cards added have not, and a change that
    // touched no card — a folder that is no card's — still changes what the frame draws.
    if (added || (reread.isEmpty() && gone.isEmpty()))
        emit changed();
    return true;
}

bool Imports::edit(quint64 token, const QString &at, const QString &key, const QJsonValue &value)
{
    return edit(QList<Edit>{{token, at, key, value}}) == 1;
}

int Imports::edit(const QList<Edit> &edits)
{
    // One batch and not one call per correction: a direction given to twenty-four series
    // was twenty-four `changed`, and each one rebuilt the checking frame and composed every
    // card again — twenty-four times what one gesture needed.
    m_quiet = true;
    int taken = 0;
    for (const Edit &one : edits)
        taken += editOne(one.token, one.at, one.key, one.value) ? 1 : 0;
    m_quiet = false;
    if (taken > 0) {
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0), index(int(m_rows.size()) - 1));
        }
        emit changed();
    }
    return taken;
}

bool Imports::editOne(quint64 token, const QString &at, const QString &key,
                      const QJsonValue &value)
{
    using enum Stage;
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        // Once the bytes are on their way a correction could only reach the library by a
        // second upload nobody asked for, so it is refused rather than kept doing nothing.
        // Ready again after a transfer that failed half-way counts as gone, which the stage
        // alone did not say: its bytes are already on the server.
        if (!Carrier::correctable(card))
            return false;
        if (!Facts::edit(card.road.sheets, at, key, value))
            return false;
        // The last value per field, kept for a role change to put back (`readSheets`).
        card.road.corrections.removeIf([&at, &key](const Card::Road::Correction &one) {
            return one.at == at && one.key == key;
        });
        card.road.corrections.append({at, key, value});
        card.road.edited = true;
        // Corrected is no longer confirmed: what « Importer » agreed to is not this.
        card.road.confirmed = false;
        if (card.stage == Reviewing)
            m_carrier->announce(row);
        else
            m_carrier->settle(row, Reviewing);
        return true;
    }
    return false;
}

bool Imports::setResetting(quint64 token, const QString &path, bool reset)
{
    for (Card &card : m_rows) {
        if (card.token != token)
            continue;
        if (!Carrier::correctable(card))
            return false;
        if (reset)
            card.road.resetting.insert(path);
        else
            card.road.resetting.remove(path);
        return true;
    }
    return false;
}

bool Imports::keep(quint64 token, const QString &sidecar, const QString &key, bool keep)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        if (!Carrier::correctable(card) || !card.road.held.contains(sidecar))
            return false;
        const QString one = sidecar + u'\n' + key;
        if (keep == card.road.keeping.contains(one))
            return false;
        if (keep)
            card.road.keeping.insert(one);
        else
            card.road.keeping.remove(one);
        card.road.edited = true;
        card.road.confirmed = false;
        if (card.stage == Stage::Reviewing)
            m_carrier->announce(row);
        else
            m_carrier->settle(row, Stage::Reviewing);
        return true;
    }
    return false;
}

bool Imports::setSplice(quint64 token, int from, int to)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        if (!Carrier::correctable(card) || !card.road.target
            || card.road.target->kind != u"CHAPTER"_s || from < 0 || to < from)
            return false;
        if (card.road.splice && card.road.splice->from == from && card.road.splice->to == to)
            return false;
        card.road.splice = Card::Road::Splice{from, to};
        card.road.edited = true;
        card.road.confirmed = false;
        if (card.stage == Stage::Reviewing)
            m_carrier->announce(row);
        else
            m_carrier->settle(row, Stage::Reviewing);
        return true;
    }
    return false;
}

bool Imports::setEditionName(quint64 token, const QString &path, const QString &name)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        if (card.road.editionNames.value(path) == name)
            return false;
        // While the answer is awaited the name is kept, and the answer's arrival announces
        // the folder again with it (`Carrier::tookFolder`).
        if (card.stage == Stage::Asking) {
            card.road.editionNames.insert(path, name);
            if (!card.road.editionOrder.contains(path))
                card.road.editionOrder << path;
            emit changed();
            return true;
        }
        if (!Carrier::correctable(card))
            return false;
        card.road.editionNames.insert(path, name);
        if (!card.road.editionOrder.contains(path))
            card.road.editionOrder << path;
        card.road.edited = true;
        card.road.confirmed = false;
        m_carrier->reannounce(row);
        return true;
    }
    return false;
}

bool Imports::setCover(quint64 token, const QString &at, const QString &source)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        if (source.isEmpty() ? !card.road.picked.contains(at)
                             : !QFileInfo(source).isFile())
            return false;
        const bool waiting = card.stage == Card::Stage::Asking;
        if (!waiting && !Carrier::correctable(card))
            return false;
        if (source.isEmpty()) {
            card.road.picked.remove(at);
        } else {
            Card::CoverPick pick;
            pick.source = source;
            pick.entry.path = (at.isEmpty() ? QString() : at + u'/') + u"cover."_s
                              + QFileInfo(source).suffix().toLower();
            pick.entry.size = QFileInfo(source).size();
            pick.entry.checksum = Manifest::checksumOf(source);
            card.road.picked.insert(at, pick);
        }
        // While the answer is awaited it arrives and announces the folder again
        // (`Carrier::tookFolder`), as for a name typed meanwhile.
        if (waiting) {
            emit changed();
            return true;
        }
        card.road.edited = true;
        card.road.confirmed = false;
        m_carrier->reannounce(row);
        return true;
    }
    return false;
}

bool Imports::askAgain(quint64 token)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).token != token)
            continue;
        if (!Carrier::correctable(m_rows.at(row)))
            return false;
        m_carrier->reannounce(row);
        return true;
    }
    return false;
}

bool Imports::chooseSecond(quint64 token, const QString &at)
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).token != token || !Carrier::correctable(m_rows.at(row)))
            continue;
        m_rows[row].road.second.insert(at);
        m_carrier->announce(row);
        return true;
    }
    return false;
}

bool Imports::retarget(quint64 token, const QString &at, const QString &kind, const QString &id)
{
    if (!at.isEmpty())
        return false;
    for (int row = 0; row < m_rows.size(); ++row) {
        Card &card = m_rows[row];
        if (card.token != token)
            continue;
        // A card that aimed at nothing may be aimed at a series it resembles, nothing else.
        if (!Carrier::correctable(card) || card.id.isEmpty()
            || (!card.road.target && kind != u"SERIES"_s && kind != u"SERIES_NEW_EDITIONS"_s))
            return false;
        Card::Road &road = card.road;
        QString name = id;
        for (const Api::Resemblance &one : road.resembles) {
            if (one.seriesId == id)
                name = one.name;
        }
        road.target = Card::Road::Target{kind, id, name};
        // Everything said about the old target is about the old target: a splice, a place
        // started again, a value kept, what was ticked.
        road.splice.reset();
        road.resetting.clear();
        road.keeping.clear();
        road.held.clear();
        road.confirmed = false;
        m_carrier->reannounce(row);
        return true;
    }
    return false;
}

bool Imports::addCard(const Shape::Drop &drop, const QString &within)
{
    const QString at = within.isEmpty() ? drop.path : drop.path + u'/' + within;
    // A failed row at that path is no card reading it: a regroup whose old sessions the
    // server would not drop leaves them there, and taken for « already a card » the series a
    // second regroup brings back vanished without a word. It is abandoned — its cleanup
    // asked again — and the folder read afresh.
    for (int row = m_carrier->rowOf(at); row >= 0; row = m_carrier->rowOf(at)) {
        if (m_rows.at(row).stage != Stage::Failed)
            return false;
        m_carrier->abandon(row);
    }
    const Manifest::Node node = Shape::cardNode(drop, within);

    // Two folders of one name in a single drop land in the same place: `root` is the
    // folder's name and the server installs into `library/<root>`. Traced on a shelf holding
    // `Death Note/` and `Vieux/Death Note/` — two cards, one destination, and the second
    // would send over what the first had just filed, without a word. `root` is read from the
    // folder's own name — the same computation `Manifest::of` makes for `tree.root` — so a
    // row not yet announced still names accurately where it would land.
    const QString destination = QFileInfo(at).fileName();
    if (const auto collidesWith =
            std::ranges::find_if(m_rows, [&destination](const Card &one) {
                return one.folder && QFileInfo(one.path).fileName() == destination;
            });
        collidesWith != m_rows.cend()) {
        const QString there = QDir(drop.path).relativeFilePath(collidesWith->path);
        m_carrier->appendRow(at, node.name, true, node.size);
        const int row = int(m_rows.size()) - 1;
        // Posed all the same rather than dropped: hiding the second card would look like a
        // drop that missed something. Marked described so `describeNext` never queues it —
        // a refused card has nothing to read — and no card of the drop: a role changed there
        // must not wait on a card that can never be corrected.
        m_rows[row].road.described = true;
        const QString why = Words::sameDestination(QDir(drop.path).relativeFilePath(at), there);
        // And given a sheet, the series its folder's name says, holding the reason: the frame
        // draws sheets, and a card refused before any walk has none — it showed nothing of
        // the second folder, which then never went without a word.
        m_rows[row].road.node = node;
        Facts::Sheet sheet;
        sheet.level = Manifest::Level::Work;
        sheet.name = node.name;
        sheet.sidecar = u"work.json"_s;
        sheet.trouble = why;
        m_rows[row].road.sheets = {sheet};
        m_carrier->settle(row, Stage::Failed, why);
        return false;
    }

    // The server refuses a declaration out of place at the announcement, in a sentence of
    // its own: the card was proposed ready and failed there. Stopped here, like the
    // collision above, and for the same reasons: nothing to read, no role that would mend it.
    if (const QString wrong = Shape::outOfPlace(drop, within); !wrong.isEmpty()) {
        m_carrier->appendRow(at, node.name, true, node.size);
        const int row = int(m_rows.size()) - 1;
        m_rows[row].road.described = true;
        // A sheet too, for the collision's reason, and as what the folder is read as and
        // nothing more: it may be a universe, and called a series it said a second thing
        // that was not so. No sidecar of its own: nothing of a refused card is written.
        const Shape::Choice choice = Shape::rolesOf(drop, within).value(QString());
        Facts::Sheet sheet;
        sheet.level = Manifest::Level::Work;
        if (choice.role == Shape::Role::Universe)
            sheet.level = Manifest::Level::Universe;
        else if (choice.role == Shape::Role::Edition)
            sheet.level = Manifest::Level::Edition;
        sheet.role = choice.role;
        sheet.roleOrigin = choice.origin;
        sheet.name = node.name;
        sheet.trouble = wrong;
        m_rows[row].road.node = node;
        m_rows[row].road.sheets = {sheet};
        m_carrier->settle(row, Stage::Failed, wrong);
        return false;
    }

    m_carrier->appendRow(at, node.name, true, node.size);
    Card::Road &road = m_rows.last().road;
    road.node = node;
    road.roles = Shape::rolesOf(drop, within);
    road.drop = drop.token;
    road.within = within;
    road.untitled = untitledOf(drop, within);
    return true;
}

void Imports::setFiling(int row, const QString &workId, bool filing)
{
    if (row < 0 || row >= m_rows.size())
        return;
    // Once it is moving the answer is no longer a question: the commit that carries it has
    // either gone or is about to. A drop aimed at an universe is answered where it stops,
    // at « Reviewing », as the switches of a series are.
    if (const Stage stage = m_rows.at(row).stage;
        stage != Stage::Deciding && stage != Stage::Ready
        && !(m_rows.at(row).road.target && stage == Stage::Reviewing)) {
        return;
    }
    if (filing)
        m_rows[row].road.filing.insert(workId);
    else
        m_rows[row].road.filing.remove(workId);
    // A move matched by title is made by the identity the declaration it travels with
    // carries (`keptIn`), and the server read that declaration when it was announced.
    if (const bool byTitle = std::ranges::any_of(
            m_rows.at(row).road.moves, [&workId](const Api::Relocation &one) {
                return one.workId == workId && one.matched == u"TITLE"_s;
            });
        byTitle && m_rows.at(row).road.target) {
        m_carrier->reannounce(row);
        return;
    }
    m_carrier->announce(row);
}

void Imports::setReplacing(int row, const QString &path, bool replacing)
{
    if (row < 0 || row >= m_rows.size())
        return;
    // The same window `setFiling` has, for the same reason: once the commit that carries the
    // answer has gone, the answer is not a question any more.
    // A drop aimed at something already held is answered while it waits to be looked at, and
    // its switches change what the commit carries, not what the server was asked: the card
    // stopped at « Reviewing » would have been refused here, and the answer asked again for
    // nothing the switch could change.
    const bool aimed = m_rows.at(row).road.target.has_value();
    if (const Stage stage = m_rows.at(row).stage;
        stage != Stage::Deciding && stage != Stage::Ready && !(aimed && stage == Stage::Reviewing)) {
        return;
    }
    if (replacing)
        m_rows[row].road.replacing.insert(path);
    else
        m_rows[row].road.replacing.remove(path);
    if (!aimed)
        m_carrier->announce(row);
}

void Imports::setDeclaring(int row, const QString &path, bool declaring)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (const Stage stage = m_rows.at(row).stage;
        stage != Stage::Deciding && stage != Stage::Ready) {
        return;
    }
    if (declaring)
        m_rows[row].road.declaring.insert(path);
    else
        m_rows[row].road.declaring.remove(path);
    m_carrier->announce(row);
}

bool Imports::ticked(int row, Choice what, const QString &key) const
{
    using enum Choice;
    if (row < 0 || row >= m_rows.size())
        return false;
    const Card::Road &road = m_rows.at(row).road;
    switch (what) {
    case Filing:
        return road.filing.contains(key);
    case Replacing:
        return road.replacing.contains(key);
    case Declaring:
        return road.declaring.contains(key);
    }
    return false;
}

void Imports::toggle(int row, const QString &at)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (m_rows.at(row).road.open.contains(at))
        m_rows[row].road.open.remove(at);
    else
        m_rows[row].road.open.insert(at);
    m_carrier->announce(row);
}

void Imports::accept(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).folder)
        return;
    m_carrier->settle(row, Stage::Ready);
}

void Imports::decide(int row, const QString &seriesId)
{
    if (row < 0 || row >= m_rows.size() || seriesId.isEmpty())
        return;
    m_rows[row].chosen = seriesId;
    m_carrier->settle(row, Stage::Ready);
}

void Imports::abandon(int row)
{
    m_carrier->abandon(row);
    forgetDropsWithoutCards();
}
