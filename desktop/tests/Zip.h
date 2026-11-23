#pragma once

// A zip written out by hand, shared by every test that needs an archive to read.
//
// Built here, byte by byte, rather than checked in as a fixture: the failures worth covering
// are the ones a fixture cannot express (a stored member, padding in a local header, a
// comment carrying the end signature), so each test says what its archive holds.

#include <QByteArray>
#include <QList>
#include <QtEndian>

#include <zlib.h>

namespace Zip {

inline void appendTwo(QByteArray &to, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    to.append(bytes, 2);
}

inline void appendFour(QByteArray &to, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    to.append(bytes, 4);
}

inline QByteArray deflated(const QByteArray &whole)
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
inline QByteArray anArchive(const QList<Member> &members, const QByteArray &comment = {})
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

/// A volume as the importer meets one: a page, stored, a few bytes long, and the sidecars
/// that are given. A sidecar left empty is left out, because an archive that holds none is
/// the case worth building, not an empty file named like one.
inline QByteArray volume(const QByteArray &entryJson, const QByteArray &comicInfo = {},
                         const QByteArray &oneShot = {})
{
    QList<Member> members;
    members << Member{"001.jpg", QByteArray("\xFF\xD8\xFF page", 8), false, {}};
    if (!entryJson.isEmpty())
        members << Member{"entry.json", entryJson, true, {}};
    if (!comicInfo.isEmpty())
        members << Member{"ComicInfo.xml", comicInfo, true, {}};
    if (!oneShot.isEmpty())
        members << Member{"oneshot.json", oneShot, true, {}};
    return anArchive(members);
}

} // namespace Zip
