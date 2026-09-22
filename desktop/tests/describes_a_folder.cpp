// What a folder says it holds, before a byte of it moves.
//
// Built against folders made here rather than a fixture, because most of what is worth
// covering cannot be checked into a tree at all: a symlink that points back at a parent, a
// sidecar spelled in the wrong case, a folder nested deeper than anybody meant.

#include "Manifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class DescribesAFolder : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_folder;

    /// A real zip holding one stored member, written out by hand.
    ///
    /// Stored and not deflated, because nothing here is testing compression: what is under
    /// test is that `Manifest`'s walk opens the file at all and finds the declaration in it.
    /// Sixteen lines beats borrowing the eighty-line writer `opens_an_archive.cpp` keeps for
    /// the cases that do test the format.
    static bool wroteArchive(const QString &path, const QByteArray &sidecar)
    {
        const QByteArray name = "entry.json";
        const auto two = [](QByteArray &to, quint16 value) {
            to.append(char(value & 0xFF));
            to.append(char((value >> 8) & 0xFF));
        };
        const auto four = [](QByteArray &to, quint32 value) {
            for (int shift = 0; shift < 32; shift += 8)
                to.append(char((value >> shift) & 0xFF));
        };

        QByteArray file;
        four(file, 0x04034b50);
        two(file, 20);
        two(file, 0);
        two(file, 0);  // stored
        two(file, 0);
        two(file, 0);
        four(file, 0);
        four(file, quint32(sidecar.size()));
        four(file, quint32(sidecar.size()));
        two(file, quint16(name.size()));
        two(file, 0);
        file.append(name);
        file.append(sidecar);

        const quint32 catalogueAt = quint32(file.size());
        four(file, 0x02014b50);
        two(file, 20);
        two(file, 20);
        two(file, 0);
        two(file, 0);
        two(file, 0);
        two(file, 0);
        four(file, 0);
        four(file, quint32(sidecar.size()));
        four(file, quint32(sidecar.size()));
        two(file, quint16(name.size()));
        two(file, 0);
        two(file, 0);
        two(file, 0);
        two(file, 0);
        four(file, 0);
        four(file, 0);
        file.append(name);

        const quint32 catalogueSize = quint32(file.size()) - catalogueAt;
        four(file, 0x06054b50);
        two(file, 0);
        two(file, 0);
        two(file, 1);
        two(file, 1);
        four(file, catalogueSize);
        four(file, catalogueAt);
        two(file, 0);

        QFile out(path);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        out.write(file);
        return true;
    }

    QString make(const QString &relative)
    {
        const QString at = m_folder.filePath(relative);
        [&] { QVERIFY(QDir().mkpath(at)); }();
        return at;
    }

    void wrote(const QString &relative, const QByteArray &bytes)
    {
        const QString at = m_folder.filePath(relative);
        [&] { QVERIFY(QDir().mkpath(QFileInfo(at).absolutePath())); }();
        QFile file(at);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(bytes);
    }

    QStringList pathsOf(const Manifest::Folder &found) const
    {
        QStringList all;
        for (const Manifest::Entry &one : found.files)
            all << one.path;
        all.sort();
        return all;
    }

