#include "Imports.h"

#include "Cbz.h"
#include "Manifest.h"
#include "Words.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QtConcurrentRun>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QQmlEngine>
#include <QUrlQuery>
#include <QVariantMap>

#include <array>

using namespace Qt::StringLiterals;

namespace {

/// How much of a file goes up in one request.
///
/// Small enough that a progress bar moves and a pause is felt within a second on a slow
/// line; large enough that a two hundred megabyte volume is fifty requests and not five
/// thousand. Nothing is lost when one fails: the next attempt resumes at the byte.
constexpr qint64 ChunkBytes = 4 * 1024 * 1024;

/// How long to wait before trying again, and it grows. The server's own `Retry-After` is
/// already honoured by `Server` for a 429; this is for the cut cable, which nobody times.
int waitFor(int attempts)
{
    static constexpr std::array Steps{2, 5, 10, 30, 60};
    const std::size_t at = qMin(std::size_t(attempts), Steps.size() - 1);
    return Steps[at];
}

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

/// What the server said about this folder, arranged by where things sit — so a node can say
/// what becomes of it without searching four lists for its own name.
///
/// By path and never by name: two editions of one work are told apart by where they sit, and
/// `creates[].at` is exactly that. Matching on the declared name would file the answer for
/// « Perfect Edition » onto whichever edition happened to be named the same.
struct Answered {
    QSet<QString> creates;
    QSet<QString> moves;
    /// The volumes still to go, the one in flight, and the ones behind it — derived from
    /// the row rather than stored twice, because `toSend` and `at` already say it.
    QStringList toSend;
    /// What would land on a file the library already holds, by path, with what is there —
    /// so a line can say what it would overwrite rather than only that it would.
    QHash<QString, Api::Replacement> replaces;
    /// Of those, the ones a reader has ticked.
    QSet<QString> replacing;
    /// What would be written over a declaration the library already holds, by the `at` of
    /// the node that declares it — `work.json` belongs to the folder holding it, not to a
    /// line of its own.
    QHash<QString, Api::Declaration> declarations;
    QSet<QString> declaring;
    /// The folder-level `at`s that hold at least one file the server already has — built by
    /// `containersOf` from `alreadyThere`, the one list that actually says "this exists".
    /// A container absent from `creates` and `moves` used to fall straight to "already in
    /// the library" on the strength of the server having answered at all — true for a
    /// series the server already knew, and false for a folder of archives with no sidecar,
    /// which `creates` never names because it is built from sidecars alone.
    QSet<QString> holdsAlready;
    int at = 0;
    bool moving = false;
    bool done = false;
    /// Set when the row itself is `Stage::Failed`. The row's own `trouble` already says why;
    /// without this the volume at `at` still read « à envoyer » at the exact moment somebody
    /// looked at the tree to see what had gone wrong — the node said something untrue right
    /// beside the sentence saying it was not.
    bool failed = false;
    /// Whether the server has answered at all. Before it has, `creates`, `moves` and
    /// `toSend` are all empty for every node — which would otherwise read as "already there"
    /// rather than as "nothing knows yet".
    bool announced = false;
};

/// Every folder-level `at` a file's path passes through, root (`""`) included — so a
/// container can be told "already there" only by something genuinely under it, rather than
/// by the mere fact that the server answered at all.
///
/// Built from `alreadyThere` rather than the other way round: `toSend` says what is new and
/// `alreadyThere` says what is not, but only a *file* answer either way — nothing in the
/// contract ever names a container as "already there" on its own, because the server does
/// not need to. A reader does, and this is what supplies it from what the files already say.
QSet<QString> containersOf(const QStringList &paths)
{
    QSet<QString> out;
    for (const QString &path : paths) {
        QString at;
        out.insert(at);
        const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
        // Every prefix but the file's own name: `Tome 1.cbz` alone is not a container, and
        // the loop below would otherwise credit its own path as one.
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            at = at.isEmpty() ? parts.at(i) : at + u'/' + parts.at(i);
            out.insert(at);
        }
    }
    return out;
}

/// What a node becomes, and how loudly to say it.
///
/// **Three tones and not seven colours.** `FilterChip` makes the argument this follows: if
/// « terminées » were always green, that green would mean nothing, and the one thing worth
/// seeing would become the harder one to see. So the tone here marks the exception — what is
/// about to be destroyed, and the one thing moving right now — and never the category. The
/// screen decides what each tone looks like; this decides which one a state has earned,
/// because deciding it in QML would mean comparing French strings there.
struct Said {
    QString text;
    Imports::Tone tone = Imports::Tone::Ordinary;
};

/// What one volume or chapter becomes, from the list the server asked for and how far the
/// queue behind it has got.
Said stateOfVolume(const QString &path, const Answered &answered)
{
    using enum Imports::Tone;

    if (!answered.announced)
        return {};
    const auto where = int(answered.toSend.indexOf(path));
    if (where < 0)
        return {Words::alreadyInTheLibrary(), Quiet};
    if (answered.done || where < answered.at)
        return {Words::wasFiled(Manifest::levelOf(path)), Ordinary};
    if (answered.failed && where == answered.at)
        return {Words::failedToSend(), Attention};
    if (answered.moving && where == answered.at)
        return {Words::beingSent(), Moving};
    // Last of the pending states, and the only one that warns rather than describes: the
    // three above it are about where this volume is in the queue, this one is about what
    // accepting costs.
    if (answered.replaces.contains(path))
        return {Words::willBeReplaced(Manifest::levelOf(path)), Attention};
    return {Words::willBeSent(), Ordinary};
}

