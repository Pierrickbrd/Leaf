#pragma once

// How a name is folded before two names are one, as the server folds it.

#include <QString>

namespace Fold {

/// A name as the server's `search_key` folds it before two names are one: accents and case
/// gone, an apostrophe joining what it stands between (« D’Aviau » is « daviau »), the
/// ligatures a keyboard spells out spelled out (« Cœur » is « coeur »), and every other run of
/// what is not a letter or a digit one space, none at either end. Whatever the client counts as
/// the server will — an author once, an order by its name — is compared with this and nothing
/// else: folded apart, « Terres d’Arran » was one order here and none there.
QString searchKey(const QString &text);

} // namespace Fold
