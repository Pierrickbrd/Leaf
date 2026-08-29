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

    python3 tools/from_tomes_json.py SOURCE DESTINATION [--dry-run]
"""

from __future__ import annotations

import argparse
import json
import os.path
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


def chapter_entry(chapter: dict, kind: str, highest: float | None) -> tuple[dict, float | None]:
    """One chapter's line in entry.json, and the highest number seen so far.

    A chapter whose "number" is a word — a one-shot, a special — has no number. It is
    anchored instead, and shows its title alone rather than a made-up label. Inventing 109
    here would be a lie the series carried for ever.
    """
    number = chapter.get("number")
    entry = {"title": chapter.get("title")}
    if isinstance(number, (int, float)) and not isinstance(number, bool):
        entry["number"] = number
        highest = number if highest is None else max(highest, number)
    else:
        entry["after"] = highest if highest is not None else 0
        entry["label"] = ""
    if kind == "VOLUME" and chapter.get("start_page") is not None:
        entry["startPage"] = chapter["start_page"]
    return entry, highest


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
        one, highest = chapter_entry(chapter, kind, highest)
        chapters.append(one)

    written = {
        "leaf": FORMAT_VERSION,
        "work": work,
        "edition": edition,
        "type": kind,
    }
    # A standalone chapter takes its number from its chapter, not from a volume count.
    number = volume.get("canonical_number")
    if kind == "VOLUME" and isinstance(number, (int, float)):
        written["number"] = number
    for key, field in (("title", "title"), ("gtin", "isbn"), ("release_date_fr", "publishedOn"), ("summary_fr", "summary")):
        if volume.get(key):
            written[field] = volume[key]
    if chapters:
        written["chapters"] = chapters
    return written, highest


def work_of(data: dict, work_name: str, status: str) -> dict:
    """What stays true of the work whichever edition you are holding."""
    work = {
        "leaf": FORMAT_VERSION,
        "title": work_name,
        "medium": MEDIUM.get((data.get("content_type") or "").strip().lower(), "other"),
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
    return work


def edition_of(data: dict, edition_name: str | None, status: str) -> dict:
    """What is true of this printing of it, and of no other."""
    edition: dict = {
        "leaf": FORMAT_VERSION,
        "status": status,
        # What was published, and only the numbered volumes: a one-shot is not volume 7.
        "volumeCount": data.get("main_series_count") or data.get("Count"),
    }
    for key, field in (("Publisher", "publisher"), ("LanguageISO", "language"), ("Format", "format")):
        if data.get(key):
            edition[field] = data[key]
    if edition_name:
        edition["name"] = edition_name
    label = chapter_label(data.get("chapter_name_template"), data.get("chapter_number_width"))
    if label:
        edition["chapterLabel"] = label
    arcs = arcs_of(data)
    if arcs:
        edition["arcs"] = arcs
    return edition


def a_folder_name(value: str, what: str) -> str:
    """The one folder name `value` was asked to be.

    `--work ../../etc` would otherwise write outside the destination, and quietly: every
    path in here is built by joining, and a join with `..` walks straight back out. Taken
    apart rather than checked, so that what reaches the joining is a name by construction
    and not a string that passed a test — and refused rather than trimmed, because a name
    that had to be trimmed is not the name anybody meant.
    """
    name = os.path.basename(value)
    if name != value or name in ("", ".", ".."):
        raise SystemExit(f'--{what}: "{value}" names a folder, so it cannot be a path')
    return name


def inside(root: Path, name: str) -> Path:
    """`root` with one more name on it, wherever that name came from.

    The names off the command line are taken apart where they arrive; this is for the ones
    that come out of tomes.json, which nothing has vouched for either.
    """
    return root / a_folder_name(name, "output")


def under(library: Path, path: Path) -> Path:
    """`path`, resolved, having checked it really is inside the library.

    The second check, at the other end from `a_folder_name`, and the one that catches what
    a name alone cannot: a symlink halfway along the path, pointing anywhere. Resolved
    first and compared afterwards, because two paths only mean the same folder once
    everything standing in the way has been followed.
    """
    resolved = os.path.realpath(path)
    root = os.path.realpath(library)
    if resolved != root and not resolved.startswith(root + os.sep):
        raise SystemExit(f"{path} is outside {library}")
    return Path(resolved)


def write_json(path: Path, content: dict) -> None:
    """Straight into the open file, rather than through one big string.

    `write_text` takes the path and the whole document in a single call, which reads as
    though the two were the same kind of thing. They are not: the path has been checked and
    the content is whatever the source file and the command line said. Opening first keeps
    them apart.
    """
    with path.open("w", encoding="utf-8") as file:
        json.dump(content, file, ensure_ascii=False, indent=2)
        file.write("\n")


def write_universe(root: Path, path: Path, name: str) -> None:
    """Written once and never rewritten: a second work joining a universe does not get to
    rename it."""
    root.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        write_json(path, {"leaf": FORMAT_VERSION, "name": name})


def write_sidecars(
    edition_dir: Path,
    work_file: Path,
    edition_file: Path | None,
    work: dict,
    edition: dict,
    existing_work: bool,
) -> None:
    edition_dir.mkdir(parents=True, exist_ok=True)
    # A second edition must not rewrite what the first one settled about the work.
    if not existing_work:
        write_json(work_file, work)
    if edition_file:
        write_json(edition_file, edition)


def announce(
    work: dict,
    edition: dict,
    work_name: str,
    edition_name: str | None,
    arcs: list,
    existing_work: bool,
) -> None:
    """What is about to be written, in the order it will be read."""
    if edition_name:
        print(f"  work.json    → {work_name}: {work['medium']}, {work['status']}, {work['readingDirection']}"
              + ("  (already there, left alone)" if existing_work else ""))
        print(f"  edition.json → {edition_name}: {edition['volumeCount']} volumes, "
              f"label {edition.get('chapterLabel', '—')}, {len(arcs)} arc(s)")
    else:
        print(f"  work.json    → {work_name}: {work['medium']}, {work['status']}, {work['readingDirection']}, "
              f"{work['volumeCount']} volumes, label {work.get('chapterLabel', '—')}, {len(arcs)} arc(s)")


def write_volumes(
    source: Path,
    edition_dir: Path,
    data: dict,
    work_name: str,
    edition_name: str | None,
    dry_run: bool,
) -> None:
    """Every volume in number order, each as one archive holding its pages and its entry.json.

    Stored rather than deflated: the pages are already compressed images, and asking zip to
    compress them again costs the whole library's worth of CPU to save nothing.
    """
    highest: float | None = None
    for key in sorted(data["volumes"], key=lambda k: int(k)):
        volume = data["volumes"][key]
        folder = source / volume["path"]
        if not folder.is_dir():
            print(f"  ! {volume['path']}: no such folder, skipped")
            continue

        pages = images_of(folder)
        written, highest = entry_of(volume, work_name, edition_name, highest)
        anchored = sum(1 for c in written.get("chapters", []) if "after" in c)
        print(f"  {volume['output']:<14} {written['type']:<7} {len(pages):>4} pages · "
              f"{len(written.get('chapters', [])):>2} chapters"
              + (f" · {anchored} anchored" if anchored else ""))

        if dry_run:
            continue
        target = inside(edition_dir, volume["output"])
        with zipfile.ZipFile(target, "w", zipfile.ZIP_STORED) as archive:
            for page in pages:
                archive.write(page, page.name)
            archive.writestr("entry.json", json.dumps(written, ensure_ascii=False, indent=2))


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
    library = under(destination, destination)
    root = under(library, library / universe_name) if universe_name else library
    work_dir = under(library, root / work_name)
    # A work with a single edition has no folder for it: the volumes sit beside work.json
    # and the edition fields live there too. Nothing is declared for a choice that is not
    # being offered.
    edition_dir = under(library, work_dir / edition_name) if edition_name else work_dir

    # Every file this writes, checked one by one rather than trusting the folder it sits in:
    # the check belongs on the path that reaches the disk, not on the one it was built from.
    universe_file = under(library, root / "universe.json") if universe_name else None
    work_file = under(library, work_dir / "work.json")
    edition_file = under(library, edition_dir / "edition.json") if edition_name else None

    work = work_of(data, work_name, status)
    edition = edition_of(data, edition_name, status)
    arcs = edition.get("arcs", [])
    if not edition_name:
        # Implicit edition: everything lands in work.json, and dropping an edition.json
        # beside the volumes would flip how the scanner classifies the folder.
        work.update({k: v for k, v in edition.items() if k not in ("leaf", "name")})

    if universe_name:
        if not dry_run:
            write_universe(root, universe_file, universe_name)
        print(f"  universe.json→ {universe_name}")

    existing_work = work_file.is_file()
    announce(work, edition, work_name, edition_name, arcs, existing_work)
    if not dry_run:
        write_sidecars(edition_dir, work_file, edition_file, work, edition, existing_work)
    write_volumes(source, edition_dir, data, work_name, edition_name, dry_run)


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

    # Every name that becomes a folder is taken apart here, at the one place they arrive,
    # rather than anywhere further in where the path is already half built.
    universe_name = a_folder_name(arguments.universe, "universe") if arguments.universe else None
    work_name = a_folder_name(arguments.work, "work") if arguments.work else arguments.source.parent.name
    if arguments.edition is None:
        edition_name = arguments.source.name
    else:
        # `--edition ""` is the way to say a work has only one, and needs no folder for it.
        edition_name = a_folder_name(arguments.edition, "edition") if arguments.edition else None

    print(f"{arguments.source.name} → {arguments.destination}" + ("  (dry run)" if arguments.dry_run else ""))
    build(
        arguments.source,
        arguments.destination,
        arguments.dry_run,
        arguments.status,
        work_name,
        edition_name,
        universe_name,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
