#include "Cbz.h"
#include "Words.h"

#include <QCryptographicHash>
#include <QFile>
#include <QStringList>

#include <optional>
#include <QtEndian>

#include <zlib.h>

using namespace Qt::StringLiterals;

namespace {

/// The four signatures a zip is made of.
constexpr quint32 EndOfCatalogue = 0x06054b50;
constexpr quint32 CatalogueEntry = 0x02014b50;
constexpr quint32 LocalHeader = 0x04034b50;

/// A zip comment is a sixteen-bit length, so the end record sits within this of the end.
constexpr qint64 MostTrailing = 0xFFFF + 22;

/// The two methods a real CBZ uses. Anything else is a member this cannot read, which is
/// not the same as an archive it cannot open.
constexpr quint16 Stored = 0;
constexpr quint16 Deflated = 8;

/// Little-endian, everywhere, because that is what the format is — and read through
/// `qFromLittleEndian` rather than by casting a pointer, which would fault on a machine
/// that cares about alignment and read the bytes backwards on one that does not.
quint16 twoAt(const QByteArray &bytes, qint64 at)
{
    return qFromLittleEndian<quint16>(bytes.constData() + at);
}

quint32 fourAt(const QByteArray &bytes, qint64 at)
{
    return qFromLittleEndian<quint32>(bytes.constData() + at);
}

/// Raw deflate — `-MAX_WBITS`. A zip member carries no zlib header, so the usual positive
/// window size looks for two bytes that are not there and fails on every archive.
QByteArray inflated(const QByteArray &packed, quint32 expected)
{
    if (expected == 0)
        return {};

    QByteArray out(expected, Qt::Uninitialized);
    z_stream stream {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};

    // A copy, to hand zlib a pointer it may call its own. `z_stream::next_in` is not const,
    // though `inflate` never writes through it, and casting the const away would be a
    // promise this code cannot keep. The cost is bounded by what this reader is for: one
    // small member of an archive — a declaration of a few hundred bytes — never a page.
    QByteArray arriving = packed;
    stream.next_in = reinterpret_cast<Bytef *>(arriving.data());
    stream.avail_in = uInt(arriving.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());

    const int went = inflate(&stream, Z_FINISH);
    const uLong got = stream.total_out;
    inflateEnd(&stream);

    if (went != Z_STREAM_END || got != expected)
        return {};
    return out;
}

/// The catalogue's own record of where it is. A zip is read from its end: a member's own
/// header says nothing reliable about its size, and this record is the only place that
/// does.
struct Catalogue {
    qint64 at = -1;
    quint32 offset = 0;
    quint32 size = 0;
    quint16 count = 0;
};

/// Whether a candidate end record is the real one.
///
/// Four bytes are not enough to recognise it. The signature can fall anywhere — in a
/// comment, in compressed data — and a decoy can be a *valid* record announcing an empty
/// archive, which no length check rejects. The one thing a decoy cannot forge is the
/// catalogue itself: this follows the offset and requires a catalogue entry to be sitting
/// there.
bool leadsToACatalogue(QFile &file, const Catalogue &candidate)
{
    if (candidate.count == 0 || candidate.size < 46)
        return false;
    if (!file.seek(candidate.offset))
        return false;
    const QByteArray head = file.read(4);
    return head.size() == 4 && fourAt(head, 0) == CatalogueEntry;
}

Catalogue catalogueOf(QFile &file)
{
    const qint64 length = file.size();
    const qint64 window = qMin(length, MostTrailing);
    if (window < 22)
        return {};

    if (!file.seek(length - window))
        return {};
    const QByteArray tail = file.read(window);

    // Backwards, because the real record is the last thing in almost every archive, and
    // walking from the end finds it in a handful of steps rather than across the whole
    // window. Each candidate is then followed before it is believed.
    Catalogue empty;
    for (qint64 at = tail.size() - 22; at >= 0; --at) {
        if (fourAt(tail, at) != EndOfCatalogue)
            continue;
        Catalogue found;
        found.at = length - window + at;
        found.count = twoAt(tail, at + 10);
        found.size = fourAt(tail, at + 12);
        found.offset = fourAt(tail, at + 16);
        // The comment it declares has to reach exactly the end of the file.
        if (found.at + 22 + twoAt(tail, at + 20) != length)
            continue;
        if (leadsToACatalogue(file, found))
            return found;
        // An archive with no members at all is legitimate and leads nowhere. Kept aside
        // rather than returned, so that a decoy claiming emptiness cannot outrank a real
        // record further up the file.
        if (found.count == 0 && found.size == 0 && empty.at < 0)
            empty = found;
    }
    return empty;
}

bool namedLike(const QByteArray &name, const QString &wanted)
{
    const QString whole = QString::fromUtf8(name);
    const qsizetype cut = whole.lastIndexOf(u'/');
    const QStringView last = cut < 0 ? QStringView(whole) : QStringView(whole).mid(cut + 1);
    return last.compare(wanted, Qt::CaseInsensitive) == 0;
}

/// One row of the catalogue, as this reader needs it.
struct Listed {
    quint16 method = 0;
    quint32 packedSize = 0;
    quint32 wholeSize = 0;
    quint32 where = 0;
};

/// The first row naming each of the three members, or nothing for one the archive lacks.
struct Rows {
    std::optional<Listed> entry;
    std::optional<Listed> comicInfo;
    std::optional<Listed> oneShot;
    int pages = 0;
    /// Every member that is not a folder, by name, when the walk was asked to keep them.
    QList<std::pair<QByteArray, Listed>> all;
};

/// Whether the last segment of a member's name ends in the extension of an image.
bool isImage(const QByteArray &name)
{
    static const QStringList extensions{u"jpg"_s, u"jpeg"_s, u"png"_s,
                                        u"webp"_s, u"gif"_s,  u"avif"_s};
    const QString whole = QString::fromUtf8(name);
    const qsizetype cut = whole.lastIndexOf(u'/');
    const QStringView last = cut < 0 ? QStringView(whole) : QStringView(whole).mid(cut + 1);
    const qsizetype dot = last.lastIndexOf(u'.');
    return dot > 0 && extensions.contains(last.mid(dot + 1).toString().toLower());
}

/// One walk of the catalogue for the three names.
///
/// The **first** of each, and the walk stops at the first row that is not a catalogue
/// entry: a truncated or lying catalogue is an archive without sidecars, not a reason to
/// fail. It goes on after all three are found, because the images are counted in the same
/// walk. The catalogue is already in memory, read in one piece, so the walk reads nothing
/// from the disk.
Rows rowsIn(const QByteArray &listing, quint16 count, bool keepAll)
{
    Rows rows;
    qint64 at = 0;
    for (quint16 seen = 0; seen < count; ++seen) {
        if (at + 46 > listing.size() || fourAt(listing, at) != CatalogueEntry)
            return rows;
        Listed row;
        row.method = twoAt(listing, at + 10);
        row.packedSize = fourAt(listing, at + 20);
        row.wholeSize = fourAt(listing, at + 24);
        const quint16 nameLength = twoAt(listing, at + 28);
        const quint16 extraLength = twoAt(listing, at + 30);
        const quint16 commentLength = twoAt(listing, at + 32);
        row.where = fourAt(listing, at + 42);
        const QByteArray name = listing.mid(at + 46, nameLength);
        at += 46 + nameLength + extraLength + commentLength;

        if (keepAll && !name.endsWith('/'))
            rows.all.append({name, row});

        if (!rows.entry && namedLike(name, u"entry.json"_s))
            rows.entry = row;
        else if (!rows.comicInfo && namedLike(name, u"comicinfo.xml"_s))
            rows.comicInfo = row;
        else if (!rows.oneShot && namedLike(name, u"oneshot.json"_s))
            rows.oneShot = row;
        else if (isImage(name))
            ++rows.pages;
    }
    return rows;
}

/// The member's own bytes, or nothing when its local header does not line up.
///
/// The local header repeats the name and carries its own extra field, whose length is its
/// own — taking the catalogue's would land in the middle of the data on the archives where
/// the two differ, which is most of them.
QByteArray memberAt(QFile &file, const Listed &row)
{
    if (!file.seek(row.where))
        return {};
    const QByteArray header = file.read(30);
    if (header.size() < 30 || fourAt(header, 0) != LocalHeader)
        return {};
    if (!file.seek(row.where + 30 + twoAt(header, 26) + twoAt(header, 28)))
        return {};

    const QByteArray packed = file.read(row.packedSize);
    return packed.size() == qint64(row.packedSize) ? packed : QByteArray();
}

/// What one member comes to: its bytes, or why they could not be had.
Cbz::Found readMember(QFile &file, const std::optional<Listed> &row)
{
    if (!row.has_value()) {
        // Opened, walked, and holding no such member. An ordinary file from somewhere else,
        // and not a failure: the proposal will rest on its name and its size.
        return {};
    }
    // Both figures come from the catalogue and are the writer's to choose: a member that is
    // small once unpacked and four gigabytes packed is not a sidecar either. Twice the
    // ceiling is room for a deflate that grew what it was given.
    if (row->wholeSize > Cbz::MostSidecarBytes || row->packedSize > 2 * Cbz::MostSidecarBytes)
        return {{}, Words::sidecarTooBig()};
    if (row->method != Stored && row->method != Deflated)
        return {{}, Words::sidecarCompressedInAnUnknownWay()};

    const QByteArray packed = memberAt(file, *row);
    // A catalogue that names a member the file does not hold where it says is an archive
    // without a usable sidecar, the same as one that names none.
    if (packed.isEmpty() && row->packedSize > 0)
        return {};

    const QByteArray json = row->method == Stored ? packed : inflated(packed, row->wholeSize);
    if (json.isEmpty() && row->wholeSize > 0)
        return {{}, Words::sidecarCouldNotBeInflated()};
    return {json, {}};
}

/// The SHA-256 of what a member decompresses to, or nothing when it cannot be had.
///
/// Inflated in blocks into the hash rather than into a buffer the size of the member: the
/// whole of a volume is hashed this way, and a page held twice over is memory spent for
/// nothing.
QString digestOf(QByteArray packed, const Listed &row)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (row.method == Stored || row.wholeSize == 0) {
        hash.addData(packed);
    } else if (row.method == Deflated) {
        z_stream stream {};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return {};
        stream.next_in = reinterpret_cast<Bytef *>(packed.data());
        stream.avail_in = uInt(packed.size());
        QByteArray block(1 << 16, Qt::Uninitialized);
        int went = Z_OK;
        while (went == Z_OK) {
            stream.next_out = reinterpret_cast<Bytef *>(block.data());
            stream.avail_out = uInt(block.size());
            went = inflate(&stream, Z_NO_FLUSH);
            hash.addData(QByteArrayView(block.constData(), block.size() - qsizetype(stream.avail_out)));
        }
        const uLong got = stream.total_out;
        inflateEnd(&stream);
        if (went != Z_STREAM_END || got != row.wholeSize)
            return {};
    } else {
        return {};
    }
    // `fromUtf8` on plain hex, as `Manifest::checksumOf` does.
    return QString::fromUtf8(hash.result().toHex());
}

