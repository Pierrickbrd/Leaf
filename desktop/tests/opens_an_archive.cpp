// The brick the import stands on: what a CBZ says about itself.
//
// Tested against archives built here, byte by byte, rather than against a fixture checked
// into the tree. A zip is a format with an end record, a catalogue and a header per member,
// and the failures worth covering are exactly the ones a fixture cannot express: a member
// stored rather than deflated, a local header whose extra field is not the catalogue's, a
// comment that happens to carry the end signature, an archive that is not one at all.

#include "Cbz.h"
#include "Words.h"
#include "Zip.h"

#include <QByteArray>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

using Zip::anArchive;
using Zip::Member;

/// The four bytes that open a catalogue entry.
const QByteArray CentralHeader("PK\x01\x02", 4);

/// The same archive, with one catalogue entry claiming a different compression method.
///
/// The catalogue is what the reader believes about a member — `anArchive` can only write
/// the two methods it knows, and both of the things that go wrong here are archives that
/// say one thing in the catalogue and hold another.
QByteArray sayingMethod(QByteArray file, const QByteArray &name, quint16 method)
{
    for (qsizetype at = 0; (at = file.indexOf(CentralHeader, at)) >= 0;
         at += 4) {
        const int nameLength =
            quint8(file.at(at + 28)) | (quint8(file.at(at + 29)) << 8);
        if (file.mid(at + 46, nameLength) != name)
            continue;
        file[at + 10] = char(method & 0xFF);
        file[at + 11] = char((method >> 8) & 0xFF);
        break;
    }
    return file;
}

/// The same archive, with one catalogue entry claiming `bytes` of compressed data — a figure
/// the file's writer chooses, and not one the file has to hold.
QByteArray sayingPackedSize(QByteArray file, const QByteArray &name, quint32 bytes)
{
    for (qsizetype at = 0; (at = file.indexOf(CentralHeader, at)) >= 0; at += 4) {
        const int nameLength = quint8(file.at(at + 28)) | (quint8(file.at(at + 29)) << 8);
        if (file.mid(at + 46, nameLength) != name)
            continue;
        for (int i = 0; i < 4; ++i)
            file[at + 20 + i] = char((bytes >> (8 * i)) & 0xFF);
        break;
    }
    return file;
}

/// The same archive, with the end record filling its member count the way Zip64 does.
QByteArray sayingZip64(QByteArray file)
{
    const qsizetype end = file.size() - 22;
    file[end + 10] = char(0xFF);
    file[end + 11] = char(0xFF);
    return file;
}

/// Something shaped like a page, and large enough that reading it would show.
QByteArray aPage()
{
    QByteArray page("\xFF\xD8\xFF", 3);
    page.append(QByteArray(64 * 1024, '\x7F'));
    return page;
}

const QByteArray Sidecar = R"({"leaf":1,"number":7,"title":"Le pari"})";

} // namespace

class OpensAnArchive : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_folder;

    QString wrote(const QByteArray &bytes, const QString &name = u"tome.cbz"_s)
    {
        const QString path = m_folder.filePath(name);
        QFile file(path);
        [&] { QVERIFY(file.open(QIODevice::WriteOnly)); }();
        file.write(bytes);
        file.close();
        return path;
    }

