#pragma once

// A Latin-1 literal that cannot hold anything Latin-1 would mangle.
//
// The trap this closes, measured rather than imagined: these sources are UTF-8, so "é" is two
// bytes, 0xC3 0xA9. Qt's own Latin-1 literal suffix says "read these bytes as Latin-1", and
// Latin-1 has a character at each of them — so the string becomes "Ã©" and nothing complains.
// It is a valid string, simply the wrong one, which is why no test at run time can be relied
// on to catch it: it would have to be a test of every literal ever written.
//
// The other doors turned out to be sound. qEnvironmentVariable, QSettings in INI format and
// qPrintable were all checked with "Yūsei — l'été" under LC_ALL=C, and all three came back
// byte-perfect: Qt 6 fixes UTF-8 as the system encoding on Unix rather than following the
// locale. The literal was the only leak.
//
// Three things enforce the rule, and none is a convention anybody has to remember:
//
//   · _ascii is consteval, so a non-ASCII byte is a compile error naming the literal;
//   · Qt's own _L1 is trapped below, so it fails whatever a file does with the namespace;
//   · bytes_stay_utf8.py catches the spellings a compiler cannot — QLatin1String("é") written
//     out in full, and toLatin1(), which turns anything it has no room for into a '?'.
//
// Use _ascii for what is genuinely ASCII and has to stay bytes: JSON field names, the
// contract's vocabulary, header names. Use u"…"_s for anything a person will read.

#include <QLatin1StringView>

#include <cstddef>

consteval QLatin1StringView operator""_ascii(const char *text, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
        if (static_cast<unsigned char>(text[i]) >= 0x80)
            // Nobody sees this at run time. Reaching a throw inside a consteval function is
            // what makes the call ill-formed, and the compiler quotes the line back.
            throw "not ASCII — Latin-1 would mangle it. Use u\"…\"_s for text people read.";

    return QLatin1StringView(text, static_cast<qsizetype>(size));
}

/// Qt's Latin-1 suffix, taken out of reach — and out of reach in both directions.
///
/// Importing operator""_s by name instead of opening Qt::Literals::StringLiterals is what
/// leaves Qt's version undeclared, but that is a habit, and a habit lasts until the first
/// file written in a hurry. With this here both roads are closed: on its own it is the only
/// candidate and the call is a use of a deleted function, and a file that opens the namespace
/// anyway makes the call ambiguous — which is also an error. Verified both ways.
///
/// Deleted rather than a consteval that always throws, which is what this was first: a
/// consteval body that can never be a constant expression is ill-formed at the *definition*,
/// so -Winvalid-constexpr rejected the guard itself and took the build with it.
///
/// Reach for _ascii instead when the bytes are ASCII, or u"…"_s for text people read.
QLatin1StringView operator""_L1(const char *, std::size_t) = delete;