/// How many of the files under this node would land on one the library already holds.
///
/// The whole subtree and not the node's own files: a series keeps its volumes in an edition
/// folder, so the count a reader needs on the series line is never on the series line's own
/// files.
struct Landing {
    /// How many files under this node would land on one the library already holds.
    int possible = 0;
    /// How many of those a reader has ticked. The difference between the two is the whole
    /// point of the box: `possible` is what the server found, `chosen` is what will happen.
    int chosen = 0;
};

/// The whole subtree and not the node's own files: a series keeps its volumes in an edition
/// folder, so the count a reader needs on the series line is never on the series line's own
/// files.
Landing landingUnder(const Manifest::Node &node, const Answered &answered)
{
    Landing found;
    for (const Manifest::Entry &file : node.files) {
        if (!answered.replaces.contains(file.path))
            continue;
        ++found.possible;
        if (answered.replacing.contains(file.path))
            ++found.chosen;
    }
    for (const Manifest::Node &child : node.children) {
        const Landing under = landingUnder(child, answered);
        found.possible += under.possible;
        found.chosen += under.chosen;
    }
    return found;
}

/// The same, for a container — which the server describes by what it declared rather than by
/// a queue.
Said stateOfNode(const Manifest::Node &node, const Answered &answered)
{
    using enum Imports::Tone;

    // Past tense once the card is done. The tree kept promising « sera créé » under a badge
    // that already read « Envoyé » — seventy-six volumes in the library and a line still
    // saying they would arrive. A state is what is about to happen or what has happened,
    // never both, and a card knowing which is no use if the tree under it does not.
    if (answered.done) {
        if (answered.creates.contains(node.at))
            return {Words::wasCreated(node.level), Ordinary};
        if (answered.moves.contains(node.at))
            return {Words::wasMoved(node.level), Ordinary};
        if (!node.files.isEmpty() || !node.children.isEmpty())
            return {Words::wasSent(node.level), Ordinary};
        return {Words::alreadyInTheLibrary(), Quiet};
    }
    if (answered.creates.contains(node.at))
        return {Words::willBeCreated(node.level), Ordinary};
    if (answered.moves.contains(node.at))
        return {Words::willBeMoved(node.level), Ordinary};
    // Before « déjà là », and instead of it. A container all of whose volumes the library
    // holds, minus one that would be overwritten, was saying that nothing was going to
    // happen — on the one line somebody reads before deciding, with the warning folded three
    // lines below it.
    if (const Landing landing = landingUnder(node, answered); landing.possible > 0)
        return {Words::holdsReplacements(landing.possible, landing.chosen), Attention};
    if (answered.holdsAlready.contains(node.at))
        return {Words::alreadyInTheLibrary(), Quiet};
    return {};
}

/// One node, flat, and its children only when it is unfolded.
///
/// Flattened rather than kept as a tree because a `Repeater` takes a list: a
/// `QAbstractItemModel` for a four-level accordion would be an engine built for a door, and
/// drawing it would need a `TreeView` the card does not want.
void flatten(const Manifest::Node &node, const QSet<QString> &open, int depth,
             QVariantList &into, const Answered &answered)
{
    const bool expandable = !node.children.isEmpty() || !node.files.isEmpty();
    const bool expanded = open.contains(node.at);
    const Said says = stateOfNode(node, answered);
    into << QVariantMap{
        {u"level"_s, std::to_underlying(node.level)},
        {u"name"_s, node.name},
        {u"at"_s, node.at},
        {u"holds"_s, Words::nodeHolds(node.volumes(), node.size)},
        {u"depth"_s, depth},
        {u"expandable"_s, expandable},
        {u"expanded"_s, expanded},
        {u"state"_s, says.text},
        {u"tone"_s, std::to_underlying(says.tone)},
        // A container's own decision: its declaration would be written over the one the
        // library holds, and that is not something dropping a folder twice asks for.
        {u"redeclarable"_s, answered.declarations.contains(node.at)},
        {u"declaring"_s,
         answered.declarations.contains(node.at)
             && answered.declaring.contains(answered.declarations.value(node.at).path)},
        {u"declarationPath"_s,
         answered.declarations.contains(node.at)
             ? answered.declarations.value(node.at).path
             : QString()},
        {u"present"_s,
         answered.declarations.contains(node.at)
             ? Words::declarationDiffers(answered.declarations.value(node.at).presentName,
                                         answered.declarations.value(node.at).differs)
             : QString()},
    };
    if (!expanded)
        return;
    for (const Manifest::Node &child : node.children)
        flatten(child, open, depth + 1, into, answered);
    for (const Manifest::Entry &file : node.files) {
        const Said said = stateOfVolume(file.path, answered);
        into << QVariantMap{
            // From the name, not hard-coded to `Volume`: a `.cbz` is a chapter or a
            // volume depending on what it is called, and `levelOf` is the one place
            // that rule is written.
            {u"level"_s, std::to_underlying(Manifest::levelOf(file.path))},
            {u"name"_s, Words::fileNamed(file.title, QFileInfo(file.path).fileName())},
            {u"at"_s, file.path},
            // The weight alone: `nodeHolds` stays silent when there is no volume to
            // count, and a volume does not count itself.
            {u"holds"_s, Words::size(file.size)},
            {u"depth"_s, depth + 1},
            {u"expandable"_s, false},
            {u"expanded"_s, false},
            {u"state"_s, said.text},
            {u"tone"_s, std::to_underlying(said.tone)},
            // A decision, not a description: only a volume that would land on something
            // carries a box, and it starts clear.
            {u"replaceable"_s, answered.replaces.contains(file.path)},
            {u"replacing"_s, answered.replacing.contains(file.path)},
            {u"present"_s,
             answered.replaces.contains(file.path)
                 ? Words::insteadOf(answered.replaces.value(file.path).presentTitle,
                                    answered.replaces.value(file.path).presentSize,
                                    answered.replaces.value(file.path).presentRead)
                 : QString()},
        };
    }
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
    , m_server(server)
{
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        bool any = false;
        for (int row = 0; row < m_rows.size(); ++row) {
            if (m_rows[row].retryIn <= 0)
                continue;
            --m_rows[row].retryIn;
            announce(row);
            if (m_rows[row].retryIn > 0) {
                any = true;
                continue;
            }
            settle(row, Stage::Ready);
        }
        if (!any)
            m_retry.stop();
        pump();
    });
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
    const Row &one = m_rows.at(index.row());

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
        return createsOf(index.row());
    case Moves:
        return movesOf(index.row());
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
    for (const Row &one : m_rows) {
        if (one.stage != Stage::Filed && one.stage != Stage::Failed)
            ++total;
    }
    return total;
}

