#!/usr/bin/env python3
"""Refuses a `TapHandler` that does not take the press for itself.

A `TapHandler` left at its default policy takes only a *passive* grab when the press comes
down, and a passive grab does not stop the press. It goes on to whatever is drawn
underneath, and a `TapHandler` there taps as well when the button comes up. So a click on
the heading of the filter panel, open over the shelf, folded that heading **and opened the
series whose cover was behind it**: the page changed under the panel somebody was using.
The panel's own padding did not do it, which is why it looked like a fault of one control
rather than of every one.

`gesturePolicy: TapHandler.ReleaseWithinBounds` takes the press exclusively, so it stops
where it landed — and it is also what a button means: pressed here, released here. Every
handler in the client says so. This guard is what keeps the twenty-ninth from being written
the default way: nothing fails until something happens to be drawn over something else,
and then only for the hand that clicks on that exact spot.

**It refuses to see nothing.** A run that matched no `TapHandler` at all is a broken guard
and says so, rather than passing in silence.

    tools/taps_stay_where_they_land.py

Exits non-zero, naming every handler with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QML = ROOT / "desktop" / "qml"

OPENS = re.compile(r"^\s*TapHandler\s*\{")
EXCLUSIVE = re.compile(r"gesturePolicy\s*:\s*TapHandler\.(ReleaseWithinBounds|WithinBounds)\b")


def own(lines: list[str], start: int) -> str:
    """The handler's block, from its opening line to the brace that closes it."""
    depth = 0
    body: list[str] = []
    for line in lines[start:]:
        body.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            break
    return "\n".join(body)


def handlers(file: Path) -> list[tuple[int, str]]:
    lines = file.read_text(encoding="utf-8").splitlines()
    return [(i + 1, own(lines, i)) for i, line in enumerate(lines) if OPENS.match(line)]


def main() -> int:
    files = sorted(QML.glob("*.qml"))
    seen = [(f, at, body) for f in files for at, body in handlers(f)]

    if not seen:
        print(
            "✗ this guard is broken, not the client: it matched no TapHandler at all in "
            f"{QML.relative_to(ROOT)}. Something moved, and a guard that sees nothing "
            "passes everything.",
            file=sys.stderr,
        )
        return 2

    passive = [(f, at) for f, at, body in seen if not EXCLUSIVE.search(body)]

    if not passive:
        print(
            f"✓ {len(seen)} TapHandler(s) across {len(files)} file(s) — every one takes the "
            "press for itself"
        )
        return 0

    for file, at in passive:
        print(
            f"✗ {file.relative_to(ROOT)}:{at} — a passive tap. The press goes on to what is "
            "drawn underneath: add `gesturePolicy: TapHandler.ReleaseWithinBounds`.",
            file=sys.stderr,
        )
    print(
        f"\n{len(passive)} handler(s) that let a click through. Over a cover, the cover taps "
        "too.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
