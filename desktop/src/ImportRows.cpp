#include "ImportRows.h"

#include "Card.h"
#include "Carrier.h"
#include "Words.h"

#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace Lines {

/// What one card would put on the wire: the files of a folder that the library does not
/// already hold, or the file itself. The total and the recap's line say the same thing
/// because they are the same sum.
int filesToSend(const Card &card)
{
    if (!card.folder)
        return 1;
    return int(std::ranges::count_if(card.road.tree.files, [&card](const auto &file) {
        return !card.road.alreadyThere.contains(file.path);
    }));
}

qint64 bytesToSend(const Card &card)
{
    if (!card.folder)
        return card.size;
    qint64 bytes = 0;
    for (const auto &file : card.road.tree.files) {
        if (!card.road.alreadyThere.contains(file.path))
            bytes += file.size;
    }
    return bytes;
}

} // namespace Lines

namespace {

/// Whether a role is still a guess of the shape. Chosen is not: a click answered it, and
/// `Facts::needsReview` no longer stops on it either.
bool guessed(const Facts::Sheet &sheet)
{
    return sheet.roleOrigin == Shape::Origin::Guessed;
}

/// How the sheets were read, as an `Origin`: Said when anything is, else Taken when
/// anything is, else Missing.
int readHow(const QList<Facts::Sheet> &sheets)
{
    using enum Facts::Origin;
    bool said = false;
    bool taken = false;
    for (const Facts::Sheet &sheet : sheets) {
        for (const Facts::Field &field : sheet.fields) {
            said = said || field.origin == Said;
            taken = taken || field.origin == Taken;
        }
    }
    if (said)
        return std::to_underlying(Said);
    return std::to_underlying(taken ? Taken : Missing);
}

/// What the third step's line says under the name, for the state the card is in: the reason
/// when it failed, the countdown when it waits to try again, and otherwise what it is, said
/// after the word that tells it is waiting or stopped.
QString aboutOf(const Card &card)
{
    using enum Card::Stage;
    if (card.stage == Failed)
        return card.trouble;
    if (card.stage == Paused && card.retryIn > 0)
        return Words::tryingAgainIn(card.retryIn);
    if (card.stage == Asking && card.road.confirmed)
        return Words::lineResetting();
    if (card.stage == Paused)
        return Words::linePaused(card.about());
    if (card.stage == Ready && card.road.released)
        return Words::lineQueued(card.about());
    return card.about();
}

} // namespace

ImportRows::ImportRows(Server *server, QObject *parent)
    : QAbstractListModel(parent)
    , m_server(server)
{
    m_left = m_file.value(u"imports/left"_s).toStringList();
    if (m_server && !m_left.isEmpty())
        m_server->whenLoaded(this, [this] { dropWhatWasLeft(); });
    // The walk tells the model of each volume it counts through `Checking` alone, on purpose
    // (see `Carrier::hashedSoFar`): the line's count and the drop's total are the same
    // number said in two other forms, and are told here, once, rather than by every place
    // that moves it. No roles at all means every one of them, this one included.
    // Whatever `changed` says, the counters that follow `progressed` have to hear too.
    connect(this, &ImportRows::changed, this, &ImportRows::progressed);
    connect(this, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                if (roles.isEmpty()) {
                    emit progressed();
                    return;
                }
                // What a chunk moved: the send's counters are sums over the lines, and told
                // by `changed` alone they stood still until a pause or a file ended — a
                // reader watching « 0 tome sur 76 » and « 156 Mio » that did not move.
                if (roles.contains(std::to_underlying(Role::Sent))) {
                    emit progressed();
                    return;
                }
                const int checking = std::to_underlying(Role::Checking);
                const int progress = std::to_underlying(Role::Progress);
                if (roles.contains(progress) || !roles.contains(checking))
                    return;
                emit dataChanged(first, last, {progress});
                emit progressed();
            });
}

QString ImportRows::kept(const QString &id) const
{
    return m_server->address() + u' ' + id;
}

