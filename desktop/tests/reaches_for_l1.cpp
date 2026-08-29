// This file must not compile either, and for its own reason.
//
// `Ascii.h` deletes Qt's Latin-1 literal suffix so that reaching for it is a build failure
// rather than a remark in review. The habit it replaces — importing operator""_s by name
// instead of opening the whole namespace — is only a habit, and a habit lasts until the
// first file written in a hurry.
//
// EXCLUDE_FROM_ALL: only `refuses_latin1.sh` ever builds this.

#include "Ascii.h"

int main()
{
    // Perfectly good ASCII. It is the suffix that is refused, not what is in the quotes.
    constexpr auto reached = "workId"_L1;
    return static_cast<int>(reached.size());
}
