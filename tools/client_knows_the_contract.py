#!/usr/bin/env python3
"""Checks that the desktop client still knows every field the contract declares.

`conformance.py` checks a running server against the contract. This checks the other side,
and without running anything: the contract says what a Series is, and `desktop/src/Api.cpp`
says what the client reads. Nothing keeps those two in step on its own.

The types are hand-written rather than generated — twenty fields did not justify a code
generator in the build. This is the price of that decision, and it is a cheap one: add a
field to the contract without teaching the client, and CI says so by name.

Two kinds of schema, because the client's interest in them differs:

  whole  every property must be read. These are the ones the client owns end to end, so a
         new field is a new fact somebody meant to show and the client is now hiding.
  part   the client takes a slice on purpose — the resume band wants three things out of an
         Entry, not its ISBN. Nothing fails; what is unread is listed, so the slice stays a
         decision rather than a drift.

    tools/client_knows_the_contract.py

Exits non-zero on the first schema that has fallen behind.
"""

import re
import sys
from pathlib import Path

import yaml

# Schema -> (depth, the functions allowed to satisfy it).
#
# Scoped to functions on purpose. Searching the whole file would count `Chapter.number` as
# read because `Entry.number` is, three lines away — a guard a name collision can fool is
# not a guard.
WATCHED = {
    "Series": ("whole", ["series"]),
    "SeriesPage": ("whole", ["page"]),
    "Facets": ("whole", ["facets"]),
    "Facet": ("whole", ["facetsUnder"]),
    "UpNext": ("whole", ["upNext"]),
    "Entry": ("part", ["upNext"]),
    "Progress": ("part", ["upNext"]),
    "Chapter": ("part", ["upNext"]),
}

ROOT = Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "contract" / "openapi.yaml"
SOURCE = ROOT / "desktop" / "src" / "Api.cpp"

# `field.text("workId"_ascii)` and friends. The name is what sits inside the quotes, and it is
# the only thing here that has to match the contract.
#
# This suffix was `_L1` until the Latin-1 guard renamed it, and this line was not renamed with
# it: the guard went on running and quietly matched nothing, so every field looked unread. A
# guard that can silently see nothing is worth no more than no guard, which is why `body_of`
# now refuses a function it reads nothing out of.
READS = re.compile(r'"([A-Za-z][A-Za-z0-9]*)"_ascii')


def body_of(source: str, name: str) -> str:
    """The braces of one function, found by counting them from its opening one."""
    start = re.search(rf"\b{re.escape(name)}\s*\([^)]*\)\s*\n?\{{", source)
    if start is None:
        raise SystemExit(f"cannot find {name}() in {SOURCE} — this guard needs updating")
    depth, i = 0, start.end() - 1
    while i < len(source):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start.end():i]
        i += 1
    raise SystemExit(f"{name}() has unbalanced braces")


def main() -> int:
    contract = yaml.safe_load(CONTRACT.read_text(encoding="utf-8"))
    schemas = contract["components"]["schemas"]
    source = SOURCE.read_text(encoding="utf-8")
    within = {fn: set(READS.findall(body_of(source, fn)))
              for fn in {fn for _, fns in WATCHED.values() for fn in fns}}

    # A function that parses a schema reads at least one field out of it. None at all does not
    # mean the client forgot everything — it means this script stopped recognising how fields
    # are written, and is about to report every schema as broken for the wrong reason.
    blind = sorted(fn for fn, names in within.items() if not names)
    if blind:
        print(f"✗ this guard is broken, not the client: it read no field at all out of "
              f"{', '.join(blind)}.\n  {READS.pattern} no longer matches how Api.cpp names "
              f"a field.")
        return 2

    behind = False
    for name, (depth, functions) in WATCHED.items():
        read = set().union(*(within[fn] for fn in functions))
        schema = schemas.get(name)
        if schema is None:
            print(f"✗ {name} — the contract no longer has this schema")
            behind = True
            continue

        declared = list((schema.get("properties") or {}).keys())
        unread = [field for field in declared if field not in read]

        if depth == "whole" and unread:
            print(f"✗ {name} — declared but never read: {', '.join(unread)}")
            behind = True
        elif depth == "whole":
            print(f"✓ {name} — all {len(declared)} fields read")
        else:
            taken = len(declared) - len(unread)
            print(f"· {name} — {taken}/{len(declared)} read on purpose"
                  + (f", left: {', '.join(unread)}" if unread else ""))

    if behind:
        print("\nThe contract moved and the client did not. Teach Api.cpp the new fields, or"
              "\nmove the schema to `part` here and say in a comment why the slice is right.")
        return 1

    print("\nThe client knows the contract.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