int Imports::deciding() const
{
    int total = 0;
    for (const Row &one : m_rows) {
        if (one.stage == Stage::Deciding)
            ++total;
    }
    return total;
}

int Imports::reading() const
{
    // A folder queued behind another is not yet "being looked at" — it is waiting its turn,
    // which `describeNext` serialises on purpose. Counting it here would make the start
    // button wait on folders nobody has started walking, for as long as the queue is deep.
    int total = m_describing ? 1 : 0;
    for (const Row &one : m_rows) {
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
    return holding() >= 0;
}

/// The row that holds the one slot, or -1 when nobody does.
int Imports::holding() const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).stage == Stage::Sending || m_rows.at(row).stage == Stage::Filing)
            return row;
    }
    return -1;
}

int Imports::rowOf(const QString &path) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).path == path)
            return row;
    }
    return -1;
}

int Imports::rowOfToken(quint64 token) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).token == token)
            return row;
    }
    return -1;
}

void Imports::announce(int row)
{
    if (row >= 0 && row < m_rows.size()) {
        emit dataChanged(index(row), index(row));
    }
    emit changed();
}

void Imports::settle(int row, Stage stage, const QString &trouble)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row].stage = stage;
    m_rows[row].trouble = trouble;
    announce(row);
}


bool Imports::Row::nothingToDo() const
{
    if (!folder)
        return false;
    // Everything still to send is something to do, unless it would land on a file the
    // library holds and nobody said to.
    for (const QString &wanted : road.toSend) {
        const bool lands =
            std::ranges::any_of(road.replaces, [&wanted](const Api::Replacement &one) {
                return one.path == wanted;
            });
        if (!lands || road.replacing.contains(wanted))
            return false;
    }
    return road.creates.isEmpty() && road.filing.isEmpty() && road.declaring.isEmpty();
}

QVariantList Imports::Row::nodes() const
{
    Answered answered;
    for (const Api::Creation &created : road.creates)
        answered.creates.insert(created.at);
    for (const Api::Relocation &moved : road.moves)
        answered.moves.insert(moved.at);
    answered.toSend = road.toSend;
    for (const Api::Replacement &landing : road.replaces)
        answered.replaces.insert(landing.path, landing);
    answered.replacing = road.replacing;
    for (const Api::Declaration &said : road.declarations) {
        const QString at = QFileInfo(said.path).path();
        answered.declarations.insert(at == u"."_s ? QString() : at, said);
    }
    answered.declaring = road.declaring;
    answered.holdsAlready = containersOf(road.alreadyThere);
    answered.at = road.at;
    answered.moving = stage == Stage::Sending;
    answered.done = stage == Stage::Filed;
    answered.failed = stage == Stage::Failed;
    answered.announced = !id.isEmpty();

    QVariantList out;
    flatten(road.node, road.open, 0, out, answered);
    return out;
}

