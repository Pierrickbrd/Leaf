#include "Card.h"

#include "Words.h"

#include <QFileInfo>
#include <QVariantMap>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

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
    Card::Tone tone = Card::Tone::Ordinary;
};

/// What one volume or chapter becomes, from the list the server asked for and how far the
/// queue behind it has got.
Said stateOfVolume(const QString &path, const Answered &answered)
{
    using enum Card::Tone;

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
    using enum Card::Tone;

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

bool Card::nothingToDo() const
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

QVariantList Card::nodes() const
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
    answered.moving = stage == Card::Stage::Sending;
    answered.done = stage == Card::Stage::Filed;
    answered.failed = stage == Card::Stage::Failed;
    answered.announced = !id.isEmpty();

    QVariantList out;
    flatten(road.node, road.open, 0, out, answered);
    return out;
}

QString Card::checking(quint64 walking) const
{
    // A card that would move nothing says so, instead of reading « Prêt » over a
    // transfer that will not happen.
    if (stage == Card::Stage::Ready && nothingToDo())
        return Words::nothingToSend();
    // Only while finding is still the folder's own present tense: past `Asking` — to
    // `Deciding` or `Ready`, `tookFolder`'s own two destinations — the count belongs
    // to a wait that is already over.
    if (stage != Card::Stage::Asking || !folder)
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

qint64 Card::sizeOf(const QString &relative) const
{
    for (const Manifest::Entry &entry : road.tree.files) {
        if (entry.path == relative)
            return entry.size;
    }
    return 0;
}

QVariantList Card::creates() const
{
    QVariantList all;
    for (const Api::Creation &one : road.creates)
        all << QVariantMap{{u"kind"_s, one.kind}, {u"name"_s, one.name}, {u"at"_s, one.at}};
    return all;
}

QVariantList Card::moves() const
{
    QVariantList all;
    for (const Api::Relocation &one : road.moves) {
        all << QVariantMap{{u"workId"_s, one.workId},
                           {u"name"_s, one.name},
                           {u"from"_s, one.from},
                           {u"at"_s, one.at},
                           {u"filing"_s, road.filing.contains(one.workId)}};
    }
    return all;
}
