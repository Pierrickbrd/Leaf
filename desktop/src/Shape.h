#pragma once

// What the shape of a dropped folder suggests each folder is, when nothing says it.
//
// The scan stopped guessing on 21/09: a folder that only holds folders is a shelf there, never
// a universe, because the guess was "most often wrong". `Dragon Ball/{Perfect, Original}` and
// `Terres d’Arran/{Elfes, Mages}` are the same shape on disk and opposite in meaning. So the
// guess lives here, in the import only, as a suggestion with its confidence, its clues and the
// other reading — and what is confirmed is written as a sidecar, so the scan reads a
// declaration afterwards and never a shape.
//
// Names only: no archive is opened, so the roles are on screen as the folder lands. The
// numbers come from `Manifest::numberOf`, the server's own reading of a file name.

#include "Manifest.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Shape {

/// The model's levels and nothing else — `scan/layout.rs` for a folder, the scanner for an
/// archive. There is no arc: an arc is a range a sidecar declares, never a folder.
enum class Role { Shelf, Universe, Work, Edition, Volume, Book, Unread };

/// Said : un sidecar ou oneshot.json ; Evident : des archives posées là (la règle du scan) ;
/// Guessed : la forme ; Chosen : un clic.
enum class Origin { Said, Evident, Guessed, Chosen };

struct Choice {
    Role role = Role::Shelf;
    Origin origin = Origin::Guessed;
};

/// By `at` relative to the drop: a folder's `Node::at`, an archive's `Entry::path`.
using Roles = QHash<QString, Choice>;

enum class Confidence { Certain, Likely, ToConfirm };

/// One line of « Pourquoi » : ✓ a fact that points to the proposed reading, ✕ what that
/// reading sets aside — the clue the other one would need and does not have, or a reservation
/// about this one (« Vrac » is no title).
struct Clue {
    bool forIt = true;
    QString text;
};

/// One way of reading a folder, and what it would make of the drop.
struct Reading {
    /// The folder and every folder under it. Archives are left out: their role follows from
    /// their folder's, and two readings that differ on a folder differ on nothing else.
    QHash<QString, Role> roles;
    int universes = 0;
    int works = 0;
    int editions = 0;
    /// Archives this reading files somewhere — not those it leaves at a universe's root.
    qint64 volumes = 0;
};

struct Suggestion {
    QString at;
    Confidence confidence = Confidence::ToConfirm;
    /// For the first reading.
    QList<Clue> clues;
    /// [0] the one proposed, [1] the other when there is one.
    QList<Reading> readings;
    /// A generic name — « Vrac », « Mangas » — on what is read as a series: « nom à donner ».
    bool titleMissing = false;
};

struct Drop {
    quint64 token = 0;
    QString path;
    Manifest::Node whole;
    Roles roles;
    /// On the folders whose role a reading decides — never on a declared one, nor on an
    /// edition whose role follows from its series' reading.
    QHash<QString, Suggestion> suggestions;
    /// Where each folder of `whole` sits, by its `at`: the child indices from the root. Made
    /// by the first lookup and copied with the drop — a card looked up by walking the tree
    /// from the root walked half the drop for each card. `whole` is never changed once the
    /// index is made: a tree put in its place would be read through indices into the old one.
    mutable QHash<QString, QList<qsizetype>> places;
};

/// The most series a folder holds and still reads as a universe. Past it, a publisher, a
/// collection or a whole library is likelier than one world shared by nine series.
constexpr int MostUniverseSeries = 8;

/// `Manifest::whole` and `suggest`; the token is left at 0 for the caller to give.
Drop read(const QString &path);

/// Fills `roles` (Said, Evident, Guessed) and `suggestions` from `whole`.
void suggest(Drop &drop);

/// A name without case or accents: « INTÉGRALE », « Intégrale » and « integrale » are one. What
/// `isEditionWord` compares with, offered for a comparison of names that must agree with it.
QString folded(const QString &text);

/// Whether a folder's name carries a known edition word, without case, without accents and as
/// a whole word.
bool isEditionWord(const QString &folderName);

/// Whether a folder's name is one people give a place they put things away, not a title.
bool isGenericName(const QString &folderName);

/// One line of the role menu.
struct Option {
    Role role = Role::Shelf;
    bool possible = true;
    /// What the role is when it is possible, why not when it is not — a role greyed with no
    /// reason reads as a menu that is broken.
    QString detail;
};

/// The roles the menu offers at `at`, a folder's or an archive's, in the order it shows them:
/// Série, Édition, Univers, Étagère, then Livre seul greyed, on a folder; Livre seul, Tome
/// d’une série, Non lu on an archive at a universe's root; Tome, Livre seul in a series —
/// then the folder roles, greyed. Empty for an `at` the drop does not hold.
///
/// What is possible is what the model can hold with the roles the drop has **now**: the
/// shelf holding Terres d’Arran is no universe while Terres d’Arran is one, and becomes one
/// once it is not. Nothing here moves a file (no « Édition d’une série voisine », no « Le
/// ranger dans une série »), and a declared folder keeps the role its sidecar gives it.
QList<Option> options(const Drop &drop, const QString &at);

/// Gives `at` the role `role`, `Chosen`, and reads again what sits under it by the rule of
/// its new parent — except a role chosen below that still fits, which is somebody's and is
/// kept. False, and nothing changed, when `options` greys that role.
bool apply(Drop &drop, const QString &at, Role role);

/// The roles of `suggestions[at].readings[index]`: the folder's, `Chosen`, and the folders
/// under it as the reading made them, folders chosen below included — the reading is a
/// choice about the whole folder. Archives are not in a reading: a role chosen on one is kept
/// while it still fits, as `apply` keeps it. False, and nothing changed, when there is no
/// such reading or its role is greyed.
bool applyReading(Drop &drop, const QString &at, int index);

/// The `at` of each card: every universe, series or edition nothing above holds, in the order
/// `Manifest::found` walks — breadth first, shelves walked through.
QStringList cards(const Drop &drop);

/// The tree of one card, rooted at `""` the way `Imports::offer` roots it, every `at` and
/// file path rebased, and each universe, series or edition at the level its role says rather
/// than the `Work` an undeclared folder falls back to.
///
/// A folder out of the model inside the card — role Shelf, under an edition or a series —
/// keeps the node's own level, `Work` for an undeclared folder: `Manifest::Level` has no level
/// for « nothing here is read », and adding one would reach every screen that draws a level.
/// What it is comes from `rolesOf`, which says Shelf, and its archives Unread.
Manifest::Node cardNode(const Drop &drop, const QString &card);

/// The roles under one card, rebased like `cardNode`.
Roles rolesOf(const Drop &drop, const QString &card);

/// Why the card cannot be sent as it stands — a declaration the nearest declaration above it
/// leaves no room for, which the server refuses at the announcement: nothing inside an
/// edition, only an edition inside a series, no universe inside a universe — as a sentence
/// from `Words`; empty when nothing stops it. Both are sidecars on the disk, which no role
/// chosen on the folders between changes: the card is stopped rather than greyed.
QString outOfPlace(const Drop &drop, const QString &card);

/// What finding cards in a drop cost, for a test to assert on, as `Review::cost` does: the
/// folders and roles looked at to give one card its tree and its roles. Both are asked once
/// per card at every drop and every role changed, and a walk of the whole drop per card is
/// right and quadratic — measured, 9,6 s for a thousand universes.
struct Cost {
    qint64 looks = 0;
};
Cost cost();

} // namespace Shape