private slots:
    void initTestCase() { QVERIFY(m_folder.isValid()); }

    void init()
    {
        // Each slot starts from an empty tree: what a walk finds is the thing under test,
        // and a leftover from the slot before would be found too.
        QDir(m_folder.path()).removeRecursively();
        QVERIFY(QDir().mkpath(m_folder.path()));
    }

    /// A volume carries what it declares itself to be, but only where something opened it.
    ///
    /// The title is read out of the archive's own `entry.json`, during the pass that is
    /// already reading every byte for its checksum. The instant tree opens nothing, and a
    /// drop without checksums opens nothing either — so a title there would cost the one
    /// thing that phase exists to give.
    void a_volume_carries_its_declared_title_only_once_something_has_opened_it()
    {
        wrote(u"Koro/work.json"_s, R"({"leaf":1,"title":"Koro Quest"})");
        const QString volume = m_folder.filePath(u"Koro/Tome 1.cbz"_s);
        QVERIFY(QDir().mkpath(m_folder.filePath(u"Koro"_s)));
        QVERIFY(wroteArchive(volume, R"({"leaf":1,"title":"Assassinat"})"));

        const Manifest::Folder quick =
            Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY2(quick.read(), qPrintable(quick.trouble));
        QCOMPARE(quick.files.constFirst().title, QString());

        const Manifest::Folder whole =
            Manifest::of(m_folder.filePath(u"Koro"_s), true);
        QVERIFY2(whole.read(), qPrintable(whole.trouble));
        QCOMPARE(whole.files.constFirst().title, u"Assassinat"_s);
    }

    /// A folder that declares nothing and holds no archive is not a level of the model.
    ///
    /// Measured on a real library: `Bleach/tmp` held two thousand loose pages, a
    /// `LIRE-MOI.txt` and a `manifest.json` that is not a sidecar — and the tree under
    /// Bleach showed it as a series inside Bleach. `Manifest::found` asks exactly this
    /// question when it chooses which folders become cards; the tree under a card was the
    /// one place that did not ask it.
    void a_folder_that_declares_nothing_and_holds_no_archive_is_not_a_node()
    {
        wrote(u"Bleach/work.json"_s, R"({"leaf":1,"title":"Bleach"})");
        wrote(u"Bleach/Édition standard/Tome 1.cbz"_s, QByteArray(40, 'a'));
        wrote(u"Bleach/tmp/manifest.json"_s, R"({"pages":2119})");
        wrote(u"Bleach/tmp/LIRE-MOI.txt"_s, "rien à voir ici");
        wrote(u"Bleach/tmp/T01 - 01 - page.jpg"_s, QByteArray(900, 'j'));

        const QList<Manifest::Node> cards = Manifest::found(m_folder.filePath(u"Bleach"_s));
        QCOMPARE(cards.size(), 1);

        QStringList named;
        for (const Manifest::Node &child : cards.constFirst().children)
            named << child.name;
        QCOMPARE(named, QStringList({u"Édition standard"_s}));

        // And its pages are not weighed either: a node nobody keeps carries nothing.
        QCOMPARE(cards.constFirst().volumes(), 1);
    }

    /// The ordinary shape: a series folder, its declaration, and its volumes.
    void a_folder_is_its_files_and_its_declaration()
    {
        wrote(u"Death Note/work.json"_s, R"({"leaf":1,"title":"Death Note"})");
        wrote(u"Death Note/Tome 1.cbz"_s, QByteArray(120, 'a'));
        wrote(u"Death Note/Tome 2.cbz"_s, QByteArray(80, 'b'));

        const Manifest::Folder found =
            Manifest::of(m_folder.filePath(u"Death Note"_s), false);
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(found.root, u"Death Note"_s);
        QCOMPARE(pathsOf(found), QStringList({u"Tome 1.cbz"_s, u"Tome 2.cbz"_s}));
        QCOMPARE(found.bytes(), 200);

        // The declaration is not one of the files to send: it says where they go.
        QCOMPARE(found.sidecars.size(), 1);
        QCOMPARE(found.sidecars.constFirst().path, u"work.json"_s);
        QVERIFY(found.sidecars.constFirst().json.contains("Death Note"));
    }

    /// A universe with two series under it. All three declarations travel, each named by
    /// the folder it describes — that is how the server knows what to create.
    void a_universe_carries_every_declaration_under_it()
    {
        wrote(u"Terres d’Arran/universe.json"_s, R"({"leaf":1,"name":"Terres d’Arran"})");
        wrote(u"Terres d’Arran/Elfes/work.json"_s, R"({"leaf":1,"title":"Elfes"})");
        wrote(u"Terres d’Arran/Elfes/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Terres d’Arran/Mages/work.json"_s, R"({"leaf":1,"title":"Mages"})");
        wrote(u"Terres d’Arran/Mages/Deluxe/edition.json"_s, R"({"leaf":1,"name":"Deluxe"})");
        wrote(u"Terres d’Arran/Mages/Deluxe/Tome 1.cbz"_s, QByteArray(10, 'b'));

        const Manifest::Folder found =
            Manifest::of(m_folder.filePath(u"Terres d’Arran"_s), false);
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(found.files.size(), 2);
        QCOMPARE(found.sidecars.size(), 4);

        QStringList where;
        for (const Manifest::Sidecar &one : found.sidecars)
            where << one.path;
        where.sort();
        QCOMPARE(where,
                 QStringList({u"Elfes/work.json"_s, u"Mages/Deluxe/edition.json"_s,
                              u"Mages/work.json"_s, u"universe.json"_s}));
    }

    /// A library carried across a filesystem that does not care about case comes back with
    /// `Work.json`, and it still declares a work.
    void a_declaration_in_the_wrong_case_is_still_one()
    {
        wrote(u"Bleach/WORK.JSON"_s, R"({"leaf":1,"title":"Bleach"})");
        wrote(u"Bleach/Tome 1.cbz"_s, QByteArray(10, 'a'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Bleach"_s), false);
        QCOMPARE(found.sidecars.size(), 1);
        QCOMPARE(found.files.size(), 1);
    }

    /// The checksum is the sender's call, and it is what makes "already there" mean the
    /// contents rather than the length.
    void a_checksum_is_the_file_and_not_its_length()
    {
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray("le contenu exact"));
        const QString path = m_folder.filePath(u"Koro/Tome 1.cbz"_s);

        const Manifest::Folder without = Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY(without.files.constFirst().checksum.isEmpty());

        const Manifest::Folder with = Manifest::of(m_folder.filePath(u"Koro"_s), true);
        QCOMPARE(with.files.constFirst().checksum, Manifest::checksumOf(path));
        QCOMPARE(with.files.constFirst().checksum,
                 QString::fromUtf8(
                     QCryptographicHash::hash(QByteArray("le contenu exact"),
                                              QCryptographicHash::Sha256)
                         .toHex()));
        // Lowercase hex, because that is what the contract says and what the server
        // compares against — an uppercase digest matches nothing at all.
        QCOMPARE(with.files.constFirst().checksum,
                 with.files.constFirst().checksum.toLower());
    }

    /// A symlink is a leaf, never descended into. Without that a link back to a parent is
    /// a walk that never ends, and the folder is announced as infinite.
    void a_link_back_to_a_parent_is_not_followed()
    {
        wrote(u"Parasite/Tome 1.cbz"_s, QByteArray(10, 'a'));
        make(u"Parasite/Deluxe"_s);
        const QString back = m_folder.filePath(u"Parasite/Deluxe/boucle"_s);
        QVERIFY(QFile::link(m_folder.filePath(u"Parasite"_s), back));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Parasite"_s), false);
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(pathsOf(found), QStringList({u"Tome 1.cbz"_s}));
    }

    /// And a tree genuinely deeper than anything a library needs says so rather than
    /// walking until something else gives out.
    void a_folder_deeper_than_a_library_ever_is_says_so()
    {
        QString at = u"Fond"_s;
        for (int i = 0; i < Manifest::DeepestFolders + 2; ++i)
            at += u"/niveau"_s;
        wrote(at + u"/Tome 1.cbz"_s, QByteArray(10, 'a'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Fond"_s), false);
        QVERIFY(!found.read());
        QVERIFY(!found.trouble.isEmpty());
    }

    /// A file is not a folder, and neither is something that is not there.
    void what_is_not_a_folder_is_refused_and_says_so()
    {
        wrote(u"seul.cbz"_s, QByteArray(10, 'a'));
        QVERIFY(!Manifest::of(m_folder.filePath(u"seul.cbz"_s)).read());
        QVERIFY(!Manifest::of(m_folder.filePath(u"absent"_s)).read());
    }

    /// An empty folder reads fine and announces nothing. It is not an error: a reader can
    /// drop one, and being told so beats being told it is broken.
    void an_empty_folder_reads_and_announces_nothing()
    {
        make(u"Vide"_s);
        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Vide"_s));
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QVERIFY(found.files.isEmpty());
        QCOMPARE(found.bytes(), 0);
    }

    /// Paths are relative to the root and use `/`, because that is what crosses the wire
    /// and what the server joins onto its own library path.
    void paths_are_relative_to_the_root()
    {
        wrote(u"Haikyu/Saison 1/Tome 1.cbz"_s, QByteArray(10, 'a'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Haikyu"_s), false);
        QCOMPARE(pathsOf(found), QStringList({u"Saison 1/Tome 1.cbz"_s}));
        QVERIFY(!found.files.constFirst().path.startsWith(u'/'));
    }

    /// The measured defect: dropping the folder that holds every series produced a single
    /// entry named after it, and the server would have installed the whole library into
    /// `library/<that name>`. A folder that declares nothing is a shelf — the rule
    /// `scan/layout.rs` has always applied, carried over here.
    void a_shelf_is_walked_through_and_never_becomes_one_thing()
    {
        wrote(u"Dépôt/Death Note/work.json"_s, R"({"leaf":1,"title":"Death Note"})");
        wrote(u"Dépôt/Death Note/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Dépôt/Terres d’Arran/universe.json"_s, R"({"leaf":1,"name":"Arran"})");
        wrote(u"Dépôt/Terres d’Arran/Elfes/work.json"_s, R"({"leaf":1,"title":"Elfes"})");
        wrote(u"Dépôt/Terres d’Arran/Elfes/Tome 1.cbz"_s, QByteArray(10, 'b'));

        const QList<Manifest::Node> found = Manifest::found(m_folder.filePath(u"Dépôt"_s));

        QCOMPARE(found.size(), 2);
        QCOMPARE(found.at(0).name, u"Death Note"_s);
        QCOMPARE(found.at(0).level, Manifest::Level::Work);
        // The folder is named "Terres d'Arran", the declaration says "Arran": the
        // declaration wins, or renaming the folder would rename the work.
        QCOMPARE(found.at(1).name, u"Arran"_s);
        QCOMPARE(found.at(1).level, Manifest::Level::Universe);
        // The shelf itself is nowhere: it is the one that would have become
        // `library/Dépôt/`.
        for (const Manifest::Node &one : found)
            QVERIFY(one.name != u"Dépôt"_s);
    }

    /// The walk stops at the first declared thing: under a work, editions are nodes of
    /// its tree, not maps of their own.
    void what_a_declared_thing_holds_is_its_tree_and_not_more_cards()
    {
        wrote(u"Dragon Ball/work.json"_s, R"({"leaf":1,"title":"Dragon Ball"})");
        wrote(u"Dragon Ball/Perfect/edition.json"_s, R"({"leaf":1,"name":"Perfect"})");
        wrote(u"Dragon Ball/Perfect/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Dragon Ball/Originale/edition.json"_s, R"({"leaf":1,"name":"Originale"})");
        wrote(u"Dragon Ball/Originale/Tome 1.cbz"_s, QByteArray(10, 'b'));

        const QList<Manifest::Node> found =
            Manifest::found(m_folder.filePath(u"Dragon Ball"_s));

        QCOMPARE(found.size(), 1);
        const Manifest::Node &work = found.constFirst();
        QCOMPARE(work.level, Manifest::Level::Work);
        QCOMPARE(work.children.size(), 2);
        QCOMPARE(work.children.constFirst().level, Manifest::Level::Edition);
        QCOMPARE(work.volumes(), 2);
        // Relative to the map, because that is what the server receives in `sidecars`.
        QCOMPARE(work.at, QString());
        QCOMPARE(work.children.constFirst().at, u"Originale"_s);
    }

    /// Archives sitting with nothing declared are a work with an implicit edition — the
    /// same rule as `layout::kind`, which calls that proof rather than a guess.
    void archives_with_nothing_declared_are_a_work()
    {
        wrote(u"Bleach/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Bleach/Tome 2.cbz"_s, QByteArray(10, 'b'));

        const QList<Manifest::Node> found = Manifest::found(m_folder.filePath(u"Bleach"_s));

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.constFirst().level, Manifest::Level::Work);
        QCOMPARE(found.constFirst().volumes(), 2);
        QVERIFY(found.constFirst().declaration.isEmpty());
    }

    /// Shelves nested deeper than a library ever needs stop, at the same depth as the
    /// server's — otherwise the two would diverge on some twisted case and nobody would
    /// know which one is right.
    void shelves_nested_past_what_a_library_needs_stop()
    {
        QString at = u"Fond"_s;
        for (int i = 0; i < Manifest::DeepestShelves + 2; ++i)
            at += u"/étagère"_s;
        wrote(at + u"/Série/Tome 1.cbz"_s, QByteArray(10, 'a'));

        QVERIFY(Manifest::found(m_folder.filePath(u"Fond"_s)).isEmpty());
    }

    /// Finding reads no byte of any volume: that is what makes it instant, and it is what
    /// lets the tree show before the hashing starts.
    void finding_costs_no_checksum()
    {
        wrote(u"Koro/work.json"_s, R"({"leaf":1,"title":"Koro"})");
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray(4096, 'a'));

        const QList<Manifest::Node> found = Manifest::found(m_folder.filePath(u"Koro"_s));

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.constFirst().size, 4096);
        for (const Manifest::Entry &one : found.constFirst().files)
            QVERIFY(one.checksum.isEmpty());
    }

    /// One call per file, counting up rather than announcing a fraction: the caller already
    /// knows the folder's own volume count from `found()`, and dividing there is its job,
    /// not the walk's. What the checking line on screen needs from this is exactly the
    /// running total — the word alone, fixed for however long the hash takes, is what once
    /// made the application look frozen.
    void the_progress_callback_counts_one_file_at_a_time()
    {
        wrote(u"Bleach/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Bleach/Tome 2.cbz"_s, QByteArray(10, 'b'));
        wrote(u"Bleach/Tome 3.cbz"_s, QByteArray(10, 'c'));

        QList<qint64> counted;
        const Manifest::Folder found = Manifest::of(
            m_folder.filePath(u"Bleach"_s), true,
            [&counted](qint64 done) { counted.append(done); });

        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(found.files.size(), 3);
        QCOMPARE(counted, QList<qint64>({1, 2, 3}));
    }

    /// A `.cbz` is a volume or a chapter, and the tree has to say which. The model carries
    /// both — `entry.type` is VOLUME or CHAPTER — and `Chapitre 686.5.cbz` is a chapter
    /// that arrived on its own. Decided from the name alone, which is the rule the server
    /// falls back on: reading the declaration inside would mean opening every archive of a
    /// dropped folder, and the finding would stop being instant.
    void a_file_is_a_volume_or_a_chapter_and_its_name_says_which()
    {
        QCOMPARE(Manifest::levelOf(u"Tome 12.cbz"_s), Manifest::Level::Volume);
        QCOMPARE(Manifest::levelOf(u"Chapitre 686.5.cbz"_s), Manifest::Level::Chapter);
        // Case-insensitive, like the server.
        QCOMPARE(Manifest::levelOf(u"CHAP.099.cbz"_s), Manifest::Level::Chapter);
        QCOMPARE(Manifest::levelOf(u"One-Shot.cbz"_s), Manifest::Level::Volume);

        // The title does not decide: the server cuts at the separator and reads only the
        // label. Without that cut, "échappée" would turn a volume into a chapter.
        QCOMPARE(Manifest::levelOf(u"Tome 4 - L’échappée.cbz"_s), Manifest::Level::Volume);
        QCOMPARE(Manifest::levelOf(u"Bonus : Chapitre 0.cbz"_s), Manifest::Level::Volume);
        QCOMPARE(Manifest::levelOf(u"Chap.099 : Coup de sifflet.cbz"_s),
                 Manifest::Level::Chapter);
    }

    /// The measured defect: the server only ever treats `.cbz` and `.zip` as an archive
    /// (`scan/layout.rs::EXTENSIONS`) — `holdsArchives`, in this same file, already knew it
    /// — but the walk behind `Manifest::of` counted every other file in the folder as an
    /// `Entry` all the same. A `cover.jpg` took the book icon, called itself a volume, and
    /// inflated both a card's own "34 tomes" and the checking count's "n/N tomes".
    void a_non_archive_file_is_not_counted_as_a_volume()
    {
        wrote(u"Koro/work.json"_s, R"({"leaf":1,"title":"Koro Quest"})");
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray(9, 'a'));
        wrote(u"Koro/cover.jpg"_s, QByteArray(9, 'b'));
        wrote(u"Koro/notes.txt"_s, QByteArray(9, 'c'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(pathsOf(found), QStringList({u"Tome 1.cbz"_s}));
    }

    /// Same rule, `found()`'s tree and not only `of()`'s flat list: a `cover.jpg` sitting
    /// beside a work's declaration counted in `Node::volumes()` too, so the card itself —
    /// not only the checking line — said "2 tomes" for a series that holds one.
    void found_does_not_count_a_non_archive_file_as_a_volume()
    {
        wrote(u"Bleach/work.json"_s, R"({"leaf":1,"title":"Bleach"})");
        wrote(u"Bleach/Tome 1.cbz"_s, QByteArray(9, 'a'));
        wrote(u"Bleach/cover.jpg"_s, QByteArray(9, 'b'));

        const QList<Manifest::Node> found = Manifest::found(m_folder.filePath(u"Bleach"_s));

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.constFirst().volumes(), 1);
    }

    /// Same mismatch, the other axis: the server's own walk skips anything starting with
    /// `.` (`layout::sub_folders`), and this one did not — a `.git` directory, or a stray
    /// `.DS_Store` folder some tool left behind, was read as a level of the model the
    /// server would never even have looked at.
    void a_hidden_folder_is_not_descended_into()
    {
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray(9, 'a'));
        wrote(u"Koro/.git/Tome 2.cbz"_s, QByteArray(9, 'b'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(pathsOf(found), QStringList({u"Tome 1.cbz"_s}));
    }
};

QTEST_MAIN(DescribesAFolder)
#include "describes_a_folder.moc"