QString Imports::Row::checking(quint64 walking) const
{
    // A card that would move nothing says so, instead of reading « Prêt » over a
    // transfer that will not happen.
    if (stage == Stage::Ready && nothingToDo())
        return Words::nothingToSend();
    // Only while finding is still the folder's own present tense: past `Asking` — to
    // `Deciding` or `Ready`, `tookFolder`'s own two destinations — the count belongs
    // to a wait that is already over.
    if (stage != Stage::Asking || !folder)
        return QString();
    // Read to the last byte, and the server not answered yet. Its own word because
    // « En attente » meant two other things already, and a reader watching twenty-seven
    // gigabytes go past has no other way to know the verification is over.
    if (road.checked)
        return Words::announcing();
    // And only for the one folder actually on the pool does the word name a hash in
    // progress. Everything else reaching here is « en attente », for one of two
    // reasons that read the same from this side: a shelf of a dozen series queues
    // every one of them at `Asking` and only walks one at a time, so the eleven still
    // waiting their turn are not stuck — showing them a count frozen at zero would say
    // they were; and a walk with `verifying` off never sets `m_walkingToken` at all
    // (see `describe()`), because a stat-only pass is over before a second progress
    // report could matter, which is also exactly why the spec is explicit that without
    // checksums no card passes through « Vérification » — the finding alone suffices.
    return token == walking ? Words::checking(road.hashed, road.node.volumes())
                            : Words::waitingToBeChecked();
}

qint64 Imports::Row::sizeOf(const QString &relative) const
{
    for (const Manifest::Entry &entry : road.tree.files) {
        if (entry.path == relative)
            return entry.size;
    }
    return 0;
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
            if (rowOf(path) >= 0)
                continue;
            appendRow(path, about.fileName(), false, about.size());
            ask(int(m_rows.size()) - 1);
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
            if (rowOf(at) >= 0)
                continue;

            // Two folders of one name in a single drop land in the same place: `root` is
            // the folder's name and the server installs into `library/<root>`. Traced on a
            // shelf holding `Death Note/` and `Vieux/Death Note/` — two cards, one
            // destination, and the second would send over what the first had just filed,
            // without a word.
            const QString destination = QFileInfo(at).fileName();
            if (const int collidesWith = folderNamed(destination); collidesWith >= 0) {
                appendRow(at, node.name, true, node.size);
                const int row = int(m_rows.size()) - 1;
                // Posed all the same rather than dropped: hiding the second card would
                // look like a drop that missed something. Marked described so
                // `describeNext` never queues it — a refused card has nothing to read.
                m_rows[row].road.described = true;
                const QString here = QDir(path).relativeFilePath(at);
                const QString there = QDir(path).relativeFilePath(m_rows.at(collidesWith).path);
                settle(row, Stage::Failed, Words::sameDestination(here, there));
                continue;
            }

            // A copy, not a reference to `node.at` itself: `rebase` overwrites that very
            // field as it goes, and a prefix aliasing the value it strips would come back
            // empty after the first line and leave every descendant unstripped.
            const QString prefix = node.at;
            rebase(node, prefix);
            appendRow(at, node.name, true, node.size);
            m_rows.last().road.node = node;
        }
    }
    describeNext();
    emit changed();
}

void Imports::appendRow(const QString &path, const QString &name, bool folder, qint64 size)
{
    Row one;
    one.token = m_nextToken++;
    one.path = path;
    one.name = name;
    one.folder = folder;
    one.size = size;
    beginInsertRows({}, int(m_rows.size()), int(m_rows.size()));
    m_rows.append(one);
    endInsertRows();
}

void Imports::describeNext()
{
    if (m_describing)
        return;
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).folder && !m_rows.at(row).road.described) {
            m_describing = true;
            describe(row);
            return;
        }
    }
}

void Imports::describe(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const QString path = m_rows.at(row).path;
    // Carried into every callback below instead of `path`: a folder abandoned and
    // redropped while this very walk is still on the pool gives the new row the old
    // row's path, and a lookup by path would hand this walk's answer to a folder it was
    // never reading. `token` cannot collide — `m_nextToken` only grows.
    const quint64 token = m_rows.at(row).token;
    const bool verifying = m_verifying;

    // Parented, so shutting the application down takes the watcher with it. The walk itself
    // carries on for as long as it takes and touches nothing here — it has a copy of the
    // path and nothing else — and its answer is simply never delivered.
    auto *watcher = new QFutureWatcher<Manifest::Folder>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, token] {
        watcher->deleteLater();
        described(token, watcher->result());
    });

    if (!verifying) {
        // Without a checksum the walk is a `stat` of every file, over before the pool has
        // even been asked twice — reporting progress on something that fast would only add
        // three thousand deliveries for a folder of chapters, never a count anyone reads.
        m_walkingToken = 0;
        watcher->setFuture(QtConcurrent::run(Manifest::of, path, false, Manifest::Progress{}));
        return;
    }

    m_walkingToken = token;
    // `progressValueChanged` is `QFutureWatcherBase`'s own signal, marshaled onto this
    // object's thread exactly the way `finished` above already is — the pool thread only
    // ever writes into the `QPromise`, which is Qt's own thread-safe channel for exactly
    // this, and never touches `this`. That is what let the previous round's raw
    // `QMetaObject::invokeMethod` from the pool crash the suite the moment a test deleted
    // this object while a walk was still running, and what let the destructor written to
    // stop it block this thread for the rest of that walk instead — the freeze this commit
    // exists to remove, moved onto the way out rather than gone. Nothing here needs either:
    // the connection made below dies with `watcher`, a child of `this`, the ordinary way
    // any other signal connection would.
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this,
            [this, token](int done) { hashedSoFar(token, done); });

    const qint64 total = m_rows.at(row).road.node.volumes();
    watcher->setFuture(QtConcurrent::run(
        [path, total](QPromise<Manifest::Folder> &promise) {
            promise.setProgressRange(0, int(total));
            promise.addResult(Manifest::of(path, true, [&promise](qint64 done) {
                promise.setProgressValue(int(done));
            }));
        }));
}

