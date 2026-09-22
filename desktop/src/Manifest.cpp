#include "Manifest.h"

#include "Cbz.h"
#include "Words.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// The three files a folder declares itself with. Named here as the server names them, and
/// matched without case for the same reason the archive reader does: a library carried
/// across a filesystem that does not care about case comes back with `Work.json`.
const QStringList &declarations()
{
    static const QStringList names{u"universe.json"_s, u"work.json"_s, u"edition.json"_s};
    return names;
}

bool declares(const QString &name)
{
    for (const QString &one : declarations()) {
        if (name.compare(one, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/// The name a declaration gives, or the folder's own name failing that.
///
/// `name` for a universe and an edition, `title` for a work — the format's own words, and
/// neither one is the other's fallback.
QString nameIn(const QByteArray &declaration, const QString &folder)
{
    const QJsonObject said = QJsonDocument::fromJson(declaration).object();
    for (const QString &key : {u"name"_s, u"title"_s}) {
        const QString found = said.value(key).toString().trimmed();
        if (!found.isEmpty())
            return found;
    }
    return folder;
}

/// Where a file's label stops and its title, if any, begins — the same cut
/// `metadata::label::separator` makes on the server: whitespace around a dash (`-`, `–`,
/// `—`) or a colon, or a colon followed by whitespace on its own. No separator, no title:
/// the label is the whole text.
QString labelIn(const QString &stem)
{
    const QString text = stem.trimmed();
    const QChar colon(u':');
    const QChar hyphen(u'-');
    const QChar enDash(0x2013);
    const QChar emDash(0x2014);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch != colon && ch != hyphen && ch != enDash && ch != emDash)
            continue;
        const bool beforeIsSpace = i > 0 && text.at(i - 1).isSpace();
        const bool afterIsSpace = i + 1 < text.size() && text.at(i + 1).isSpace();
        if (afterIsSpace && (beforeIsSpace || ch == colon))
            return text.left(i).trimmed();
    }
    return text;
}

/// The title an archive's own `entry.json` declares, or nothing.
///
/// Nothing rather than a guess: an archive that declares no title is not one titled after
/// its file name, and saying so would make the two indistinguishable on screen.
QString titleIn(const QByteArray &sidecar)
{
    if (sidecar.isEmpty())
        return {};
    const QJsonObject said = QJsonDocument::fromJson(sidecar).object();
    return said.value(u"title"_s).toString();
}

/// The order a reader counts in: « Tome 2 » before « Tome 10 ».
///
/// `QDir::Name` sorts by code point, which puts « Tome 10 » between « Tome 1 » and « Tome 2 »
/// — measured on a twenty-one volume series, where the tree read 1, 10, 11, 12, …, 2, 20, 21,
/// 3. A shelf nobody can read down is a shelf whose order says nothing. `QCollator` in
/// numeric mode is the one place this rule is written; it is `thread_local` because the walk
/// runs on a pool and building one per comparison costs more than the sort.
bool beforeNaturally(const QFileInfo &left, const QFileInfo &right)
{
    static thread_local QCollator order = [] {
        QCollator made;
        made.setNumericMode(true);
        made.setCaseSensitivity(Qt::CaseInsensitive);
        return made;
    }();
    // Folders first, then files: a reader looking for an edition should not have to pass
    // sixty volumes to find it.
    if (left.isDir() != right.isDir())
        return left.isDir();
    return order.compare(left.fileName(), right.fileName()) < 0;
}

/// What a folder declares about itself, in the order `layout::kind` decides.
std::optional<std::pair<Manifest::Level, QString>> declaredHere(const QString &at)
{
    const QFileInfoList entries = QDir(at).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &[name, level] :
         {std::pair{u"universe.json"_s, Manifest::Level::Universe},
          std::pair{u"work.json"_s, Manifest::Level::Work},
          std::pair{u"edition.json"_s, Manifest::Level::Edition}}) {
        // Case-insensitive, like `declares()`: a library carried across a filesystem
        // that does not care about case comes back with `Work.json`, and
        // `a_declaration_in_the_wrong_case_is_still_one` already depends on that for
        // `Manifest::of` — finding has to see the same thing.
        for (const QFileInfo &about : entries) {
            if (about.fileName().compare(name, Qt::CaseInsensitive) == 0)
                return std::pair{level, about.absoluteFilePath()};
        }
    }
    return std::nullopt;
}

/// Whether a file's extension is one the server treats as an archive —
/// `scan/layout.rs::EXTENSIONS`, matched the same case-insensitive way. The one rule every
/// reader of a dropped folder has to share, the same way `levelOf` is the one place the
/// chapter rule is written: `holdsArchives` already tested it, but the walks that build
/// `Node::files` and `Folder::files` did not, and counted a `cover.jpg` as a volume for it —
/// the book icon, a place in the "34 tomes" of a card, and a place in the "n/N tomes" of a
/// checking count it was never part of.
bool isArchive(const QFileInfo &about)
{
    const QString suffix = about.suffix().toLower();
    return suffix == u"cbz"_s || suffix == u"zip"_s;
}

/// Whether a name starts with a dot — the same test `layout::sub_folders` makes on the
/// server before it will step into a folder, by name and nothing else. Qt's own
/// `QDir::Hidden` filter is not that: none of the calls below ask for it, so an entry is
/// skipped only when the *platform* calls it hidden — the dot convention on Unix, which is
/// why a `.git` folder never showed up here on Linux even before this existed, and the
/// `FILE_ATTRIBUTE_HIDDEN` bit on Windows, which an ordinary cloned repository's `.git`
/// does not carry. There, without this, it was read as a level of the model the server —
/// whose own rule is the name, not an attribute no platform of its own to check — would
/// never even look at.
bool isHidden(const QString &name)
{
    return name.startsWith(u'.');
}

bool holdsArchives(const QString &at)
{
    const QDir here(at);
    for (const QFileInfo &about : here.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (isArchive(about))
            return true;
    }
    return false;
}

void walk(const QString &at, const QString &root, int left, Manifest::Folder &into,
          bool withChecksums, const Manifest::Progress &onward)
{
    if (left == 0) {
        into.trouble = Words::folderTooDeep();
        return;
    }

    const QDir here(at);
    QFileInfoList all = here.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(all.begin(), all.end(), beforeNaturally);
    for (const QFileInfo &about : all) {
        // The entry's own kind, never what it points at: a symlink to a folder is a leaf,
        // so a link back to a parent is a file this does not follow rather than a walk
        // that never ends.
        if (about.isSymLink())
            continue;

        if (about.isDir()) {
            // The server's own walk never steps into one of these — `layout::sub_folders`
            // — so a `.git` or a stray `.DS_Store` directory left inside a dropped folder
            // must not be read as a level of the model here either.
            if (isHidden(about.fileName()))
                continue;
            walk(about.absoluteFilePath(), root, left - 1, into, withChecksums, onward);
            if (!into.read())
                return;
            continue;
        }

        const QString relative = QDir(root).relativeFilePath(about.absoluteFilePath());
        if (declares(about.fileName())) {
            QFile file(about.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly))
                into.sidecars.append({relative, file.readAll()});
            continue;
        }
        // Only what the server would call an archive: `scan/layout.rs` never treats a
        // `cover.jpg` or any other non-`.cbz`/`.zip` file as one, and this walk used to
        // count every file all the same.
        if (!isArchive(about))
            continue;

        Manifest::Entry entry;
        entry.path = relative;
        entry.size = about.size();
        if (withChecksums) {
            entry.checksum = Manifest::checksumOf(about.absoluteFilePath());
            // And what it says it is, while it is open anyway. A container shows the title
            // its sidecar declares and a volume showed only its file name, so a shelf of
            // « Tome 1.cbz » said nothing a folder listing did not. One more open per file
            // against a full read for the checksum — and never on the instant tree, which
            // opens nothing at all.
            entry.title = titleIn(Cbz::sidecarOf(about.absoluteFilePath()).sidecar);
        }
        into.files.append(entry);
        // The running total, not the file just read: a caller counts against the volumes
        // `found()` already told it about, and has no use for which one this was.
        if (onward)
            onward(into.files.size());
    }
}

