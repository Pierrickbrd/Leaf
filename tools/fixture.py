#!/usr/bin/env python3
"""A small library on disk, for the conformance check to run against.

Not the real one: it has to be reproducible, it has to be small enough for CI, and it has
to carry one of everything the contract can describe — a universe, a work with an implicit
edition, chapter markers, an arc, a file waiting in the drop.

    tools/fixture.py <folder>
"""

import json
import sys
import zipfile
from pathlib import Path

# One valid JPEG, 1×1, so the pages are real images and the server measures real dimensions.
JPEG = bytes.fromhex(
    "ffd8ffe000104a46494600010100000100010000ffdb004300"
    + "ff" * 64
    + "ffc00011080001000103012200021101031101"
    "ffc4001f0000010501010101010100000000000000000102030405060708090a0b"
    "ffc400b5100002010303020403050504040000017d01020300041105122131410613516107"
    "227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a343536"
    "3738393a434445464748494a535455565758595a636465666768696a737475767778797a83"
    "8485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4"
    "c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa"
    "ffda0008010100003f00fbfe8a28a2803fffd9"
)


def archive(path: Path, pages: int, entry: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        for page in range(pages):
            z.writestr(f"{page:03d}.jpg", JPEG)
        z.writestr("entry.json", json.dumps(entry, ensure_ascii=False))


def main(root: Path) -> None:
    library = root / "library"
    for folder in ("inbox", "cache", "drop", "data"):
        (root / folder).mkdir(parents=True, exist_ok=True)

    # A universe holding one work: the level a manga rarely has and a BD series usually does.
    arran = library / "Terres d'Arran"
    arran.mkdir(parents=True, exist_ok=True)
    (arran / "universe.json").write_text(json.dumps({"leaf": 1, "name": "Terres d'Arran"}))
    nains = arran / "Nains"
    nains.mkdir(exist_ok=True)
    (nains / "work.json").write_text(
        json.dumps(
            {
                "leaf": 1,
                "title": "Nains",
                "medium": "bd",
                "author": "Nicolas Jarry",
                "status": "ongoing",
                "readingDirection": "LEFT_TO_RIGHT",
                "genres": ["fantasy"],
                "summary": "Cinq clans, cinq récits.",
                "publisher": "Soleil",
                "language": "fr",
                "volumeCount": 4,
                "arcs": [{"name": "Premier cycle", "unit": "VOLUME", "from": 1, "to": 2}],
            },
            ensure_ascii=False,
        )
    )
    for n in (1, 2):
        archive(nains / f"Tome {n}.cbz", 4, {
            "leaf": 1, "work": "Nains", "number": n, "title": f"Tome {n}",
            "chapters": [{"number": n, "title": f"Récit {n}", "startPage": 0}],
        })

    # A work with no universe above it and two chapter markers per volume.
    bleach = library / "Bleach"
    bleach.mkdir(exist_ok=True)
    (bleach / "work.json").write_text(
        json.dumps({
            "leaf": 1, "title": "Bleach", "medium": "manga", "author": "Tite Kubo",
            "status": "completed", "readingDirection": "RIGHT_TO_LEFT",
            "genres": ["shonen"], "volumeCount": 74,
        })
    )
    for n in (1, 2):
        archive(bleach / f"Tome {n}.cbz", 6, {
            "leaf": 1, "work": "Bleach", "number": n,
            "chapters": [
                {"number": n * 2 - 1, "title": "Death & Strawberry", "startPage": 0},
                {"number": n * 2, "startPage": 3},
            ],
        })

    # One waiting in the shared folder, so the short path has something to list.
    archive(root / "drop" / "Tome 3.cbz", 4, {"leaf": 1, "work": "Nains", "number": 3})
    print(f"a library of 2 series, 4 volumes and 6 chapters under {library}")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
