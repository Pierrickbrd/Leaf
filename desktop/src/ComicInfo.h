#pragma once

// ComicInfo.xml, read the way the server reads it.
//
// The rules are those of `server/src/metadata/legacy_comic_info.rs`, which is the source of
// truth: the import shows what the server will keep, so a client that read the same file
// differently would announce one title and file another. Field by field:
//
//  * bytes that are not UTF-8 are unreadable, whatever the declaration says (checked on the
//    bytes, not on the `encoding=` attribute); so is a document that is not well formed;
//  * names are local and case-sensitive; the **first** occurrence of a tag wins;
//  * a value empty after trimming is not a value, and does not hold the place of a later one;
//  * only the root's direct children are fields (depth <= 1); a `<Name>` under a `<Chapter>`
//    at any depth is a chapter and never touches `Title` — read flat, a chapter's name
//    overwrote the volume's title;
//  * the text of a tag is accumulated and trimmed **once**: an entity splits the text in two
//    and trimming each half turned "The Knuckle & The Arrow" into "The Knuckle&The Arrow";
//  * `Number` reads `,` as `.` (the library has "56,5"); `Count` is an integer; `Genre` is cut
//    on `,`, trimmed, empties dropped; `Manga` containing `RightToLeft` says
//    "RIGHT_TO_LEFT" and anything else says nothing; the date is `Year`, then `Month`, then
//    `Day` when present, joined by `-` without padding;
//  * `Writer` is the author and `Penciller` the artist, read alike: the first occurrence, trimmed,
//    kept as the one text it is. Both are lists the server cuts on `,` ("Arleston, Tarquin" is
//    two people); `Facts` cuts them, so that this file keeps saying what the tag held;
//  * a document with no useful tag at all is nothing.
//
//  * CDATA sections are ignored, as the server ignores them; a leading byte-order mark is
//    stripped, as quick-xml does;
//  * the size ceiling is the caller's (`Cbz::MostSidecarBytes`, 4 MiB), not this file's.
//
// Two deliberate gaps with the server, both toward refusing more: a document carrying a DTD
// is unreadable (the server never expands one, but a client that streams the file to a
// general-purpose parser must not either — the "billion laughs" is ten lines), and an entity
// the document did not declare makes it unreadable (the server hands it back verbatim).
//
// One gap that is not a choice: Qt cannot tell `<B/>` from `<B></B>`, and the server's reader
// ignores an empty element without clearing the text gathered so far. So `<Series>S<br/></Series>`
// reads "S" there and nothing here. Not seen in a real file; pinned by a test so a change shows.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace ComicInfo {

struct Fields {
    // Proper to the volume.
    std::optional<double> number;
    QString title;
    QString isbn;
    QString publishedOn;
    QString summary;
    QStringList chapters; // the <Name> of <Chapters><Chapter>, in order
    // Proper to the series, which the format copies into every volume.
    QString series;
    QString publisher;
    QString language;
    QString readingDirection;
    QString author;
    QString artist;
    QString arc;
    std::optional<int> volumeCount;
    QStringList genres;
};

/// Nothing when the bytes are not UTF-8, the document is not XML, it carries a DTD or an
/// undeclared entity, or it holds no useful tag. `unreadable`, when given, says which of the
/// two nothing it was: a ComicInfo holding only its `<Pages>` was said « illisible » to a
/// reader looking at a perfectly good file.
std::optional<Fields> read(const QByteArray &xml, bool *unreadable = nullptr);

} // namespace ComicInfo
