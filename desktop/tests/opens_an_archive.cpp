// The brick the import stands on: what a CBZ says about itself.
//
// Tested against archives built here, byte by byte, rather than against a fixture checked
// into the tree. A zip is a format with an end record, a catalogue and a header per member,
// and the failures worth covering are exactly the ones a fixture cannot express: a member
// stored rather than deflated, a local header whose extra field is not the catalogue's, a
// comment that happens to carry the end signature, an archive that is not one at all.

#include "Cbz.h"

#include <QBuffer>
#include <QByteArray>
#include <QTemporaryDir>
#include <QtEndian>
#include <QTest>

#include <zlib.h>

using namespace Qt::StringLiterals;

namespace {

void appendTwo(QByteArray &to, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    to.append(bytes, 2);
}

void appendFour(QByteArray &to, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    to.append(bytes, 4);
}

QByteArray deflated(const QByteArray &whole)
{
    QByteArray out(compressBound(uLong(whole.size())) + 64, Qt::Uninitialized);
    z_stream stream {};
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                 Z_DEFAULT_STRATEGY);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(whole.constData()));
    stream.avail_in = uInt(whole.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    deflate(&stream, Z_FINISH);
    out.resize(qsizetype(stream.total_out));
    deflateEnd(&stream);
    return out;
}

/// One member of the archive under construction.
struct Member {
    QByteArray name;
    QByteArray content;
    bool packed = true;
    /// The local header's extra field. Deliberately its own: real archives put alignment
    /// padding here and nowhere else, and a reader that takes the catalogue's length
    /// instead lands in the middle of the data.
    QByteArray localExtra;
};

/// A zip, written out by hand. Everything the reader relies on is a decision made here.
QByteArray anArchive(const QList<Member> &members, const QByteArray &comment = {})
{
    QByteArray file;
    QList<quint32> offsets;
    QList<QByteArray> stored;

    for (const Member &member : members) {
        offsets << quint32(file.size());
        const QByteArray body = member.packed ? deflated(member.content) : member.content;
        stored << body;

        appendFour(file, 0x04034b50);
        appendTwo(file, 20);
        appendTwo(file, 0);
        appendTwo(file, member.packed ? 8 : 0);
        appendTwo(file, 0);
        appendTwo(file, 0);
        appendFour(file, 0);
        appendFour(file, quint32(body.size()));
        appendFour(file, quint32(member.content.size()));
        appendTwo(file, quint16(member.name.size()));
        appendTwo(file, quint16(member.localExtra.size()));
        file.append(member.name);
        file.append(member.localExtra);
        file.append(body);
    }

    const quint32 catalogueAt = quint32(file.size());
    for (int i = 0; i < members.size(); ++i) {
        appendFour(file, 0x02014b50);
        appendTwo(file, 20);
        appendTwo(file, 20);
        appendTwo(file, 0);
        appendTwo(file, members.at(i).packed ? 8 : 0);
        appendTwo(file, 0);
        appendTwo(file, 0);
        appendFour(file, 0);
        appendFour(file, quint32(stored.at(i).size()));
        appendFour(file, quint32(members.at(i).content.size()));
        appendTwo(file, quint16(members.at(i).name.size()));
        appendTwo(file, 0);
        appendTwo(file, 0);
        appendTwo(file, 0);
        appendTwo(file, 0);
        appendFour(file, 0);
        appendFour(file, offsets.at(i));
        file.append(members.at(i).name);
    }
    const quint32 catalogueSize = quint32(file.size()) - catalogueAt;

    appendFour(file, 0x06054b50);
    appendTwo(file, 0);
    appendTwo(file, 0);
    appendTwo(file, quint16(members.size()));
    appendTwo(file, quint16(members.size()));
    appendFour(file, catalogueSize);
    appendFour(file, catalogueAt);
    appendTwo(file, quint16(comment.size()));
    file.append(comment);
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
};

QTEST_MAIN(OpensAnArchive)
#include "opens_an_archive.moc"