/// A node, and what it holds. Recursive: what a declared thing contains is its tree,
/// never other maps.
Manifest::Node nodeOf(const QString &at, const QString &root, int left)
{
    Manifest::Node node;
    node.at = QDir(root).relativeFilePath(at);
    if (node.at == u"."_s)
        node.at.clear();

    const auto declared = declaredHere(at);
    node.level = declared ? declared->first : Manifest::Level::Work;
    if (declared) {
        QFile file(declared->second);
        if (file.open(QIODevice::ReadOnly))
            node.declaration = file.readAll();
    }
    node.name = nameIn(node.declaration, QFileInfo(at).fileName());

    QFileInfoList here = QDir(at).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(here.begin(), here.end(), beforeNaturally);
    for (const QFileInfo &about : here) {
        if (about.isSymLink())
            continue;
        if (about.isDir()) {
            if (isHidden(about.fileName()))
                continue;
            if (left > 0) {
                // Kept only if it says something. A folder that declares nothing, holds no
                // archive and has no descendant doing either is not a thing this model has
                // a level for — `Bleach/tmp`, two thousand loose pages and a `manifest.json`
                // that is not a sidecar, came back as a series inside Bleach. `found` asks
                // exactly this question when it chooses the cards; the tree under a card was
                // the one place that did not.
                const Manifest::Node child = nodeOf(about.absoluteFilePath(), root, left - 1);
                if (!child.declaration.isEmpty() || !child.files.isEmpty()
                    || !child.children.isEmpty()) {
                    node.children.append(child);
                }
            }
            continue;
        }
        if (declares(about.fileName()))
            continue;
        if (!isArchive(about))
            continue;
        Manifest::Entry entry;
        entry.path = QDir(root).relativeFilePath(about.absoluteFilePath());
        entry.size = about.size();
        node.files.append(entry);
        node.size += entry.size;
    }
    for (const Manifest::Node &child : node.children)
        node.size += child.size;
    return node;
}

} // namespace

