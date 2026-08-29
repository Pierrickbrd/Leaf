#!/usr/bin/env python3
"""Refuses the word Latin1 in the client, except as the name of a parameter's type.

`Ascii.h` closes two doors at the compiler: `_ascii` is consteval and rejects a non-ASCII
byte whatever form it is written in — escaped, octal, raw, concatenated, the compiler sees
the bytes — and Qt's `_L1` is deleted, so reaching for it fails whether or not a file opens
the namespace. What no compiler can refuse is the same mistake spelled out, because these are
ordinary Qt constructors.

**This does not look at what is passed.** An earlier version did, and it was worthless: every
one of these got past it, and each is a thing somebody writes without meaning any harm.

    QLatin1String("\\xC3\\xA9")        the source is ASCII, the bytes are not
    QLatin1String("\\303\\251")        the same in octal
    QLatin1String(                    a call split over two lines
        "Comédie")
    QLatin1String("Com" "édie")       adjacent literals, only the first inspected
    QLatin1String(R"(été)")           a raw string
    QLatin1String(variable)           not a literal at all
    QStringDecoder(QStringConverter::Latin1)   a whole converter, never on the list

An argument has an unbounded number of shapes, so the argument is the wrong thing to check.
The name is not: `Latin1` appears in the client only where somebody has asked for Latin-1,
and the one honest reason to ask is to name the type of a parameter that carries ASCII —
`QLatin1StringView name`, which is what the JSON field readers take.

Everything else is refused, including `toLatin1`, which turns whatever it has no room for
into a literal '?' so that Haikyū becomes Haiky?. Write `_ascii` for bytes that are ASCII and
`u"…"_s` for text people read; `toUtf8` is right every time bytes have to leave.

    tools/bytes_stay_utf8.py

Exits non-zero, listing every place with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOOKED_AT = [ROOT / "desktop" / "src", ROOT / "desktop" / "tests", ROOT / "desktop" / "qml"]

# Where the guard itself lives. It is the one file that has to name what it is guarding.
SPARED = {ROOT / "desktop" / "src" / "Ascii.h"}

# The whole identifier, not the word: there is no word boundary between the Q and the L of
# QLatin1String, so a pattern anchored with \b matches QStringConverter::Latin1 and sails
# straight past QLatin1String("é") — which is the case that matters most.
ANY = re.compile(r"\w*Latin1\w*")

# `QLatin1StringView name` — a type, then an identifier, then the end of the declaration.
#
# The last part is not decoration. `QLatin1String sournois("é");` is also a type followed by
# an identifier, and it mangles: what makes a parameter harmless is that nothing is being
# built. So what may follow the name is a comma, a close bracket or a semicolon — never a
# `(`, a `{` or an `=`.
AS_A_TYPE = re.compile(r"^QLatin1String(?:View)?$")
A_DECLARATION = re.compile(r"\s+[A-Za-z_]\w*\s*(?:[,);]|$)")


def files_to_read():
    """Every source file the rule covers, in a stable order."""
    for place in LOOKED_AT:
        if not place.is_dir():
            continue
        for path in sorted(place.rglob("*")):
            if path.suffix in {".h", ".cpp", ".qml"} and path not in SPARED:
                yield path


def asks_for_latin1(path):
    """Every place in one file that names Latin-1 other than as a parameter's type."""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        for hit in ANY.finditer(line):
            word = hit.group(0)
            after = line[hit.end():]
            if AS_A_TYPE.match(word) and A_DECLARATION.match(after):
                continue  # the type of a parameter, carrying ASCII
            yield number, word, line.strip()


def main() -> int:
    found = [
        (path.relative_to(ROOT), number, word, line)
        for path in files_to_read()
        for number, word, line in asks_for_latin1(path)
    ]

    for path, number, word, line in found:
        print(f"✗ {path}:{number} — {word}\n    {line}")

    if found:
        print(f"\n{len(found)} place(s) asking for Latin-1. Use _ascii for ASCII bytes,"
              "\nu\"…\"_s for text people read, toUtf8 for bytes that have to leave.")
        return 1

    print("Latin-1 appears nowhere but as the type of a parameter.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