private slots:
    void initTestCase() { QVERIFY(m_folder.isValid()); }

    /// The ordinary shape: pages, and one small JSON among them.
    void an_archive_gives_up_its_sidecar()
    {
        const QString path = wrote(anArchive({
            {"001.jpg", aPage(), false, {}},
            {"entry.json", Sidecar, true, {}},
            {"002.jpg", aPage(), false, {}},
        }));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.opened());
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// Stored rather than deflated, which is what a zip does with something already small
    /// or already compressed. The reader must not hand it to zlib.
    void a_sidecar_that_was_not_compressed_is_read_as_it_is()
    {
        const QString path = wrote(anArchive({
            {"entry.json", Sidecar, false, {}},
            {"001.jpg", aPage(), false, {}},
        }));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.opened());
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// The local header carries its own extra field, and real archives use it for padding.
    /// Seeking past the catalogue's length instead lands inside the data.
    void a_local_header_with_padding_of_its_own_is_still_found()
    {
        const QString path = wrote(anArchive({
            {"entry.json", Sidecar, true, QByteArray(37, '\0')},
        }));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.opened());
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// Matched on the last segment and without case, which is what the server matches on.
    void the_name_is_matched_without_case_and_inside_a_folder()
    {
        const QString path = wrote(anArchive({
            {"Tome 7/Meta/ENTRY.JSON", Sidecar, true, {}},
        }));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.opened());
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// An ordinary file from somewhere else. It opened, it holds nothing, and it is not a
    /// failure — the proposal will rest on its name and its size.
    void an_archive_without_one_opens_and_says_nothing()
    {
        const QString path = wrote(anArchive({
            {"001.jpg", aPage(), false, {}},
            {"ComicInfo.xml", "<ComicInfo/>", true, {}},
        }));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY2(found.opened(), qPrintable(found.trouble));
        QVERIFY(found.sidecar.isEmpty());
    }

    /// The comment is the last thing in a zip and can hold anything, the end signature
    /// included. Scanning backwards finds the record; scanning forwards finds the comment.
    void a_comment_carrying_the_end_signature_does_not_win()
    {
        QByteArray decoy;
        decoy.append("PK\x05\x06", 4);
        decoy.append(QByteArray(18, '\0'));
        const QString path = wrote(anArchive({{"entry.json", Sidecar, true, {}}}, decoy));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY2(found.opened(), qPrintable(found.trouble));
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// Said in French, and distinguishable from "opened and holds nothing": the screen
    /// warns that the proposal will be weaker, and it can only warn if it knows.
    void something_that_is_not_an_archive_says_so()
    {
        const Cbz::Found found = Cbz::sidecarOf(wrote("Ceci n’est pas un zip"));
        QVERIFY(!found.opened());
        QVERIFY(!found.trouble.isEmpty());
        QVERIFY(found.sidecar.isEmpty());
    }

    void a_file_that_is_not_there_says_so_too()
    {
        const Cbz::Found found = Cbz::sidecarOf(m_folder.filePath(u"absent.cbz"_s));
        QVERIFY(!found.opened());
        QVERIFY(!found.trouble.isEmpty());
    }

    /// An empty file is neither an archive nor a crash. It used to be the second: the end
    /// record was looked for in a window larger than the file.
    void an_empty_file_is_refused_rather_than_read()
    {
        const Cbz::Found found = Cbz::sidecarOf(wrote({}, u"vide.cbz"_s));
        QVERIFY(!found.opened());
    }

    /// The whole point of the class. A volume is two hundred megabytes of pages and the
    /// sidecar is a few hundred bytes; reading the pages would make the pre-flight cost
    /// what the transfer costs, and there would be no pre-flight left.
    void the_pages_are_never_read()
    {
        QList<Member> members;
        for (int i = 0; i < 40; ++i)
            members << Member{QByteArray::number(i) + ".jpg", aPage(), false, {}};
        members << Member{"entry.json", Sidecar, true, {}};
        const QString path = wrote(anArchive(members), u"gros.cbz"_s);

        QFile archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const qint64 whole = archive.size();
        archive.close();
        QVERIFY(whole > 2 * 1024 * 1024);

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.opened());
        QCOMPARE(found.sidecar, Sidecar);
    }

    /// Zip64 is said outright rather than read wrong.
    ///
    /// It declares itself by filling the ordinary fields with ones and putting the real
    /// figures in a second record this reader does not read. Reading the ones as numbers
    /// would seek a catalogue of sixty-five thousand members into the middle of a file. A
    /// comic volume is never four gigabytes, so the archive is offered without its sidecar
    /// and the card says why.
    void an_archive_that_says_zip64_is_left_where_it_is()
    {
        const QString path = wrote(sayingZip64(anArchive({
            {"entry.json", Sidecar, true, {}},
        })));

        const Cbz::Found found = Cbz::sidecarOf(path);
        QVERIFY(found.sidecar.isEmpty());
        QCOMPARE(found.trouble, Words::archiveTooBig());
    }

    /// A sidecar too large to be one is refused by its own declared size, before a byte of
    /// it is read — the catalogue says how big it will be, and believing it is the whole
    /// point of the ceiling.
    void a_sidecar_larger_than_a_sidecar_can_be_is_never_inflated()
    {
        // Written with escapes rather than as a raw string: one that both opens and closes
        // on a quote reads as the end of the literal to `moc`, which then finds no class in
        // this file at all and leaves the test binary without a vtable to link against.
        QByteArray huge = "{\"leaf\":1,\"title\":\"";
        huge.append(QByteArray(Cbz::MostSidecarBytes, 'x'));
        huge.append("\"}");

        const Cbz::Found found = Cbz::sidecarOf(wrote(anArchive({
            {"entry.json", huge, true, {}},
        })));

        QVERIFY(found.sidecar.isEmpty());
        QCOMPARE(found.trouble, Words::sidecarTooBig());
    }

    /// The compressed size is as free to lie as the whole one: a sidecar whose few bytes are
    /// said to be four gigabytes is refused as too big, as one that says so of its whole size,
    /// and not asked of the disk.
    void a_sidecar_said_to_be_packed_past_any_sidecar_is_never_read()
    {
        const Cbz::Found found = Cbz::sidecarOf(wrote(sayingPackedSize(
            anArchive({{"entry.json", Sidecar, true, {}}}), "entry.json", 0x7FFFFFFF)));

        QVERIFY(found.sidecar.isEmpty());
        QCOMPARE(found.trouble, Words::sidecarTooBig());
    }

    /// A method this client does not know is named rather than guessed at.
    ///
    /// Stored and deflated are the two a comic archive is ever written with; the format
    /// allows a dozen more. Treating an unknown one as deflated would hand zlib bytes that
    /// are not a deflate stream, and the answer would be "no sidecar" — which is what an
    /// archive that genuinely has none says, and the two are not the same fact.
    void a_sidecar_compressed_a_way_this_client_does_not_know_says_which()
    {
        const Cbz::Found found = Cbz::sidecarOf(wrote(
            sayingMethod(anArchive({{"entry.json", Sidecar, true, {}}}), "entry.json", 9)));

        QVERIFY(found.sidecar.isEmpty());
        QCOMPARE(found.trouble, Words::sidecarCompressedInAnUnknownWay());
    }

    /// A member that says deflated and is not comes back as a concern, not as silence.
    ///
    /// The one failure that cannot be told from a truncated file: the catalogue is
    /// believed, the bytes are fetched, and zlib refuses them. Saying nothing here would
    /// let a volume whose metadata was quietly lost pass for one that never had any.
    void a_sidecar_that_will_not_inflate_says_so_rather_than_going_quiet()
    {
        // Written stored, so the bytes on disk are plain JSON, and then declared deflated.
        const Cbz::Found found = Cbz::sidecarOf(wrote(
            sayingMethod(anArchive({{"entry.json", Sidecar, false, {}}}), "entry.json", 8)));

        QVERIFY(found.sidecar.isEmpty());
        QCOMPARE(found.trouble, Words::sidecarCouldNotBeInflated());
    }

    /// A reimport compares page counts, and a second stream per archive was 115 000 seeks
    /// over 57 686 pages: the images are counted in the pass that finds the sidecars, and
    /// only those — a folder entry, an `entry.json` and a stray `.txt` are no page.
    void the_pages_are_counted_in_the_pass_that_reads_the_members()
    {
        const QString path = wrote(anArchive({
            {"entry.json", Sidecar, true, {}},
            {"scans/", {}, false, {}},
            {"scans/001.JPG", aPage(), false, {}},
            {"scans/002.webp", aPage(), false, {}},
            {"003.png", aPage(), false, {}},
            {"004.jpeg", aPage(), false, {}},
            {"005.gif", aPage(), false, {}},
            {"006.avif", aPage(), false, {}},
            {"notes.txt", "x", true, {}},
            {"ComicInfo.xml", "<ComicInfo/>", true, {}},
        }));

        QCOMPARE(Cbz::membersOf(path).pages, 6);
        QCOMPARE(Cbz::membersOf(wrote(anArchive({{"entry.json", Sidecar, true, {}}}))).pages, 0);
        QCOMPARE(Cbz::membersOf(m_folder.filePath(u"absent.cbz"_s)).pages, -1);
    }

    /// The importer wants both members of a volume, and an archive's catalogue costs one
    /// open and one read: asking twice would double both over every file of a folder.
    void both_members_are_found_in_one_reading()
    {
        const QString path = wrote(anArchive({
            {"001.jpg", aPage(), false, {}},
            {"entry.json", Sidecar, true, {}},
            {"scans/ComicInfo.XML", "<ComicInfo><Number>7</Number></ComicInfo>", true, {}},
        }));

        const Cbz::Members members = Cbz::membersOf(path);
        QVERIFY(members.entry.opened());
        QVERIFY(members.comicInfo.opened());
        QCOMPARE(members.entry.sidecar, Sidecar);
        QCOMPARE(members.comicInfo.sidecar,
                 QByteArray("<ComicInfo><Number>7</Number></ComicInfo>"));
    }

    /// A member too large is that member's trouble: the other is a perfectly good sidecar
    /// and refusing it too would throw away what the proposal could have rested on.
    void a_comic_info_too_large_troubles_only_itself()
    {
        const Cbz::Members members = Cbz::membersOf(wrote(anArchive({
            {"entry.json", Sidecar, true, {}},
            {"ComicInfo.xml", QByteArray(Cbz::MostSidecarBytes + 1, 'x'), true, {}},
        })));

        QCOMPARE(members.comicInfo.trouble, Words::sidecarTooBig());
        QVERIFY(members.comicInfo.sidecar.isEmpty());
        QVERIFY(members.entry.opened());
        QCOMPARE(members.entry.sidecar, Sidecar);
    }

    /// An archive that will not open has not opened for either member, and the screen says
    /// so once for each, in the same words.
    void an_archive_that_will_not_open_says_so_for_both()
    {
        const Cbz::Members members = Cbz::membersOf(wrote("Ceci n’est pas un zip"));

        QCOMPARE(members.entry.trouble, Words::notAnArchive());
        QCOMPARE(members.comicInfo.trouble, Words::notAnArchive());
    }

    /// The server takes the first row the catalogue lists; a second reading that took the
    /// last would propose a different place than the server then files the volume in.
    void the_first_comic_info_listed_wins_as_the_server_reads_it()
    {
        const Cbz::Members members = Cbz::membersOf(wrote(anArchive({
            {"a/ComicInfo.xml", "<first/>", true, {}},
            {"b/comicinfo.XML", "<second/>", true, {}},
        })));

        QCOMPARE(members.comicInfo.sidecar, QByteArray("<first/>"));
        QVERIFY(members.entry.opened());
        QVERIFY(members.entry.sidecar.isEmpty());
    }

    /// An archive declared a book by its own `oneshot.json` read as an ordinary volume,
    /// because only two members were ever looked for: the third has to come out of the
    /// same single reading as the other two, not out of a second open of the catalogue.
    void a_one_shot_declaration_is_read_with_the_other_two_in_one_pass()
    {
        const QByteArray oneShot = R"({"leaf":1,"title":"Le Livre"})";
        const Cbz::Members members = Cbz::membersOf(wrote(anArchive({
            {"001.jpg", aPage(), false, {}},
            {"entry.json", Sidecar, true, {}},
            {"scans/OneShot.JSON", oneShot, true, {}},
            {"ComicInfo.xml", "<ComicInfo/>", true, {}},
        })));

        QVERIFY(members.oneShot.opened());
        QCOMPARE(members.oneShot.sidecar, oneShot);
        QCOMPARE(members.entry.sidecar, Sidecar);
        QCOMPARE(members.comicInfo.sidecar, QByteArray("<ComicInfo/>"));
    }

    /// A one-shot declaration past the ceiling is that member's trouble: the entry beside it
    /// is a good sidecar and must not be thrown away with it.
    void a_one_shot_too_large_troubles_only_itself()
    {
        const Cbz::Members members = Cbz::membersOf(wrote(anArchive({
            {"entry.json", Sidecar, true, {}},
            {"oneshot.json", QByteArray(Cbz::MostSidecarBytes + 1, 'x'), true, {}},
        })));

        QCOMPARE(members.oneShot.trouble, Words::sidecarTooBig());
        QVERIFY(members.oneShot.sidecar.isEmpty());
        QVERIFY(members.entry.opened());
        QCOMPARE(members.entry.sidecar, Sidecar);
    }

    /// An archive that will not open has not opened for any of the three, and the screen
    /// says so once for each, in the same words.
    void an_archive_that_will_not_open_says_so_for_all_three()
    {
        const Cbz::Members members = Cbz::membersOf(wrote("Ceci n’est pas un zip"));

        QCOMPARE(members.entry.trouble, Words::notAnArchive());
        QCOMPARE(members.comicInfo.trouble, Words::notAnArchive());
        QCOMPARE(members.oneShot.trouble, Words::notAnArchive());
    }

    /// The helper the next tests build their volumes with is itself read back whole.
    void a_volume_built_for_the_tests_gives_back_what_it_was_given()
    {
        const Cbz::Members members =
            Cbz::membersOf(wrote(Zip::volume(Sidecar, "<ComicInfo/>")));

        QCOMPARE(members.entry.sidecar, Sidecar);
        QCOMPARE(members.comicInfo.sidecar, QByteArray("<ComicInfo/>"));
    }
};

QTEST_MAIN(OpensAnArchive)
#include "opens_an_archive.moc"
