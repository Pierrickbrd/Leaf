// What a folder says it holds, before a byte of it moves.
//
// Built against folders made here rather than a fixture, because most of what is worth
// covering cannot be checked into a tree at all: a symlink that points back at a parent, a
// sidecar spelled in the wrong case, a folder nested deeper than anybody meant.

#include "Manifest.h"
#include "Zip.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

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

    /// BD/{Terres d'Arran/{Elfes, Mages}, Vrac}, with archives that are not zips.
    void aDrop()
    {
        wrote(u"BD/Terres d’Arran/Elfes/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"BD/Terres d’Arran/Mages/Tome 1.cbz"_s, QByteArray(10, 'b'));
        wrote(u"BD/Vrac/Livre.cbz"_s, QByteArray(10, 'c'));
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

    /// What a file aimed at a volume travels as: the server has one road for what is dropped
    /// on a target, and it is a folder's. Its root is its name, and only it is in it.
    void a_single_file_is_a_folder_holding_only_it()
    {
        const QString volume = m_folder.filePath(u"BD/Tome 1.cbz"_s);
        QVERIFY(QDir().mkpath(m_folder.filePath(u"BD"_s)));
        QVERIFY(wroteArchive(volume, R"({"leaf":1,"number":1})"));
        wrote(u"BD/Tome 2.cbz"_s, QByteArray(10, 'b'));

        const Manifest::Folder one = Manifest::single(volume, true);
        QVERIFY2(one.read(), qPrintable(one.trouble));
        QCOMPARE(one.root, u"Tome 1"_s);
        QCOMPARE(pathsOf(one), QStringList({u"Tome 1.cbz"_s}));
        QVERIFY(!one.files.constFirst().checksum.isEmpty());
        QVERIFY(Manifest::single(volume, false).files.constFirst().checksum.isEmpty());

        // A folder is not a file, and a file that is no archive is not a volume.
        QVERIFY(!Manifest::single(m_folder.filePath(u"BD"_s), false).read());
        wrote(u"BD/notes.txt"_s, "x");
        QVERIFY(!Manifest::single(m_folder.filePath(u"BD/notes.txt"_s), false).read());
    }

    /// A volume carries what it declares itself to be wherever the walk opened it, with
    /// checksums or without.
    ///
    /// The title is read out of the archive's own `entry.json`. It used to wait for the
    /// checksum pass, and a walk without checksums then listed every volume by its file
    /// name while its archive said otherwise. Only the instant tree opens nothing.
    void a_volume_carries_its_declared_title_with_or_without_checksums()
    {
        wrote(u"Koro/work.json"_s, R"({"leaf":1,"title":"Koro Quest"})");
        const QString volume = m_folder.filePath(u"Koro/Tome 1.cbz"_s);
        QVERIFY(QDir().mkpath(m_folder.filePath(u"Koro"_s)));
        QVERIFY(wroteArchive(volume, R"({"leaf":1,"title":"Assassinat"})"));

        const Manifest::Folder quick =
            Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY2(quick.read(), qPrintable(quick.trouble));
        QCOMPARE(quick.files.constFirst().title, u"Assassinat"_s);
        QVERIFY(quick.files.constFirst().checksum.isEmpty());

        const Manifest::Folder whole =
            Manifest::of(m_folder.filePath(u"Koro"_s), true);
        QVERIFY2(whole.read(), qPrintable(whole.trouble));
        QCOMPARE(whole.files.constFirst().title, u"Assassinat"_s);
    }

    /// The walk without checksums used to skip the archive altogether, so the import's
    /// first screen knew a volume's name and nothing it said about itself.
    void a_walk_without_checksums_still_reads_what_each_archive_says()
    {
        wrote(u"Koro/Tome 1.cbz"_s,
              Zip::volume(R"({"leaf":1,"title":"A"})", "<ComicInfo><Title>B</Title></ComicInfo>"));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Koro"_s), false);

        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(found.files.size(), 1);
        QVERIFY(!found.files.constFirst().members.comicInfo.sidecar.isEmpty());
        QVERIFY(!found.files.constFirst().members.entry.sidecar.isEmpty());
        QVERIFY(found.files.constFirst().checksum.isEmpty());
    }

    /// Locks the count in the mode without checksums: `onward` was already called outside
    /// the checksum branch, and the walk now opens an archive per call there, so the
    /// running total must still read 1, 2, 3.
    void the_walk_counts_every_archive_it_opens_without_checksums_too()
    {
        for (const QString &name : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s, u"Tome 3.cbz"_s})
            wrote(u"Koro/"_s + name, Zip::volume(R"({"leaf":1})"));

        for (const bool withChecksums : {false, true}) {
            QList<qint64> counted;
            const Manifest::Folder found =
                Manifest::of(m_folder.filePath(u"Koro"_s), withChecksums,
                             [&counted](qint64 done) { counted.append(done); });

            QVERIFY2(found.read(), qPrintable(found.trouble));
            QCOMPARE(counted, QList<qint64>({1, 2, 3}));
        }
    }

    /// Finding stays instant: a folder holding an archive nobody can read is found exactly
    /// as a folder holding a good one, because nothing is opened.
    void the_instant_tree_still_opens_no_archive()
    {
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Koro/Tome 2.cbz"_s, Zip::volume(R"({"leaf":1})"));

        const QList<Manifest::Node> found = Manifest::found(m_folder.filePath(u"Koro"_s));

        QCOMPARE(found.size(), 1);
        QCOMPARE(found.constFirst().volumes(), 2);
        for (const Manifest::Entry &one : found.constFirst().files) {
            QVERIFY(one.members.entry.sidecar.isEmpty());
            QVERIFY(one.members.entry.opened());
            QVERIFY(one.title.isEmpty());
        }
    }

    /// An archive that does not open is a file of the folder all the same, and it says what
    /// went wrong instead of vanishing from the list.
    void an_unreadable_archive_is_still_listed_with_what_went_wrong()
    {
        wrote(u"Koro/Tome 1.cbz"_s, QByteArray(10, 'a'));

        const Manifest::Folder found = Manifest::of(m_folder.filePath(u"Koro"_s), false);

        QVERIFY2(found.read(), qPrintable(found.trouble));
        QCOMPARE(found.files.size(), 1);
        QVERIFY(!found.files.constFirst().members.entry.opened());
        QVERIFY(!found.files.constFirst().members.comicInfo.opened());
        QVERIFY(found.files.constFirst().title.isEmpty());
    }

    /// « Tome 12 - 1984 » is volume 12: the year belongs to the title, which comes after the
    /// separator. The server cuts the label first for the same reason (`last_number`).
    void a_number_is_read_from_the_label_and_never_from_the_title()
    {
        QCOMPARE(Manifest::numberOf(u"Tome 12 - 1984.cbz"_s), std::optional<double>(12));
        QCOMPARE(Manifest::numberOf(u"blue_period_13.cbz"_s), std::optional<double>(13));
        QCOMPARE(Manifest::numberOf(u"Tome 4,5.cbz"_s), std::optional<double>(4.5));
        QCOMPARE(Manifest::numberOf(u"Chap. -3.cbz"_s), std::optional<double>(-3));
        QCOMPARE(Manifest::numberOf(u"Tome 7.cbz"_s), std::optional<double>(7));
        QCOMPARE(Manifest::numberOf(u"Tome 1 vol 2.cbz"_s), std::optional<double>(2));
        QCOMPARE(Manifest::numberOf(u"vol2.cbz"_s), std::optional<double>(2));
        // A separator with no digit behind it ends the number rather than joining it.
        QCOMPARE(Manifest::numberOf(u"Tome 4,.cbz"_s), std::optional<double>(4));
        QCOMPARE(Manifest::numberOf(u"Tome 4..cbz"_s), std::optional<double>(4));
        QCOMPARE(Manifest::numberOf(u"a-3.cbz"_s), std::optional<double>(-3));
        QVERIFY(!Manifest::numberOf(u"Special.cbz"_s).has_value());
        QVERIFY(!Manifest::numberOf(u"Bonus : Chapter 0.cbz"_s).has_value());
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

    /// The server rewrites an archive as it installs it, and a checksum of the file as sent
    /// says nothing of the file that comes out. The members are hashed on what they decompress
    /// to, deflated or stored, and only when the checksum is asked for.
    void members_are_hashed_on_their_content_when_the_checksum_is_asked_for()
    {
        const QByteArray deflatedPage = QByteArray("\xFF\xD8\xFF une page, ") + QByteArray(5000, 'x');
        const QByteArray storedPage("\xFF\xD8\xFF une autre", 17);
        wrote(u"Koro/Tome 1.cbz"_s,
              Zip::anArchive({{"001.jpg", deflatedPage, true, {}},
                              {"scans/002.jpg", storedPage, false, {}},
                              {"entry.json", QByteArray("{}"), true, {}}}));
        const auto sum = [](const QByteArray &bytes) {
            return QString::fromUtf8(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };

        const Manifest::Folder without = Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY(without.files.constFirst().members.digests.isEmpty());

        const Manifest::Folder with = Manifest::of(m_folder.filePath(u"Koro"_s), true);
        const QList<Cbz::Digest> &pages = with.files.constFirst().members.digests;
        QVERIFY(with.files.constFirst().members.digestTrouble.isEmpty());
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages.at(0).name, u"001.jpg"_s);
        QCOMPARE(pages.at(0).size, qint64(deflatedPage.size()));
        QCOMPARE(pages.at(0).checksum, sum(deflatedPage));
        QCOMPARE(pages.at(1).name, u"scans/002.jpg"_s);
        QCOMPARE(pages.at(1).checksum, sum(storedPage));

        const Manifest::Folder one =
            Manifest::single(m_folder.filePath(u"Koro/Tome 1.cbz"_s), true);
        QCOMPARE(one.files.constFirst().members.digests.size(), 3);
    }

    /// An archive whose members cannot all be hashed is announced without any, and says why:
    /// a list with a hole in it would make the server refuse a volume that is whole, and a
    /// silence would leave the reader thinking the pages were covered.
    void an_archive_that_cannot_be_hashed_whole_says_why_and_sends_none()
    {
        wrote(u"Koro/Tome 1.cbz"_s,
              Zip::anArchive({{"001.jpg", QByteArray("\xFF\xD8\xFF page", 8), false, {}}}));
        wrote(u"Koro/Tome 2.cbz"_s, QByteArray("Ceci n’est pas un zip"));

        const Manifest::Folder with = Manifest::of(m_folder.filePath(u"Koro"_s), true);
        QCOMPARE(with.files.size(), 2);
        QVERIFY(with.files.at(0).members.digestTrouble.isEmpty());
        QVERIFY(!with.files.at(1).members.digests.size());
        QVERIFY(!with.files.at(1).members.digestTrouble.isEmpty());

        const Manifest::Folder without = Manifest::of(m_folder.filePath(u"Koro"_s), false);
        QVERIFY(without.files.at(1).members.digestTrouble.isEmpty());
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

    /// The measured defect: a `cover.jpg` beside `work.json` was walked past, so it never
    /// left the PC and the series came up with no cover. It is announced beside the volumes
    /// and still counted among none of them; one beside loose volumes has no one to be the
    /// cover of, and one that is not named like a cover is not one.
    void a_cover_beside_a_declaration_is_announced_and_is_not_a_volume()
    {
        wrote(u"Colorist/work.json"_s, R"({"leaf":1,"title":"Colorist"})");
        wrote(u"Colorist/Chapitre 001.cbz"_s, QByteArray(9, 'a'));
        wrote(u"Colorist/Cover.JPG"_s, QByteArray(9, 'b'));
        wrote(u"Colorist/notes.jpg"_s, QByteArray(9, 'c'));
        wrote(u"Loose/Tome 1.cbz"_s, QByteArray(9, 'a'));
        wrote(u"Loose/cover.jpg"_s, QByteArray(9, 'b'));

        const Manifest::Folder declared = Manifest::of(m_folder.filePath(u"Colorist"_s), false);
        QVERIFY2(declared.read(), qPrintable(declared.trouble));
        QCOMPARE(pathsOf(declared), QStringList({u"Chapitre 001.cbz"_s}));
        QCOMPARE(declared.covers.size(), 1);
        QCOMPARE(declared.covers.constFirst().path, u"Cover.JPG"_s);
        QCOMPARE(declared.covers.constFirst().size, qint64(9));

        const Manifest::Folder loose = Manifest::of(m_folder.filePath(u"Loose"_s), false);
        QVERIFY(loose.covers.isEmpty());
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

    /// The instant tree walked shelves through and never kept them, so a drop of shelves
    /// could not be shown as the folders it is.
    void the_whole_drop_keeps_its_shelves_and_opens_nothing()
    {
        aDrop();

        const Manifest::Node drop = Manifest::whole(m_folder.filePath(u"BD"_s));

        QCOMPARE(drop.name, u"BD"_s);
        QCOMPARE(drop.at, QString());
        QVERIFY(!drop.declared);
        QCOMPARE(drop.children.size(), 2);
        const Manifest::Node &arran = drop.children.at(0);
        QCOMPARE(arran.name, u"Terres d’Arran"_s);
        QVERIFY(!arran.declared);
        QCOMPARE(arran.children.size(), 2);
        QCOMPARE(arran.children.at(0).name, u"Elfes"_s);
        QCOMPARE(arran.children.at(1).name, u"Mages"_s);
        QCOMPARE(arran.children.at(0).at, u"Terres d’Arran/Elfes"_s);
        QCOMPARE(drop.children.at(1).name, u"Vrac"_s);
        QVERIFY(!drop.children.at(1).declared);
        QCOMPARE(drop.volumes(), 3);
        QVERIFY(arran.children.at(0).files.constFirst().members.entry.sidecar.isEmpty());
        QVERIFY(arran.children.at(0).files.constFirst().members.entry.opened());
    }

    /// `Level::Work` is what a node falls back to, so the level alone could not tell a
    /// folder that said so from one that merely holds archives.
    void a_node_knows_whether_it_declared_itself()
    {
        wrote(u"Vide/work.json"_s, QByteArray());
        wrote(u"Vide/Tome 1.cbz"_s, QByteArray(10, 'a'));
        wrote(u"Muet/Tome 1.cbz"_s, QByteArray(10, 'b'));

        QVERIFY(Manifest::found(m_folder.filePath(u"Vide"_s)).constFirst().declared);
        QVERIFY(!Manifest::found(m_folder.filePath(u"Muet"_s)).constFirst().declared);
    }

    /// Keeping shelves for `whole` must not change `found`, which walks them through.
    void found_still_walks_shelves_through()
    {
        aDrop();

        const QList<Manifest::Node> cards = Manifest::found(m_folder.filePath(u"BD"_s));

        QCOMPARE(cards.size(), 3);
        // Breadth first, as it always was: Vrac is one folder down, the other two are two.
        QCOMPARE(cards.at(0).name, u"Vrac"_s);
        QCOMPARE(cards.at(1).name, u"Elfes"_s);
        QCOMPARE(cards.at(2).name, u"Mages"_s);
        for (const Manifest::Node &card : cards)
            QVERIFY(!card.declared);
    }
};

QTEST_MAIN(DescribesAFolder)
#include "describes_a_folder.moc"
