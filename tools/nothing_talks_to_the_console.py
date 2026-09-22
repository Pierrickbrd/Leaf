#!/usr/bin/env python3
"""Refuses a debugging line left in any of the three blocks.

A debugging line is written to be read once and deleted the same minute. One was not: it was
added to find why a QML binding evaluated to false, the removal missed it because the lines
around it had moved, and it was committed, built, and printed into the terminal of the person
running the application — where it read as something the program had to say.

    qml: SONDE stage -> 1 moving -> false

What this refuses, and what it leaves alone, is the whole of the rule:

    qml       console.log, console.warn, console.debug, anything on console
    desktop   qDebug, qInfo
    server    dbg!, and println!/eprintln! outside main.rs

**`qWarning` and `qCritical` stay.** They are how this client says a singleton could not be
resolved or an answer could not be read — diagnostics the program means to emit, written for
somebody reading a journal. A rule that swept them away would be a rule with exceptions, and
a rule with exceptions is a judgement somebody has to make every time.

`println!` is the same distinction one language over: `main.rs` is a command-line program and
says its version, its usage and what a scan found. Nowhere else has a terminal to write to.

**It refuses to see nothing.** Each of the three blocks must yield files; a path that stopped
matching would otherwise pass in silence and go on passing — which is what
`client_knows_the_contract.py` was doing the first time its own regex was renamed out from
under it.

**Both workflows reach it.** `ctest` runs it for a change under `desktop/`, and a change
under `server/` runs `tools/test_guards.py`, whose last case is this guard against the real
tree — so a `dbg!` added in a server-only pull request is refused by the server's own job
rather than waiting for somebody to touch the client.

    tools/nothing_talks_to_the_console.py

Exits non-zero, listing every place with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Block:
    """One block, the files it is read from, and what it may not say."""

    def __init__(self, name: str, where: Path, suffixes: tuple[str, ...], speaks: re.Pattern,
                 spared: tuple[str, ...] = ()):
        self.name = name
        self.where = where
        self.suffixes = suffixes
        self.speaks = speaks
        self.spared = spared

    def files(self) -> list[Path]:
        found = [f for s in self.suffixes for f in self.where.rglob(f"*{s}")]
        return sorted(f for f in found if f.name not in self.spared)


BLOCKS = [
    # The object and not one of its methods: `log`, `warn`, `debug`, `trace` and whatever Qt
    # adds next are the same mistake, and listing them by name lets the next one through.
    Block("qml", ROOT / "desktop" / "qml", (".qml",), re.compile(r"\bconsole\s*\.")),
    Block("desktop", ROOT / "desktop" / "src", (".cpp", ".h"),
          re.compile(r"\bq(?:Debug|Info)\s*\(")),
    # `main.rs` is a command-line program: it says its version, its usage, and what a scan
    # found. Nothing else in the server has a terminal to write to.
    Block("server", ROOT / "server" / "src", (".rs",),
          re.compile(r"\bdbg!|\b(?:println|eprintln)!"), spared=("main.rs",)),
]


def said(block: Block) -> list[tuple[Path, int, str]]:
    found: list[tuple[Path, int, str]] = []
    for file in block.files():
        for number, line in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
            if block.speaks.search(line):
                found.append((file, number, line.strip()))
    return found


def main() -> int:
    blind = [b.name for b in BLOCKS if not b.files()]
    if blind:
        print(
            "✗ this guard is broken, not the client: it read no file at all for "
            f"{', '.join(blind)}. Something moved, and a guard that sees nothing passes "
            "everything.",
            file=sys.stderr,
        )
        return 2

    spoke: list[tuple[Path, int, str]] = []
    read = 0
    for block in BLOCKS:
        read += len(block.files())
        spoke += said(block)

    if not spoke:
        print(f"✓ {read} file(s) across three blocks — none of them talks to the console")
        return 0

    for file, number, line in spoke:
        print(f"✗ {file.relative_to(ROOT)}:{number} — {line}", file=sys.stderr)
    print(
        f"\n{len(spoke)} debugging line(s). One of these reached the terminal of somebody "
        "running the application, where it read as something the program had to say.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