void ImportRows::remember(const QString &id)
{
    if (!m_server || id.isEmpty() || m_server->address().isEmpty())
        return;
    const QString line = kept(id);
    if (m_left.contains(line))
        return;
    m_left.append(line);
    m_file.setValue(u"imports/left"_s, m_left);
    m_file.sync();
}

void ImportRows::forget(const QString &id)
{
    if (!m_server || id.isEmpty() || m_left.isEmpty())
        return;
    if (m_left.removeOne(kept(id))) {
        m_file.setValue(u"imports/left"_s, m_left);
        m_file.sync();
    }
}

void ImportRows::dropWhatWasLeft()
{
    const QString here = m_server->address();
    if (here.isEmpty())
        return;
    const QString prefix = here + u' ';
    QStringList mine;
    for (const QString &line : std::as_const(m_left)) {
        if (line.startsWith(prefix))
            mine << line.mid(prefix.size());
    }
    for (const QString &id : std::as_const(mine)) {
        // Forgotten only once the server has said it is not to be had: gone (success or 404),
        // or not ours to drop (403). A server that was off at launch (status 0), a profile to
        // choose (409), a throttle (429) or a failure of its own (5xx) says nothing of the
        // kind, and forgetting then left the empty session on the disk for good.
        m_server->remove(u"/import/"_s + id, this, [this, id](const Server::Answer &answer) {
            if (answer.went() || answer.status == 404 || answer.status == 403)
                forget(id);
        });
    }
}

int ImportRows::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> ImportRows::roleNames() const
{
    using enum Role;
    return {
        {std::to_underlying(Name), "name"},         {std::to_underlying(Stage_), "stage"},
        {std::to_underlying(Sent), "sent"},         {std::to_underlying(Size), "size"},
        {std::to_underlying(Reason), "reason"},     {std::to_underlying(Confidence), "confidence"},
        {std::to_underlying(Candidates), "candidates"}, {std::to_underlying(Concerns), "concerns"},
        {std::to_underlying(Chosen), "chosen"},     {std::to_underlying(Trouble), "trouble"},
        {std::to_underlying(RetryIn), "retryIn"},   {std::to_underlying(Folder), "folder"},
        {std::to_underlying(Creates), "creates"},   {std::to_underlying(Moves), "moves"},
        {std::to_underlying(Nodes), "nodes"},       {std::to_underlying(Checking), "checking"},
        {std::to_underlying(ReadHow), "readHow"},   {std::to_underlying(Progress), "progress"},
        {std::to_underlying(Volumes), "volumes"},   {std::to_underlying(Bytes), "bytes"},
        {std::to_underlying(About), "about"},       {std::to_underlying(Alarm), "alarm"},
        {std::to_underlying(Fraction), "fraction"}, {std::to_underlying(Released), "released"},
        {std::to_underlying(Shown), "shown"},
    };
}

QVariant ImportRows::data(const QModelIndex &index, int role) const
{
    using enum Role;
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Card &one = m_rows.at(index.row());

    switch (Role(role)) {
    case Name:
        return one.name;
    case Stage_:
        return QVariant::fromValue(one.stage);
    case Sent:
        return one.sent;
    case Size:
        return one.size;
    case Reason:
        return one.proposal ? one.proposal->reason : QString();
    case Confidence:
        return one.proposal ? std::to_underlying(one.proposal->confidence)
                            : std::to_underlying(Api::Proposal::Confidence::Other);
    case Candidates: {
        QVariantList all;
        if (one.proposal) {
            for (const Api::Candidate &candidate : one.proposal->candidates) {
                all << QVariantMap{{u"seriesId"_s, candidate.seriesId},
                                   {u"name"_s, candidate.name}};
            }
        }
        return all;
    }
    case Concerns:
        return one.proposal ? QVariant::fromValue(one.proposal->concerns) : QVariant();
    case Chosen:
        return one.chosen;
    case Trouble:
        return one.trouble;
    case RetryIn:
        return one.retryIn;
    case Folder:
        return one.folder;
    case Creates:
        return one.creates();
    case Moves:
        return one.moves();
    case Nodes:
        return one.nodes();
    case Checking:
        return one.checking(m_walkingToken);
    case Progress:
        return one.progress(m_walkingToken);
    case Volumes:
        return Lines::filesToSend(one);
    case Bytes:
        return Lines::bytesToSend(one);
    case About:
        return aboutOf(one);
    case Alarm:
        return one.stage == Stage::Failed;
    case Fraction:
        return one.fraction();
    case Released:
        return one.road.released;
    case Shown:
        return one.shown();
    case ReadHow: {
        // How the whole card was read, as the legend names it: something said by the
        // folder's own JSON, else something taken from a ComicInfo, else nothing — « Complet »,
        // « Komga », « Nu ». -1 while the sheets are not read yet.
        if (one.road.sheets.isEmpty())
            return -1;
        return readHow(one.road.sheets);
    }
    }
    return {};
}

