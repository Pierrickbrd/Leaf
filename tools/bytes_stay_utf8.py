#!/usr/bin/env python3
"""Refuses the word Latin1 anywhere in the client.

Every literal in this client is `u"…"_s` — UTF-16, which carries a title in any script — so
there is no safe Latin-1 left to tell apart from an unsafe one, and the rule has no
exceptions. What no compiler can refuse is the mistake spelled out, because these are
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
The name is not: `Latin1` appears only where somebody asked for Latin-1, and nobody here has
a reason to. This includes `toLatin1`, which turns whatever it has no room for into a literal
'?' — so that Haikyū becomes Haiky?.

    tools/bytes_stay_utf8.py

Exits non-zero, listing every place with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOOKED_AT = [ROOT / "desktop" / "src", ROOT / "desktop" / "tests", ROOT / "desktop" / "qml"]

# The whole identifier, not the word: there is no word boundary between the Q and the L of
# QLatin1String, so a pattern anchored with \b matches QStringConverter::Latin1 and sails
# straight past QLatin1String("é") — which is the case that matters most.
#
# `_L1` too, and it has to be spelled out: there is no "Latin1" inside it, so the pattern
# above sails straight past `u"é"_L1`. That door used to be closed by a deleted operator in
# Ascii.h; the file is gone, and this line is what stands in its place.
ANY = re.compile(r"\w*Latin1\w*|(?<![\w])_L1\b")


def files_to_read():
    """Every source file the rule covers, in a stable order."""
    for place in LOOKED_AT:
        if not place.is_dir():
            continue
        for path in sorted(place.rglob("*")):
            if path.suffix in {".h", ".cpp", ".qml"}:
                yield path


def asks_for_latin1(path):
    """Every place in one file that names Latin-1."""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        for hit in ANY.finditer(line):
            yield number, hit.group(0), line.strip()


def main() -> int:
    found = [
        (path.relative_to(ROOT), number, word, line)
        for path in files_to_read()
        for number, word, line in asks_for_latin1(path)
    ]

    for path, number, word, line in found:
        print(f"✗ {path}:{number} — {word}\n    {line}")

    if found:
        print(f"\n{len(found)} place(s) asking for Latin-1. Every literal here is u\"…\"_s,"
              "\nwhich carries any script; toUtf8 for bytes that have to leave.")
        return 1

    print("Latin-1 appears nowhere in the client.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
