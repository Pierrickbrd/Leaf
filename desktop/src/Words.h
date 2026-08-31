#pragma once

// French, written properly, in one place.
//
// Three habits creep into every interface copied from an American one, and all three are
// visible the moment they are wrong and invisible when they are right:
//
//   · one initial capital per label, not one per word — « Non lues », never « Non Lues » ;
//   · a non-breaking space before `: ; ! ?` and inside « … » ;
//   · a curly apostrophe, and real accented capitals — « À compléter », « Édition Deluxe ».
//
// They are here rather than in QML because a string in a `.qml` file is a string nobody
// tests, and because the same words appear on three screens. Acronyms keep their case: BD,
// never "Bd" — which is why this cannot be a `toUpper` on the first letter and nothing else.

#include "Api.h"
#include "Navigation.h"
#include "Widths.h"

#include <QString>

#include <optional>

namespace Words {

/// A non-breaking space. French puts one before every two-part punctuation mark, and a normal
/// space lets the line break in front of the colon.
extern const QChar Nbsp;

/// "21 tomes", "7 albums", "1 tome". A BD comes in albums and everything else in volumes —
/// which is a fact about the medium, so it is read from the medium and not from the shelf.
QString volumes(int count, std::optional<Api::Medium> medium);

/// The pills, left to right: « Non lues », « En cours », « Terminées ».
QString readStatus(Api::ReadStatus status);

/// The pills, right: « Manga », « BD », « Comics ». The acronym stays an acronym.
QString medium(Api::Medium value);

/// « Trier : Nom » — with the space that will not break before the colon.
QString labelled(const QString &label, const QString &value);

/// « Tome 12 · Page 47/190 · Chapitre 98 ».
///
/// Each segment is its own label, so each begins with a capital. Segments with nothing to say
/// are left out rather than shown empty: a card reading "Page /" is worse than a shorter card.
QString where(Api::UpNext::Kind kind, std::optional<double> number, std::optional<int> page,
              int pageCount, const std::optional<QString> &chapter);

/// « aucun résultat dans manga · 3 sans les filtres » — the sentence under the field when a
/// search finds nothing behind a lit pill. Without it the screen just looks broken.
QString nothingHere(const QStringList &pills, int withoutThem);

/// The screen's own name — « Étagère », « Série », « Lecteur », « Santé », « Réglages » —
/// which is otherwise the one string this client would put in a `.qml` file and never test.
QString destination(Navigation::Destination value);

/// The band a window is in, said the way a person reads it — « Large », « Moyenne »,
/// « Étroite » — for the same reason `destination` exists: so nothing switches on the enum
/// from inside QML.
QString band(Widths::Band value);

} // namespace Words