int ImportRows::deciding() const
{
    int total = 0;
    for (const Card &one : m_rows) {
        if (one.stage == Stage::Deciding)
            ++total;
    }
    return total;
}

int ImportRows::reviewing() const
{
    return int(std::ranges::count_if(m_rows, [](const Card &one) {
        return one.stage == Stage::Reviewing;
    }));
}

int ImportRows::foldersRead() const
{
    return int(std::ranges::count_if(m_rows, [](const Card &one) {
        return one.folder && one.stage != Stage::Failed && one.road.described;
    }));
}

int ImportRows::foldersToRead() const
{
    // Queued or walked now alike: both are still to read, which is what the count says.
    return int(std::ranges::count_if(m_rows, [](const Card &one) {
        return one.folder && one.stage != Stage::Failed && !one.road.described;
    }));
}

int ImportRows::readings() const
{
    using enum Shape::Role;
    int total = 0;
    for (const Card &card : m_rows) {
        // Only what « Importer » can still confirm: a card let go, or gone, has nothing left
        // to be asked, and counting it kept « Confirmer » on the button after the press.
        if (!card.folder || !Carrier::correctable(card))
            continue;
        bool asks = false;
        for (const Facts::Sheet &sheet : card.road.sheets) {
            if (!Carrier::travels(card, sheet))
                continue;
            if (sheet.role == Volume || sheet.role == Book || sheet.role == Unread) {
                // Each archive at a universe's root is a reading of its own: one is a book,
                // the next one is read by nobody, and confirming one says nothing of the other.
                total += sheet.role != Volume && guessed(sheet) ? 1 : 0;
                continue;
            }
            // A series of the scan's own rule (`Evident`) asks nothing about its role — but
            // one called « Vrac » still has its name to be given, and that is what its chip
            // says (« Une série · nom à donner ») until a title is typed (`readSheets`).
            const bool nameToGive =
                card.road.untitled.contains(sheet.at)
                && std::ranges::any_of(sheet.fields, [](const Facts::Field &field) {
                       return field.key == u"title"_s && field.origin == Facts::Origin::Missing;
                   });
            asks = asks || guessed(sheet) || nameToGive;
        }
        total += asks ? 1 : 0;
    }
    return total;
}

const Shape::Drop *ImportRows::dropOf(quint64 token) const
{
    if (token == 0)
        return nullptr;
    const auto found = std::ranges::find(m_drops, token, &Shape::Drop::token);
    return found == m_drops.cend() ? nullptr : std::to_address(found);
}

void ImportRows::forgetDropsWithoutCards()
{
    m_drops.removeIf([this](const Shape::Drop &drop) {
        return std::ranges::none_of(
            m_rows, [&drop](const Card &card) { return card.road.drop == drop.token; });
    });
}

void ImportRows::setVerifying(bool verifying)
{
    if (verifying == m_verifying)
        return;
    m_verifying = verifying;
    emit changed();
}