void Imports::hashedSoFar(quint64 token, qint64 done)
{
    // By token and not by path — see `describe()` — for the same reason `described()`
    // looks up by token: the row may have been abandoned, or replaced by a redrop sharing
    // its old path, while the walk it came from was still running.
    const int at = rowOfToken(token);
    if (at < 0)
        return;
    m_rows[at].road.hashed = done;
    // Only the one role, not `announce()`'s full `dataChanged`: that would invalidate
    // every role the card reads, and `Role::Nodes` rebuilds the whole flattened tree from
    // scratch — turning the count that was meant to prove the wait is alive into rebuilding
    // an unfolded tree once per volume, quadratic in exactly the folder size this exists to
    // reassure about.
    emit dataChanged(index(at), index(at), {std::to_underlying(Role::Checking)});
}

void Imports::described(quint64 token, const Manifest::Folder &tree)
{
    // Cleared here rather than left for the next `describe()` to overwrite: between this
    // walk ending and the next one starting, nothing is on the pool, and `m_walkingToken`
    // saying otherwise would show a stale count against whichever row happens to be
    // walking next.
    if (m_walkingToken == token)
        m_walkingToken = 0;

    // By token, not by path: rows can be abandoned while a folder is being read, and a
    // folder redropped at the same path while that walk is still running would otherwise
    // receive this answer instead of the one it is actually waiting on.
    const int at = rowOfToken(token);
    if (at < 0) {
        m_describing = false;
        describeNext();
        return;
    }
    // Whatever the walk found, it is over: from here the card is waiting on the server and
    // not on the disk, and until this flag existed nothing on screen said so.
    m_rows[at].road.checked = true;
    m_rows[at].road.described = true;
    if (!tree.read()) {
        settle(at, Stage::Failed, tree.trouble);
    } else {
        m_rows[at].road.tree = tree;
        m_rows[at].size = tree.bytes();
        announceFolder(at, tree);
    }
    // The next one in the queue, and not before: two disk walks at once would get in each
    // other's way, and the first card would be ready no sooner for it.
    m_describing = false;
    describeNext();
}

void Imports::announceFolder(int row, const Manifest::Folder &tree)
{
    if (!m_server || row < 0 || row >= m_rows.size())
        return;

    QJsonArray files;
    for (const Manifest::Entry &entry : tree.files) {
        QJsonObject one{{u"path"_s, entry.path}, {u"size"_s, double(entry.size)}};
        if (!entry.checksum.isEmpty())
            one.insert(u"checksum"_s, entry.checksum);
        files.append(one);
    }
    QJsonArray sidecars;
    for (const Manifest::Sidecar &one : tree.sidecars) {
        sidecars.append(
            QJsonObject{{u"path"_s, one.path}, {u"json"_s, QString::fromUtf8(one.json)}});
    }

    // Always ADDITION: the scope question made sense only at the level that holds volumes,
    // and asking it of every edition of a ten-series universe was ten questions for a claim
    // one is rarely in a position to make. Naming a folder COMPLETE is a capability the
    // server keeps — nothing on this side asks for it any more.
    const QJsonObject body{{u"root"_s, tree.root},
                           {u"files"_s, files},
                           {u"sidecars"_s, sidecars},
                           {u"scope"_s, u"ADDITION"_s}};
    const QString path = m_rows.at(row).path;
    m_server->post(u"/import"_s, QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) { tookFolder(path, answer); });
}

void Imports::tookFolder(const QString &path, const Server::Answer &answer)
{
    const int row = rowOf(path);
    if (row < 0)
        return;
    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        return;
    }
    const Api::Read<Api::Opened> read = Api::opened(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        return;
    }

    m_rows[row].id = read.value->id;
    m_rows[row].road.toSend = read.value->toSend;
    m_rows[row].road.replaces = read.value->replaces;
    // Cleared with the announcement for `filing`'s own reason: a re-announced folder may
    // answer differently, and a tick against a volume it no longer names would overwrite
    // something for a reason nobody could see.
    m_rows[row].road.replacing.clear();
    m_rows[row].road.declarations = read.value->declarations;
    m_rows[row].road.declaring.clear();
    m_rows[row].road.alreadyThere = read.value->alreadyThere;
    m_rows[row].road.creates = read.value->creates;
    m_rows[row].road.moves = read.value->moves;
    // Cleared with the announcement, never carried across one: a re-announced folder may
    // answer differently, and a tick against a work it no longer names would move it for
    // no reason a reader could see.
    m_rows[row].road.filing.clear();
    m_rows[row].road.at = 0;
    m_rows[row].road.sentInFile = 0;
    // What is already there costs nothing and is not sent again, so the bar measures what
    // will actually travel rather than what the folder weighs.
    m_rows[row].size = read.value->bytesToSend;

    // Nothing to create and nothing to move is nothing to ask about. Creating a universe
    // is exactly the thing a reader cannot undo by deleting a file, and a series that moves
    // is one they would have to find again — so either stops and waits.
    const bool asks = !read.value->creates.isEmpty() || !read.value->moves.isEmpty();
    settle(row, asks ? Stage::Deciding : Stage::Ready);
}

