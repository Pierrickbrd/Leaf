#!/usr/bin/env python3
"""Refuses a second copy of something one file already owns.

Three shapes in this client were written out wherever they were needed, and each drifted the
way copies drift — not into a bug, into an inconsistency nobody decided on:

    the tinted glyph   eleven copies, ten files, five sizes. The Material Symbols carry no
                       fill, so a glyph is an `Image` drawn hidden and a `ColorOverlay`
                       painting it. Forget the `visible: false` and the untinted glyph shows
                       through under the tinted one.

    the card's lift    five copies, five pairs of numbers, two of them already disagreeing by
                       a pixel for no reason anyone could name — and each carrying the same
                       comment saying it was the same lift.

    the rounded cover  six copies, four of them wrong: `clip` cuts to the box and not to the
                       radius, so the picture kept its square corners and painted them over
                       the rounding. `covers_stay_round.py` refuses that mistake; this
                       refuses writing the mask out again at all.

This guard is the rule « reuse it, do not write a second one that works differently » made
checkable, because the three above were each found by somebody looking at a screenshot rather
than at the code.

**A file may use what it owns.** That is the only exception, and it is the point.

**It refuses to see nothing.** Every owner named below must exist and must itself contain the
shape it owns; a rename that left this matching nothing would otherwise pass in silence —
which is what `client_knows_the_contract.py` did the first time its own regex was renamed out
from under it.

    tools/one_file_owns_it.py

Exits non-zero, naming every copy with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QML = ROOT / "desktop" / "qml"


class Owned:
    """One shape, the file that owns it, and what a copy of it looks like."""

    def __init__(self, what: str, owner: str, shows_up: re.Pattern, instead: str,
                 also: re.Pattern | None = None):
        self.what = what
        self.owner = owner
        self.shows_up = shows_up
        self.instead = instead
        # A second line the same block must carry for this to be the shape and not a
        # coincidence. Without it, `z: -1` alone accused a nine-slice shadow and a hairline.
        self.also = also


OWNED = [
    Owned("the tinted glyph", "Glyph.qml", re.compile(r"\bColorOverlay\s*\{"),
          "use `Glyph`"),
    # A black fill laid *under* what it lifts. The pair is what makes it a lift: a negative
    # `z` on its own is any hairline drawn behind a row, and a black fill on its own is a
    # veil. Matched on the block so the two may be written in either order.
    Owned("the card's lift", "CardLift.qml",
          re.compile(r'color:\s*"#000000"'), "use `CardLift`, at the height the thing sits",
          also=re.compile(r"z:\s*-1")),
    Owned("the rounded cover", "RoundedCover.qml", re.compile(r"\bOpacityMask\s*\{"),
          "use `RoundedCover`"),
]


def block(lines: list[str], start: int) -> str:
    """The lines written directly inside the block the line at `start` belongs to."""
    depth = 0
    for i in range(start, -1, -1):
        depth += lines[i].count("}") - lines[i].count("{")
        if depth < 0:
            opened = i
            break
    else:
        return "\n".join(lines)
    depth, held = 1, []
    for line in lines[opened + 1:]:
        held.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            break
    return "\n".join(held)


def copies(file: Path, shape: Owned) -> list[tuple[int, str]]:
    lines = file.read_text(encoding="utf-8").splitlines()
    said: list[tuple[int, str]] = []
    for number, line in enumerate(lines, 1):
        if line.lstrip().startswith(("//", "/*", "*")):
            continue
        if not shape.shows_up.search(line):
            continue
        if shape.also is not None and not shape.also.search(block(lines, number - 1)):
            continue
        said.append((number, line.strip()))
    return said


def main() -> int:
    files = sorted(QML.glob("*.qml"))
    if not files:
        print(f"✗ this guard is broken: no .qml at all in {QML.relative_to(ROOT)}.",
              file=sys.stderr)
        return 2

    blind = [s.what for s in OWNED
             if not (QML / s.owner).is_file() or not copies(QML / s.owner, s)]
    if blind:
        print(
            "✗ this guard is broken, not the client: it found no sign of "
            f"{', '.join(blind)} in the file that owns it. Something moved, and a guard "
            "that sees nothing passes everything.",
            file=sys.stderr,
        )
        return 2

    again: list[tuple[Owned, Path, int, str]] = []
    for shape in OWNED:
        for file in files:
            if file.name == shape.owner:
                continue
            again += [(shape, file, at, line) for at, line in copies(file, shape)]

    if not again:
        print(f"✓ {len(files)} file(s) — {len(OWNED)} shapes, one owner each, no second copy")
        return 0

    for shape, file, at, line in again:
        print(f"✗ {file.relative_to(ROOT)}:{at} — {shape.what} again ({line}). "
              f"{shape.instead}.", file=sys.stderr)
    print(
        f"\n{len(again)} copy of something already written once. A second one does not stay "
        "the same as the first — it drifts, and the screen says two things.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
