// This file must not compile. That is its whole job.
//
// The Latin-1 guard in `Ascii.h` is a compile-time one, so nothing at run time can show that
// it still works — and a guard nobody can see working is a guard that quietly stops working.
// `refuses_latin1.sh` builds this and insists both that it failed and that it failed for the
// right reason.
//
// It is EXCLUDE_FROM_ALL, so an ordinary build never touches it.

#include "Ascii.h"

int main()
{
    // "Comédie" is seven characters and eight bytes. Read as Latin-1 it would become
    // "ComÃ©die", silently, which is exactly what the guard exists to prevent.
    constexpr auto mangled = "Comédie"_ascii;
    return static_cast<int>(mangled.size());
}
