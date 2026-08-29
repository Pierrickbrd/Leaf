#!/usr/bin/env python3
"""Turns a prepared series into the Leaf format.

The working tree looks like this — a folder per volume, a folder per chapter inside it,
and a tomes.json holding everything that was collected along the way:

    Death Note/Black Edition/
    ├── tomes.json
    ├── Tome 1/
    │   ├── 000.jpg                          the cover
    │   ├── Chapitre 000 - Pages préliminaires/   001 → 003
    │   ├── Chapitre 001 - Ennui/                 004 → 051
    │   └── …
    └── One-Shot/

and it comes out as this:

    Death Note/
    ├── work.json
    └── Black Edition/
        ├── edition.json
        └── Tome 1.cbz    (images at the root, plus entry.json)

Two things worth knowing about what it does.

**The images are flattened.** Chapter folders are a convenience while preparing a volume;
inside the archive they carry no meaning, because Leaf orders pages by image name and
ignores folders entirely. Flattening removes a structure that could only mislead another
reader. It is safe here precisely because the numbering is continuous across the whole
volume rather than restarting in each chapter.

**The source is only ever read.** Nothing is written back, moved or renamed: the prepared
tree stays exactly as it was.

    python3 tools/from-tomes-json.py SOURCE DESTINATION [--dry-run]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

FORMAT_VERSION = 1
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif", ".avif"}

# What `content_type` in the prepared file means in Leaf terms.
MEDIUM = {
    "manga": "manga",
    "bd": "bd",
    "bande dessinée": "bd",
    "comics": "comics",
    "comic": "comics",
    "manhwa": "manhwa",
    "manhua": "manhua",
    "webtoon": "webtoon",
    "artbook": "artbook",
}


def natural_key(name: str):
    """Same order the server uses: numbers compare as numbers, 10 after 2."""
    return [int(part) if part.isdigit() else part.lower() for part in re.split(r"(\d+)", name)]


def images_of(folder: Path) -> list[Path]:
    """Every image in the volume, in reading order — by name, folders ignored."""
    found = [p for p in folder.rglob("*") if p.is_file() and p.suffix.lower() in IMAGE_SUFFIXES]
    duplicates = {p.name for p in found if [q.name for q in found].count(p.name) > 1}
    if duplicates:
        raise SystemExit(
            f"{folder.name}: several images share a name ({', '.join(sorted(duplicates)[:5])}). "
            "Reading order would be undefined — number across the whole volume, not per chapter."
        )
    return sorted(found, key=lambda p: natural_key(p.name))


def reading_direction(manga: str | None) -> str:
    if not manga:
        return "LEFT_TO_RIGHT"
    return "RIGHT_TO_LEFT" if "RightToLeft" in manga else "LEFT_TO_RIGHT"


def chapter_label(template: str | None, width: int | None) -> str | None:
    """`Page {number} : {title}` and a width of 3 become `Page {n:000}`.

    Only the part before the separator is a label; the title is a field of its own and
    the client puts them back together.
    """
    if not template:
        return None
    prefix = template.split("{title}")[0]
    # Trailing separator off, then the spaces around it. A regex with a variable-length
    # run on both sides of the separator backtracks; two strips do not.
    prefix = prefix.strip().rstrip(":-–—").strip()
    if "{number}" not in prefix:
        return None
    padding = "0" * width if width and width > 1 else ""
    return prefix.replace("{number}", "{n:%s}" % padding if padding else "{n}")


def arcs_of(source: dict) -> list[dict]:
    """Arcs as chapter ranges, which is the only way to say where one actually ends.

    Death Note's two arcs cover volumes 1–4 and 4–6: volume 4 belongs to both. Declared
    in volumes it would have to be counted twice and the boundary would be wrong by tens
    of pages.
    """
    arcs = []
    for arc in source.get("story_arcs", {}).values():
        start, end = arc.get("chapter_start"), arc.get("chapter_end")
        if not isinstance(start, (int, float)) or not isinstance(end, (int, float)):
            continue
        arcs.append({"name": arc.get("name_fr") or arc.get("name"), "unit": "CHAPTER", "from": start, "to": end})
    return sorted(arcs, key=lambda a: a["from"])


def entry_of(volume: dict, work: str, edition: str, last_number: float | None) -> tuple[dict, float | None]:
    """One entry's entry.json, and the chapter number it leaves behind.

    A one-shot is not a volume. It is eighty-nine pages holding one story and no volume
    number, which is the definition of a standalone chapter — so it is typed as one, and
    it carries no start page: the whole file is the chapter.
    """
    kind = "CHAPTER" if (volume.get("type") or "").lower() in {"oneshot", "chapter", "chapitre"} else "VOLUME"
    chapters = []
    highest = last_number
    for chapter in volume.get("chapters") or []:
        number = chapter.get("number")
        numeric = isinstance(number, (int, float)) and not isinstance(number, bool)
        entry = {"title": chapter.get("title")}
        if numeric:
            entry["number"] = number
            highest = number if highest is None else max(highest, number)
        else:
            # A chapter whose "number" is a word — a one-shot, a special — has no number.
            # It is anchored instead, and shows its title alone rather than a made-up
            # label. Inventing 109 here would be a lie the series carried for ever.
            entry["after"] = highest if highest is not None else 0
            entry["label"] = ""
        if kind == "VOLUME" and chapter.get("start_page") is not None:
            entry["startPage"] = chapter["start_page"]
        chapters.append(entry)

    number = volume.get("canonical_number")
    written = {
        "leaf": FORMAT_VERSION,
        "work": work,
        "edition": edition,
        "type": kind,
    }
    # A standalone chapter takes its number from its chapter, not from a volume count.
    if kind == "VOLUME" and isinstance(number, (int, float)):
        written["number"] = number
    for key, field in (("title", "title"), ("gtin", "isbn"), ("release_date_fr", "publishedOn"), ("summary_fr", "summary")):
        if volume.get(key):
            written[field] = volume[key]
    if chapters:
        written["chapters"] = chapters
    return written, highest


def build(
    source: Path,
    destination: Path,
    dry_run: bool,
    status: str,
    work_name: str,
    edition_name: str | None,
    universe_name: str | None,
) -> None:
    """Writes one edition. Call it again for the next one, into the same work.

    The prepared tree does not say what is an edition and what is a separate work — the
    three Parasite folders look identical on disk, and only the author tells you Reversi
    is a derived series rather than a printing of the same one. So it is named rather
    than guessed.
    """
    data = json.loads((source / "tomes.json").read_text(encoding="utf-8"))

    # A universe groups works that are not editions of one another — Parasite holds the
    # series proper and its spin-off, which is by a different author. Without the file
    # the scanner would have to guess, and a folder of folders looks the same either way.
    root = destination / universe_name if universe_name else destination
    work_dir = root / work_name
    # A work with a single edition has no folder for it: the volumes sit beside work.json
    # and the edition fields live there too. Nothing is declared for a choice that is not
    # being offered.
    edition_dir = work_dir / edition_name if edition_name else work_dir

    medium = MEDIUM.get((data.get("content_type") or "").strip().lower(), "other")
    work = {
        "leaf": FORMAT_VERSION,
        "title": work_name,
        "medium": medium,
        # Nothing in the prepared file says whether a series is finished — a count that
        # differs from the main count only means there is a one-shot beside it. So it is
        # asked for rather than guessed: an ongoing series takes new volumes on its own,
        # a completed one asks first, and getting that backwards is silently annoying.
        "status": status,
        "readingDirection": reading_direction(data.get("Manga")),
    }
    for key, field in (("Writer", "author"), ("Summary", "summary")):
        if data.get(key):
            work[field] = data[key]
    if data.get("Genre"):
        work["genres"] = [g.strip() for g in data["Genre"].split(",") if g.strip()]

    edition: dict = {
        "leaf": FORMAT_VERSION,
        "status": status,
        # What was published, and only the numbered volumes: a one-shot is not volume 7.
        "volumeCount": data.get("main_series_count") or data.get("Count"),
    }
    for key, field in (("Publisher", "publisher"), ("LanguageISO", "language"), ("Format", "format")):
        if data.get(key):
            edition[field] = data[key]
    label = chapter_label(data.get("chapter_name_template"), data.get("chapter_number_width"))
    if edition_name:
        edition["name"] = edition_name
    if label:
        edition["chapterLabel"] = label
    arcs = arcs_of(data)
    if arcs:
        edition["arcs"] = arcs

    if universe_name and not dry_run:
        root.mkdir(parents=True, exist_ok=True)
        universe_file = root / "universe.json"
        if not universe_file.is_file():
            universe_file.write_text(
                json.dumps({"leaf": FORMAT_VERSION, "name": universe_name}, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
    if universe_name:
        print(f"  universe.json→ {universe_name}")

    work_file = work_dir / "work.json"
    existing_work = work_file.is_file()

    if edition_name:
        print(f"  work.json    → {work_name}: {medium}, {work['status']}, {work['readingDirection']}"
              + ("  (already there, left alone)" if existing_work else ""))
        print(f"  edition.json → {edition_name}: {edition['volumeCount']} volumes, "
              f"label {edition.get('chapterLabel', '—')}, {len(arcs)} arc(s)")
    else:
        # Implicit edition: everything lands in work.json, and dropping an edition.json
        # beside the volumes would flip how the scanner classifies the folder.
        work.update({k: v for k, v in edition.items() if k not in ("leaf", "name")})
        print(f"  work.json    → {work_name}: {medium}, {work['status']}, {work['readingDirection']}, "
              f"{work['volumeCount']} volumes, label {work.get('chapterLabel', '—')}, {len(arcs)} arc(s)")

    if not dry_run:
        edition_dir.mkdir(parents=True, exist_ok=True)
        # A second edition must not rewrite what the first one settled about the work.
        if not existing_work:
            work_file.write_text(json.dumps(work, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if edition_name:
            (edition_dir / "edition.json").write_text(
                json.dumps(edition, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
            )

    highest: float | None = None
    for key in sorted(data["volumes"], key=lambda k: int(k)):
        volume = data["volumes"][key]
        folder = source / volume["path"]
        if not folder.is_dir():
            print(f"  ! {volume['path']}: no such folder, skipped")
            continue

        pages = images_of(folder)
        written, highest = entry_of(volume, work_name, edition_name, highest)
        target = edition_dir / volume["output"]
        anchored = sum(1 for c in written.get("chapters", []) if "after" in c)
        print(f"  {volume['output']:<14} {written['type']:<7} {len(pages):>4} pages · "
              f"{len(written.get('chapters', [])):>2} chapters"
              + (f" · {anchored} anchored" if anchored else ""))

        if dry_run:
            continue
        with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as archive:
            for page in pages:
                archive.write(page, page.name)
            archive.writestr("entry.json", json.dumps(written, ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="the folder holding tomes.json")
    parser.add_argument("destination", type=Path, help="the Leaf library root")
    parser.add_argument("--dry-run", action="store_true", help="say what would be written, write nothing")
    parser.add_argument("--universe", help="a grouping above the work, for works that are not editions of one another")
    parser.add_argument("--work", help="the work this belongs to (default: the parent folder's name)")
    parser.add_argument(
        "--edition",
        help="the edition's name, or empty for a work that has only one (default: the source folder's name)",
    )
    parser.add_argument(
        "--status",
        choices=("ongoing", "completed"),
        default="ongoing",
        help="whether the series is finished — not guessable from the prepared file",
    )
    arguments = parser.parse_args()

    if not (arguments.source / "tomes.json").is_file():
        print(f"no tomes.json in {arguments.source}", file=sys.stderr)
        return 1

    work_name = arguments.work or arguments.source.parent.name
    edition_name = arguments.source.name if arguments.edition is None else (arguments.edition or None)

    print(f"{arguments.source.name} → {arguments.destination}" + ("  (dry run)" if arguments.dry_run else ""))
    build(
        arguments.source,
        arguments.destination,
        arguments.dry_run,
        arguments.status,
        work_name,
        edition_name,
        arguments.universe,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