/// Every listed member hashed, or nothing at all if one of them cannot be, and then `trouble`
/// says which way.
QList<Cbz::Digest> digestsOf(QFile &file, const Rows &rows, QString &trouble)
{
    QList<Cbz::Digest> digests;
    for (const auto &[name, row] : rows.all) {
        if (row.method != Stored && row.method != Deflated) {
            trouble = Words::memberCompressedInAnUnknownWay();
            return {};
        }
        const QByteArray packed = memberAt(file, row);
        const QString sum = (packed.isEmpty() && row.packedSize > 0) ? QString()
                                                                     : digestOf(packed, row);
        if (sum.isEmpty()) {
            trouble = Words::memberCouldNotBeRead();
            return {};
        }
        digests.append({QString::fromUtf8(name), qint64(row.wholeSize), sum});
    }
    return digests;
}

} // namespace

namespace Cbz {

Members membersOf(const QString &path, bool withDigests)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const Found trouble{{}, Words::couldNotBeOpened()};
        return {trouble, trouble, trouble, -1, {}, withDigests ? trouble.trouble : QString()};
    }

    const Catalogue catalogue = catalogueOf(file);
    if (catalogue.at < 0) {
        const Found trouble{{}, Words::notAnArchive()};
        return {trouble, trouble, trouble, -1, {}, withDigests ? trouble.trouble : QString()};
    }
    // Zip64 says so by filling these with ones, and its real figures live in a second
    // record this does not read. Said outright rather than read wrong: a comic volume is
    // never four gigabytes, and a file that gets here is offered without its sidecar.
    if (catalogue.count == 0xFFFF || catalogue.offset == 0xFFFFFFFF
        || catalogue.size == 0xFFFFFFFF) {
        const Found trouble{{}, Words::archiveTooBig()};
        return {trouble, trouble, trouble, -1, {}, withDigests ? trouble.trouble : QString()};
    }
    if (!file.seek(catalogue.offset)) {
        const Found trouble{{}, Words::catalogueMissing()};
        return {trouble, trouble, trouble, -1, {}, withDigests ? trouble.trouble : QString()};
    }

    const Rows rows = rowsIn(file.read(catalogue.size), catalogue.count, withDigests);
    QString digestTrouble;
    QList<Digest> digests;
    if (withDigests)
        digests = digestsOf(file, rows, digestTrouble);
    return {readMember(file, rows.entry), readMember(file, rows.comicInfo),
            readMember(file, rows.oneShot), rows.pages, digests, digestTrouble};
}

Found sidecarOf(const QString &path)
{
    return membersOf(path).entry;
}

} // namespace Cbz
