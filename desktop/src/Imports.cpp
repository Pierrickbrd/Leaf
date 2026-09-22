#include "Imports.h"

#include "Card.h"
#include "Carrier.h"
#include "Manifest.h"
#include "Words.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace {

/// `at`, stripped of `prefix` and the separator after it.
QString unprefixed(QString at, const QString &prefix)
{
    if (prefix.isEmpty())
        return at;
    at.remove(0, prefix.size());
    if (at.startsWith(u'/'))
        at.remove(0, 1);
    return at;
}

/// Rewrites a node's own `at` to `""` and every descendant's `at` — folder or file — to
/// match, so a card born on a shelf folds and unfolds its own root exactly like one
/// dropped directly.
///
/// `Manifest::found` roots every node at the folder that was queried, on purpose: a shelf
/// holding two series has to tell them apart on disk, and rooting each at itself made both
/// answer `.at == ""` and nothing said which folder was which. Right for `offer`, wrong for
/// a card's own tree — `toggle` asks for `""` to mean "this card's root", and a card is a
/// shelf's *child*, not the shelf.
void rebase(Manifest::Node &node, const QString &prefix)
{
    node.at = unprefixed(node.at, prefix);
    for (Manifest::Node &child : node.children)
        rebase(child, prefix);
    for (Manifest::Entry &file : node.files)
        file.path = unprefixed(file.path, prefix);
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
    : QAbstractListModel(parent)
    , m_carrier(new Carrier(this, server))
{
}

int Imports::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> Imports::roleNames() const
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
    };
}

QVariant Imports::data(const QModelIndex &index, int role) const
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
    }
    return {};
}

int Imports::inFlight() const
{
    int total = 0;
    for (const Card &one : m_rows) {
        if (one.stage != Stage::Filed && one.stage != Stage::Failed)
            ++total;
    }
    return total;
}

int Imports::deciding() const
{
    int total = 0;
    for (const Card &one : m_rows) {
        if (one.stage == Stage::Deciding)
            ++total;
    }
    return total;
}

int Imports::reading() const
{
    // A folder queued behind another is not yet "being looked at" — it is waiting its turn,
    // which `Carrier::describeNext` serialises on purpose. Counting it here would make the
    // button wait on folders nobody has started walking, for as long as the queue is deep.
    int total = m_carrier->walkingAFolder() ? 1 : 0;
    for (const Card &one : m_rows) {
        if (one.stage != Stage::Asking)
            continue;
        // A file always counts: each one is a live request of its own. A folder counts once
        // its walk is done and it is only the server's answer being waited on — before that
        // it is either the one `m_describing` already counted, or still queued.
        if (!one.folder || one.road.described)
            ++total;
    }
    return total;
}

bool Imports::busy() const
{
    return m_carrier->holding() >= 0;
}







void Imports::offer(const QStringList &paths)
{
    // The row already claiming a destination, or -1. `root` is read from the folder's own
    // name — the same computation `Manifest::of` makes for `tree.root` — so a row not yet
    // announced still names accurately where it would land.
    auto folderNamed = [this](const QString &destination) {
        for (int row = 0; row < m_rows.size(); ++row) {
            if (m_rows.at(row).folder
                && QFileInfo(m_rows.at(row).path).fileName() == destination)
                return row;
        }
        return -1;
    };

    for (const QString &path : paths) {
        const QFileInfo about(path);
        if (about.isFile()) {
            if (m_carrier->rowOf(path) >= 0)
                continue;
            m_carrier->appendRow(path, about.fileName(), false, about.size());
            m_carrier->ask(int(m_rows.size()) - 1);
            continue;
        }
        if (!about.isDir())
            continue;
        // One card per declared thing, and the shelves walked through rather than kept.
        // Measured: dropping the folder that holds every series rendered one card named
        // after it — and the server installs into `library/<that name>`, so accepting
        // that screen would have put the whole library inside a folder of itself.
        for (Manifest::Node node : Manifest::found(path)) {
            const QString at = node.at.isEmpty() ? path : path + u'/' + node.at;
            if (m_carrier->rowOf(at) >= 0)
                continue;

            // Two folders of one name in a single drop land in the same place: `root` is
            // the folder's name and the server installs into `library/<root>`. Traced on a
            // shelf holding `Death Note/` and `Vieux/Death Note/` — two cards, one
            // destination, and the second would send over what the first had just filed,
            // without a word.
            const QString destination = QFileInfo(at).fileName();
            if (const int collidesWith = folderNamed(destination); collidesWith >= 0) {
                m_carrier->appendRow(at, node.name, true, node.size);
                const int row = int(m_rows.size()) - 1;
                // Posed all the same rather than dropped: hiding the second card would
                // look like a drop that missed something. Marked described so
                // `describeNext` never queues it — a refused card has nothing to read.
                m_rows[row].road.described = true;
                const QString here = QDir(path).relativeFilePath(at);
                const QString there = QDir(path).relativeFilePath(m_rows.at(collidesWith).path);
                m_carrier->settle(row, Stage::Failed, Words::sameDestination(here, there));
                continue;
            }

            // A copy, not a reference to `node.at` itself: `rebase` overwrites that very
            // field as it goes, and a prefix aliasing the value it strips would come back
            // empty after the first line and leave every descendant unstripped.
            const QString prefix = node.at;
            rebase(node, prefix);
            m_carrier->appendRow(at, node.name, true, node.size);
            m_rows.last().road.node = node;
        }
    }
    m_carrier->describeNext();
    emit changed();
}