QVariantList Imports::movesOf(int row) const
{
    QVariantList all;
    if (row < 0 || row >= m_rows.size())
        return all;
    for (const Api::Relocation &one : m_rows.at(row).road.moves) {
        all << QVariantMap{{u"workId"_s, one.workId},
                           {u"name"_s, one.name},
                           {u"from"_s, one.from},
                           {u"at"_s, one.at},
                           {u"filing"_s, m_rows.at(row).road.filing.contains(one.workId)}};
    }
    return all;
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
    announce(row);
}

bool Imports::isFiling(int row, const QString &workId) const
{
    if (row < 0 || row >= m_rows.size())
        return false;
    return m_rows.at(row).road.filing.contains(workId);
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
    announce(row);
}

bool Imports::isReplacing(int row, const QString &path) const
{
    if (row < 0 || row >= m_rows.size())
        return false;
    return m_rows.at(row).road.replacing.contains(path);
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
    announce(row);
}

bool Imports::isDeclaring(int row, const QString &path) const
{
    if (row < 0 || row >= m_rows.size())
        return false;
    return m_rows.at(row).road.declaring.contains(path);
}

QVariantList Imports::createsOf(int row) const
{
    QVariantList all;
    if (row < 0 || row >= m_rows.size())
        return all;
    for (const Api::Creation &one : m_rows.at(row).road.creates) {
        all << QVariantMap{{u"kind"_s, one.kind}, {u"name"_s, one.name},
                           {u"at"_s, one.at}};
    }
    return all;
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
    announce(row);
}

void Imports::accept(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).folder)
        return;
    settle(row, Stage::Ready);
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

void Imports::ask(int row)
{
    if (!m_server || row < 0 || row >= m_rows.size())
        return;
    const Row &one = m_rows.at(row);

    // Read here and not on a thread: the catalogue of a zip is a seek and a few kilobytes,
    // and a file that cannot be opened is answered by `Cbz` rather than by an exception.
    const Cbz::Found found = Cbz::sidecarOf(one.path);
    QJsonObject body{{u"name"_s, one.name}, {u"size"_s, double(one.size)}};
    if (!found.sidecar.isEmpty())
        body.insert(u"sidecar"_s, QString::fromUtf8(found.sidecar));

    const QString path = one.path;
    m_server->post(u"/preflight"_s, QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) { took(path, answer); });
}

void Imports::took(const QString &path, const Server::Answer &answer)
{
    const int row = rowOf(path);
    if (row < 0)
        return;

    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        return;
    }
    const Api::Read<Api::Reserved> read = Api::reserved(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        return;
    }

    m_rows[row].id = read.value->id;
    m_rows[row].proposal = read.value->proposal;

    // Sure of itself and one series named: there is nothing to ask, and asking anyway would
    // be fifty clicks for a shelf that was never in doubt.
    if (const Api::Proposal &said = read.value->proposal;
        said.confidence == Api::Proposal::Confidence::Certain && !said.candidates.isEmpty()) {
        m_rows[row].chosen = said.candidates.constFirst().seriesId;
        settle(row, Stage::Ready);
        return;
    }
    settle(row, Stage::Deciding);
}

void Imports::decide(int row, const QString &seriesId)
{
    if (row < 0 || row >= m_rows.size() || seriesId.isEmpty())
        return;
    m_rows[row].chosen = seriesId;
    settle(row, Stage::Ready);
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
    pump();
}

void Imports::pump()
{
    if (!m_server || !m_trouble.isEmpty() || holding() >= 0)
        return;

    for (int row = 0; row < m_rows.size(); ++row) {
        // A folder has no series to choose: it declares one. Asking it for a `chosen`
        // before letting it go left it Ready for ever, which looks exactly like a queue
        // that decided not to bother.
        if (m_rows.at(row).stage != Stage::Ready)
            continue;
        if (!m_rows.at(row).folder && m_rows.at(row).chosen.isEmpty())
            continue;
        // Left where it is. Putting the one in flight on top meant the list rearranged
        // itself under the reader's eyes every time a transfer ended — a card they were
        // looking at moved because a different one finished, and the order they dropped
        // the folders in was gone. The badge says which one is going; the list does not
        // have to.
        m_rows[row].stage = Stage::Sending;
        announce(row);
        sendMore(row);
        return;
    }

    // Nothing ready, and somebody stepped aside earlier: the slot is free and it is theirs
    // again. This is the rule nothing clicks — a paused file starts again by itself once
    // the one ahead of it is done.
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).stage != Stage::Paused)
            continue;
        // Except one that is counting seconds. `retryLater` parks a failed transfer at
        // `Paused` too, and this loop could not tell that apart from a reader's own pause
        // without looking at `retryIn` — so it took the slot back in the same turn the
        // failure arrived. The countdown on the card was a lie every time, and against a
        // server answering 500 it was a hot loop: fail, resume, fail, as fast as the
        // answers came back, with `attempts` climbing and no wait ever served.
        if (m_rows.at(row).retryIn > 0)
            continue;
        settle(row, Stage::Ready);
        pump();
        return;
    }
}

