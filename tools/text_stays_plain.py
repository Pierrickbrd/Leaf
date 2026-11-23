#!/usr/bin/env python3
"""Refuses a text of the client that does not say how it is read.

A `Text` with no `textFormat` is `Text.AutoText`, which reads its string as markup whenever
it looks like some. What the server sends is whatever somebody typed into a title, a summary,
a folder name, so a series called `<img src="http://…">` was a request to a stranger's
address made by the screen that listed it, and `<b>` was a title in bold. Two hundred and
four of the client's texts read their string that way until they were all given a format.

What is refused: a `Text`, `TextEdit` or `Label` block in `desktop/qml/` with no `textFormat`
among its own properties. Markup the application writes itself is declared where it is used
(`Text.StyledText`, `Text.RichText`), which is what makes it a decision somebody took and not
a default nobody noticed.

**It refuses to see nothing.** No QML file, or no text in them, and it says the guard is
broken, not the client — the rule `client_knows_the_contract.py` was written to follow.

    tools/text_stays_plain.py

Exits non-zero, listing every place with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QML = ROOT / "desktop" / "qml"

OPENING = re.compile(r"^[ \t]*(Text|TextEdit|Label)\s*\{", re.M)


def unformatted(source: str) -> list[tuple[int, str]]:
    """The `(line, type)` of every text block of `source` that names no `textFormat`."""
    found = []
    for opening in OPENING.finditer(source):
        depth, at = 1, opening.end()
        while depth and at < len(source):
            depth += (source[at] == "{") - (source[at] == "}")
            at += 1
        if "textFormat" not in source[opening.start():at]:
            found.append((source.count("\n", 0, opening.start()) + 1, opening.group(1)))
    return found


def texts(source: str) -> int:
    return len(OPENING.findall(source))


def main() -> int:
    files = sorted(QML.glob("*.qml"))
    read = sum(texts(f.read_text(encoding="utf-8")) for f in files)
    if not files or read == 0:
        print(
            "✗ this guard is broken, not the client: it read no text at all in "
            f"{QML.relative_to(ROOT)}. Something moved, and a guard that sees nothing "
            "passes everything.",
            file=sys.stderr,
        )
        return 2

    bad = [(f, line, kind) for f in files
           for line, kind in unformatted(f.read_text(encoding="utf-8"))]
    if not bad:
        print(f"✓ {read} text(s) in {len(files)} file(s) — each says how it is read")
        return 0

    for file, line, kind in bad:
        print(f"✗ {file.relative_to(ROOT)}:{line} — {kind} without textFormat", file=sys.stderr)
    print(
        f"\n{len(bad)} text(s) read their string as markup if it looks like some. Give each "
        "`textFormat: Text.PlainText`, or the format it is written in on purpose.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
