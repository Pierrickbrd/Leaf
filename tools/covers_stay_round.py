#!/usr/bin/env python3
"""Refuses a `Rectangle` that rounds its corners and clips to them.

`clip: true` clips an item to its **bounding box**, never to its `radius`. A picture laid in
a rounded rectangle therefore keeps its four square corners and paints them straight over the
rounding, and the hairline `CoverSkin` draws round the shape the rectangle *claims* ends up
outlining a shape that is not there — a rounded border with the illustration poking out at
every corner.

Six covers in this client were written that way and three of them shipped: the header of
every series page, the tiles of *Voir aussi*, the edition menu, and the row of the import
dialog. It is invisible on a cover whose own edges are dark, which is why it lasted — and
plain on every pale one, which is how it was finally seen, on a screenshot of *Fire Force*.

`RoundedCover.qml` is the answer: Qt 6.4 has no `MultiEffect`, so it masks with the
`ShaderEffectSource` + `OpacityMask` pair, once, in one file. This guard is what keeps the
seventh cover from being written the old way — the defect is silent, no test can see it, and
the eye only catches it on the right illustration.

**Only the rectangle's own properties count.** A `clip` belonging to a child nested inside it
is that child's business; reading the whole subtree would accuse every panel that happens to
contain a clipped list.

**It refuses to see nothing.** A run that matched no `Rectangle` at all is a broken guard and
says so, rather than passing in silence — which is what `client_knows_the_contract.py` did
the first time its own regex was renamed out from under it.

    tools/covers_stay_round.py

Exits non-zero, naming every rectangle with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QML = ROOT / "desktop" / "qml"

OPENS = re.compile(r"^\s*Rectangle\s*\{\s*$")
ROUNDS = re.compile(r"^\s*radius\s*:", re.M)
CLIPS = re.compile(r"^\s*clip\s*:\s*true\b", re.M)


def own(lines: list[str], start: int) -> str:
    """The lines written directly inside the block opened at `start`, children left out."""
    depth = 1
    body: list[str] = []
    for line in lines[start + 1:]:
        if depth == 1:
            body.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            break
    return "\n".join(body)


def rectangles(file: Path) -> list[tuple[int, str]]:
    lines = file.read_text(encoding="utf-8").splitlines()
    return [(i + 1, own(lines, i)) for i, line in enumerate(lines) if OPENS.match(line)]


def main() -> int:
    files = sorted(QML.glob("*.qml"))
    seen = [(f, at, body) for f in files for at, body in rectangles(f)]

    if not seen:
        print(
            "✗ this guard is broken, not the client: it matched no Rectangle at all in "
            f"{QML.relative_to(ROOT)}. Something moved, and a guard that sees nothing "
            "passes everything.",
            file=sys.stderr,
        )
        return 2

    lying = [(f, at) for f, at, body in seen if ROUNDS.search(body) and CLIPS.search(body)]

    if not lying:
        print(
            f"✓ {len(seen)} rectangle(s) across {len(files)} file(s) — none of them clips to "
            "a rounding it does not have"
        )
        return 0

    for file, at in lying:
        print(
            f"✗ {file.relative_to(ROOT)}:{at} — rounds and clips. `clip` cuts to the box, "
            "not to the radius: use RoundedCover.",
            file=sys.stderr,
        )
    print(
        f"\n{len(lying)} rectangle(s) whose corners lie. Whatever is drawn inside keeps its "
        "square corners and paints them over the rounding.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