void Imports::sendMore(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (m_rows.at(row).folder) {
        sendMoreOfFolder(row);
        return;
    }
    const Row &one = m_rows.at(row);

    if (one.sent >= one.size) {
        confirm(row);
        return;
    }

    QFile file(one.path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(one.sent)) {
        settle(row, Stage::Failed, Words::couldNotBeRead(one.name));
        pump();
        return;
    }
    const QByteArray chunk = file.read(qMin(ChunkBytes, one.size - one.sent));
    const QString path = one.path;
    const qint64 from = one.sent;
    const qint64 whole = one.size;

    m_server->put(u"/intake/"_s + one.id + u"/file"_s, chunk, from, whole, this,
                  [this, path, chunk](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0)
                          return;
                      // Stepped aside, or thrown out, while these bytes were in the air.
                      // Whatever arrived is kept — the next attempt resumes past it.
                      if (m_rows.at(at).stage != Stage::Sending)
                          return;
                      if (!answer.went()) {
                          retryLater(at);
                          return;
                      }
                      m_rows[at].attempts = 0;
                      m_rows[at].sent += chunk.size();
                      announce(at);
                      sendMore(at);
                  });
}

void Imports::sendMoreOfFolder(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const Row &one = m_rows.at(row);

    if (one.road.at >= one.road.toSend.size()) {
        commitFolder(row);
        return;
    }
    const QString relative = one.road.toSend.at(one.road.at);
    const qint64 whole = one.sizeOf(relative);

    QFile file(one.path + u'/' + relative);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(one.road.sentInFile)) {
        settle(row, Stage::Failed, Words::couldNotBeRead(relative));
        pump();
        return;
    }
    const QByteArray chunk = file.read(qMin(ChunkBytes, whole - one.road.sentInFile));
    if (chunk.isEmpty()) {
        // Nothing left of this one. On to the next, without a request that would carry no
        // bytes and no range.
        m_rows[row].road.at += 1;
        m_rows[row].road.sentInFile = 0;
        sendMoreOfFolder(row);
        return;
    }

    const QString path = one.path;
    const qint64 from = one.road.sentInFile;
    QUrlQuery query;
    query.addQueryItem(u"path"_s, relative);
    m_server->put(u"/import/"_s + one.id + u"/file"_s, query, chunk, from, whole, this,
                  [this, path, went = chunk.size(), whole](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0 || m_rows.at(at).stage != Stage::Sending)
                          return;
                      // The one answer that is not a failure and not a success: the server
                      // holds less than this client believed. It says how much, and the
                      // next attempt starts exactly there rather than at the beginning.
                      if (answer.status == 409) {
                          resumeFrom(at);
                          return;
                      }
                      if (!answer.went()) {
                          retryLater(at);
                          return;
                      }
                      chunkLanded(at, went, whole);
                      sendMoreOfFolder(at);
                  });
}

void Imports::chunkLanded(int row, qint64 went, qint64 whole)
{
    m_rows[row].attempts = 0;
    m_rows[row].sent += went;
    m_rows[row].road.sentInFile += went;
    // Only the roles this chunk actually changed, not `announce()`'s full `dataChanged` —
    // the same fix `2614544` made for the hash tick, left on this path: a 7.9 GiB folder is
    // about two thousand chunks, and an unscoped signal here invalidated `Role::Nodes` on
    // every one of them, rebuilding the whole flattened tree — `node.volumes()` walked
    // afresh per node — and handing the `Repeater` a new `QVariantList` that destroyed and
    // recreated every `ImportNode`, every chevron `Canvas`, every level `Image`, once per
    // chunk rather than once per volume.
    QList roles{std::to_underlying(Role::Sent)};
    if (m_rows.at(row).road.sentInFile >= whole) {
        m_rows[row].road.at += 1;
        m_rows[row].road.sentInFile = 0;
        // The tree's own state moves here, not on every chunk: `stateOfVolume` reads
        // `Road::at` to say which node is « envoi » rather than « à envoyer » or « rangé »,
        // and that only changes once a whole file is done.
        roles.append(std::to_underlying(Role::Nodes));
    }
    emit dataChanged(index(row), index(row), roles);
}

void Imports::resumeFrom(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const QString path = m_rows.at(row).path;
    m_server->get(u"/import/"_s + m_rows.at(row).id, this,
                  [this, path](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0)
                          return;
                      const Api::Read<Api::Session> read = Api::session(answer.body.object());
                      if (!answer.went() || !read.ok()) {
                          retryLater(at);
                          return;
                      }
                      if (const Row &one = m_rows.at(at);
                          one.road.at < one.road.toSend.size()) {
                          m_rows[at].road.sentInFile =
                              read.value->received.value(one.road.toSend.at(one.road.at), 0);
                      }
                      announce(at);
                      sendMoreOfFolder(at);
                  });
}