void Imports::setFiling(int row, const QString &workId, bool filing)
{
    if (row < 0 || row >= m_rows.size())
        return;
    // Once it is moving the answer is no longer a question: the commit that carries it has
    // either gone or is about to.
    if (const Stage stage = m_rows.at(row).stage;
        stage != Stage::Deciding && stage != Stage::Ready) {
        return;
    }
    if (filing)
        m_rows[row].road.filing.insert(workId);
    else
        m_rows[row].road.filing.remove(workId);
    m_carrier->announce(row);
}

void Imports::setReplacing(int row, const QString &path, bool replacing)
{
    if (row < 0 || row >= m_rows.size())
        return;
    // The same window `setFiling` has, for the same reason: once the commit that carries the
    // answer has gone, the answer is not a question any more.
    if (const Stage stage = m_rows.at(row).stage;
        stage != Stage::Deciding && stage != Stage::Ready) {
        return;
    }
    if (replacing)
        m_rows[row].road.replacing.insert(path);
    else
        m_rows[row].road.replacing.remove(path);
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

void Imports::setVerifying(bool verifying)
{
    if (verifying == m_verifying)
        return;
    m_verifying = verifying;
    emit changed();
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

int Imports::offerUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &one : urls) {
        // A URL that is not a local file is not a file at all as far as this is concerned.
        if (!one.isLocalFile())
            continue;
        paths << one.toLocalFile();
    }
    const auto before = int(m_rows.size());
    offer(paths);
    return int(m_rows.size()) - before;
}

void Imports::giveUpPreparing()
{
    using enum Stage;
    for (int row = int(m_rows.size()) - 1; row >= 0; --row) {
        if (const Stage stage = m_rows.at(row).stage;
            stage == Asking || stage == Deciding || stage == Ready) {
            abandon(row);
        }
    }
}



void Imports::decide(int row, const QString &seriesId)
{
    if (row < 0 || row >= m_rows.size() || seriesId.isEmpty())
        return;
    m_rows[row].chosen = seriesId;
    m_carrier->settle(row, Stage::Ready);
}

void Imports::send()
{
    // What would do nothing goes first, and the server is told so its session is cleaned
    // up. Leaving them in a list of transfers is one more card to read for nothing, and
    // their inbox folders are the same leak abandoning one closes — measured on a real
    // server, twenty-six sessions nobody would ever claim.
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        if (m_rows.at(row).stage == Stage::Ready && m_rows.at(row).nothingToDo())
            abandon(row);
    }
    m_carrier->pump();
}










void Imports::pause(int row)
{
    using enum Stage;
    if (row < 0 || row >= m_rows.size())
        return;
    if (const Stage stage = m_rows.at(row).stage;
        stage != Sending && stage != Ready) {
        return;
    }

    m_carrier->settle(row, Paused);
    m_carrier->pump();
}

void Imports::resume(int row)
{
    using enum Stage;
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).stage != Paused)
        return;

    m_rows[row].retryIn = 0;
    m_rows[row].attempts = 0;
    // Takes the slot back, and whoever held it steps behind — "now it is that one that
    // waits". A transfer interrupted this way loses nothing: the server keeps what
    // arrived, and the next attempt starts past it.
    if (const int held = m_carrier->holding(); held >= 0)
        m_carrier->settle(held, Paused);
    m_carrier->settle(row, Ready);
    m_carrier->pump();
}

void Imports::abandon(int row)
{
    m_carrier->abandon(row);
}