namespace Manifest {

qint64 Folder::bytes() const
{
    qint64 total = 0;
    for (const Entry &one : files)
        total += one.size;
    return total;
}

QString checksumOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    // Fed in blocks rather than read whole: a two hundred megabyte volume held in memory to
    // be hashed is two hundred megabytes held for no reason, and a folder of forty of them
    // would be the whole library at once.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    // `fromUtf8` on a digest that is plain hex either way. The narrower reading would do
    // and the guard in `tools/` refuses it all the same — it refuses the word itself, in a
    // comment as in code, because a codebase that makes an exception "just this once" is
    // the one where `é` comes back as `Ã©` somewhere nobody was looking.
    return QString::fromUtf8(hash.result().toHex());
}

Folder of(const QString &path, bool withChecksums, const Progress &onward)
{
    const QFileInfo about(path);
    if (!about.isDir())
        return {{}, {}, {}, Words::notAFolder()};

    Folder found;
    found.root = about.fileName();
    walk(about.absoluteFilePath(), about.absoluteFilePath(), DeepestFolders, found,
         withChecksums, onward);
    if (!found.read())
        return {{}, {}, {}, found.trouble};
    return found;
}

qint64 Node::volumes() const
{
    qint64 total = files.size();
    for (const Node &child : children)
        total += child.volumes();
    return total;
}

Level levelOf(const QString &fileName)
{
    // The server judges the label, not the whole name: `scanner::entry_kind` reads
    // `label::parse(&stem_of(file)).label`, which stops at the first separator and drops
    // whatever follows. Skip that cut and a title alone decides — `Tome 4 -
    // L’échappée.cbz` and a hypothetical `... - chapelle.cbz` both carry "chap" past the
    // dash, and a plain volume would come back a chapter that the scan then contradicts.
    if (labelIn(QFileInfo(fileName).completeBaseName()).contains(u"chap"_s, Qt::CaseInsensitive))
        return Level::Chapter;
    return Level::Volume;
}

QList<Node> found(const QString &path)
{
    const QFileInfo about(path);
    if (!about.isDir())
        return {};

    QList<Node> out;
    // A stack rather than recursion: the walk only descends while it has found nothing,
    // and mixing that with building the node made both unreadable.
    QList<std::pair<QString, int>> waiting{{about.absoluteFilePath(), DeepestShelves}};
    while (!waiting.isEmpty()) {
        const auto [at, left] = waiting.takeFirst();
        if (declaredHere(at) || holdsArchives(at)) {
            // Rooted at `path`, the query, and not at `at`, the thing just found: a shelf
            // holding two series returned both with `.at` empty, and nothing left to tell
            // a caller which folder on disk was which — importing a card meant importing
            // whichever one happened to sit first. Rooted the same way `Manifest::of` roots
            // a single folder, every node `found` returns now answers "where" the same way.
            out.append(nodeOf(at, about.absoluteFilePath(), DeepestFolders));
            continue;
        }
        if (left == 0)
            continue;
        QFileInfoList children = QDir(at).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(children.begin(), children.end(), beforeNaturally);
        for (const QFileInfo &child : children) {
            if (!child.isSymLink() && !isHidden(child.fileName()))
                waiting.append({child.absoluteFilePath(), left - 1});
        }
    }
    return out;
}

} // namespace Manifest