void Imports::commitFolder(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row].stage = Stage::Filing;
    announce(row);

    const QString path = m_rows.at(row).path;
    // The answers travel with the commit rather than as a call of their own: the folder is
    // filed under the one that is being installed, and until that install runs there is no
    // "here" to file anything under.
    QJsonArray filing;
    for (const Api::Relocation &one : m_rows.at(row).road.moves) {
        if (m_rows.at(row).road.filing.contains(one.workId))
            filing << one.workId;
    }
    // And what may land on something already there. Only what was ticked: the server
    // installs over nothing it was not told to, and a path left out comes back in `pending`
    // rather than being quietly skipped.
    QJsonArray replacing;
    for (const QString &landing : m_rows.at(row).road.replacing)
        replacing << landing;
    QJsonArray declaring;
    for (const QString &said : m_rows.at(row).road.declaring)
        declaring << said;
    const QByteArray body =
        QJsonDocument(QJsonObject{{u"move"_s, filing},
                        {u"replace"_s, replacing},
                        {u"declare"_s, declaring}})
            .toJson(QJsonDocument::Compact);
    m_server->post(u"/import/"_s + m_rows.at(row).id + u"/commit"_s, body, this,
                   [this, path](const Server::Answer &answer) {
                       committed(rowOf(path), answer);
                   });
}

void Imports::committed(int row, const Server::Answer &answer)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        pump();
        return;
    }
    const Api::Read<Api::Installed> read = Api::installed(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        pump();
        return;
    }
    // A commit that could not install everything is not a failure and does not lose the
    // session: what is still coming is said out loud, and the row keeps the id it would be
    // sent against.
    settle(row, Stage::Filed,
           Words::whatLanded(read.value->installed, int(read.value->pending.size()),
                             int(read.value->corrupt.size()), int(read.value->orphans.size())));
    pump();
}

void Imports::confirm(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row].stage = Stage::Filing;
    announce(row);

    const QString path = m_rows.at(row).path;
    const QJsonObject body{{u"seriesId"_s, m_rows.at(row).chosen}};
    m_server->post(u"/intake/"_s + m_rows.at(row).id + u"/file"_s,
                   QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) {
                       const int at = rowOf(path);
                       if (at < 0)
                           return;
                       if (!answer.went()) {
                           settle(at, Stage::Failed, answer.trouble);
                           pump();
                           return;
                       }
                       settle(at, Stage::Filed);
                       pump();
                   });
}

void Imports::retryLater(int row)
{
    using enum Stage;
    if (row < 0 || row >= m_rows.size())
        return;
    // The key lost its right to import, or was refused: that is not this file's fault and
    // not something waiting will mend. Said once, and the queue stops.
    if (m_server && m_server->stopped()) {
        m_trouble = m_server->whyStopped();
        for (int at = 0; at < m_rows.size(); ++at) {
            if (m_rows.at(at).stage == Sending || m_rows.at(at).stage == Ready)
                settle(at, Failed);
        }
        emit changed();
        return;
    }

    // Read before it is raised, so the first wait is the first step. Raised first, the
    // table began at its second entry: the two seconds it opens with were never served to
    // anybody, and a cut cable waited five before it tried again.
    m_rows[row].retryIn = waitFor(m_rows.at(row).attempts);
    ++m_rows[row].attempts;
    m_rows[row].stage = Paused;
    announce(row);
    if (!m_retry.isActive())
        m_retry.start();
    // Somebody else may go while this one waits: the slot is not held by a file that is
    // counting seconds.
    pump();
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

    settle(row, Paused);
    pump();
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
    if (const int held = holding(); held >= 0)
        settle(held, Paused);
    settle(row, Ready);
    pump();
}

void Imports::abandon(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    // Kept whole, not just the id: the one path where the server refuses to let this go
    // reinserts exactly this — same tree, same name, everything a card needs to draw
    // itself — as the failed row it now is, rather than rebuilding one from whatever
    // scraps a callback captured.
    const Row kept = m_rows.at(row);
    const bool wasHolding = holding() == row;

    beginRemoveRows({}, row, row);
    m_rows.remove(row);
    endRemoveRows();

    // The server keeps what nobody named, so it has to be told. A file's session lives
    // under `/intake/{id}` and a folder's under `/import/{id}` — two different routes for
    // the two roads `Row::folder` already tells apart everywhere else in this file. Sending
    // a folder's id down `/intake/` instead refused every folder's cleanup —
    // `received_folder` on the server reads the id through `Origin::of` and never
    // recognises an `imp_…` one there — and the bytes it had already received stayed in
    // the inbox forever, one box per abandoned folder.
    if (m_server && !kept.id.isEmpty()) {
        const QString route = (kept.folder ? u"/import/"_s : u"/intake/"_s) + kept.id;
        m_server->remove(route, this, [this, kept](const Server::Answer &answer) {
            if (answer.went())
                return;
            // Said on the one card it is true of, and nowhere else: abandoning it says
            // nothing about whether the rest of the queue can still send, so a refusal
            // comes back as this one failed row rather than a `trouble` that would stop
            // every other one too — `pump()` reads that property and nothing here touches
            // it. `m_server->stopped()`, in `retryLater`, is the case where the cause
            // really is shared — a key that lost its right to import, which nothing
            // sending will get past either — and that one still halts the whole queue;
            // this is not that.
            Row back = kept;
            back.stage = Stage::Failed;
            back.trouble = Words::couldNotCleanUp(kept.name);
            beginInsertRows({}, int(m_rows.size()), int(m_rows.size()));
            m_rows.append(back);
            endInsertRows();
            emit changed();
        });
    }

    emit changed();
    if (wasHolding)
        pump();
}
