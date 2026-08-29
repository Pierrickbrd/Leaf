"""The converter, tested without a prepared library.

Every case here is a shape the real tree actually took: a work with one edition and a work
with two, a universe above them, a one-shot that is not volume 7, and a chapter whose
number is a word.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import contextlib
import io
import json
import pathlib
import tempfile
import unittest
import zipfile

from from_tomes_json import (
    arcs_of,
    a_folder_name,
    inside,
    under,
    build,
    chapter_label,
    entry_of,
    images_of,
    natural_key,
    reading_direction,
)

TOMES = {
    "content_type": "Manga",
    "Manga": "YesAndRightToLeft",
    "Writer": "Tsugumi Ohba",
    "Summary": "Un carnet, et ce qu'il coûte.",
    "Genre": "Thriller, Surnaturel , ",
    "Publisher": "Kana",
    "LanguageISO": "fr",
    "Format": "Black Edition",
    "main_series_count": 6,
    "Count": 7,
    "chapter_name_template": "Chapitre {number} : {title}",
    "chapter_number_width": 3,
    "story_arcs": {
        "b": {"name": "Yotsuba", "name_fr": "Yotsuba", "chapter_start": 60, "chapter_end": 90},
        "a": {"name": "Kira", "chapter_start": 1, "chapter_end": 59},
        "x": {"name": "Sans bornes", "chapter_start": None, "chapter_end": 4},
    },
    "volumes": {
        "2": {
            "path": "One-Shot",
            "output": "One-Shot.cbz",
            "type": "oneshot",
            "canonical_number": 99,
            "title": "C-Kira",
            "chapters": [{"number": 108, "title": "C-Kira", "start_page": 0}],
        },
        "1": {
            "path": "Tome 1",
            "output": "Tome 1.cbz",
            "canonical_number": 1,
            "title": "Ennui",
            "gtin": "9782505011224",
            "release_date_fr": "2011-01-07",
            "summary_fr": "Light trouve un carnet.",
            "chapters": [
                {"number": 1, "title": "Ennui", "start_page": 3},
                {"number": "bonus", "title": "Pages préliminaires"},
            ],
        },
    },
}


def prepared(root: pathlib.Path, tomes: dict = None) -> pathlib.Path:
    """A source tree: the file, and one image per volume folder."""
    source = root / "Death Note" / "Black Edition"
    source.mkdir(parents=True)
    (source / "tomes.json").write_text(
        json.dumps(tomes if tomes is not None else TOMES), encoding="utf-8"
    )
    for volume in (tomes if tomes is not None else TOMES)["volumes"].values():
        folder = source / volume["path"] / "Chapitre 001"
        folder.mkdir(parents=True)
        for name in ("010.jpg", "9.jpg"):
            (folder / name).write_bytes(b"\xff\xd8\xff")
    return source


def written(source: pathlib.Path, destination: pathlib.Path, **overrides) -> None:
    arguments = {
        "dry_run": False,
        "status": "completed",
        "work_name": "Death Note",
        "edition_name": "Black Edition",
        "universe_name": None,
    }
    arguments.update(overrides)
    with contextlib.redirect_stdout(io.StringIO()):
        build(source, destination, **arguments)


class ReadsThePreparedFile(unittest.TestCase):
    def test_numbers_in_a_name_compare_as_numbers(self):
        self.assertEqual(sorted(["10.jpg", "9.jpg", "1.jpg"], key=natural_key),
                         ["1.jpg", "9.jpg", "10.jpg"])

    def test_a_label_keeps_only_what_comes_before_the_title(self):
        self.assertEqual(chapter_label("Chapitre {number} : {title}", 3), "Chapitre {n:000}")
        self.assertEqual(chapter_label("Chapitre {number} — {title}", None), "Chapitre {n}")
        self.assertEqual(chapter_label("Chapitre {number}", 1), "Chapitre {n}")

    def test_a_template_naming_no_number_is_no_label(self):
        self.assertIsNone(chapter_label("{title}", 3))
        self.assertIsNone(chapter_label(None, 3))

    def test_the_direction_is_read_from_one_word(self):
        self.assertEqual(reading_direction("YesAndRightToLeft"), "RIGHT_TO_LEFT")
        self.assertEqual(reading_direction("Yes"), "LEFT_TO_RIGHT")
        self.assertEqual(reading_direction(None), "LEFT_TO_RIGHT")

    def test_an_arc_without_both_ends_is_not_an_arc(self):
        arcs = arcs_of(TOMES)
        self.assertEqual([a["name"] for a in arcs], ["Kira", "Yotsuba"])
        self.assertEqual(arcs[0], {"name": "Kira", "unit": "CHAPTER", "from": 1, "to": 59})

    def test_images_are_ordered_across_the_whole_volume(self):
        with tempfile.TemporaryDirectory() as root:
            source = prepared(pathlib.Path(root))
            self.assertEqual([p.name for p in images_of(source / "Tome 1")],
                             ["9.jpg", "010.jpg"])

    def test_two_images_sharing_a_name_stop_the_run(self):
        with tempfile.TemporaryDirectory() as root:
            source = prepared(pathlib.Path(root))
            second = source / "Tome 1" / "Chapitre 002"
            second.mkdir()
            (second / "9.jpg").write_bytes(b"\xff\xd8\xff")
            with self.assertRaises(SystemExit) as refused:
                images_of(source / "Tome 1")
            self.assertIn("9.jpg", str(refused.exception))


class WritesOneEntry(unittest.TestCase):
    def test_a_volume_carries_its_number_and_its_chapters(self):
        written, highest = entry_of(TOMES["volumes"]["1"], "Death Note", "Black Edition", None)
        self.assertEqual(written["type"], "VOLUME")
        self.assertEqual(written["number"], 1)
        self.assertEqual(written["isbn"], "9782505011224")
        self.assertEqual(written["publishedOn"], "2011-01-07")
        self.assertEqual(written["chapters"][0],
                         {"title": "Ennui", "number": 1, "startPage": 3})
        self.assertEqual(highest, 1)

    def test_a_chapter_whose_number_is_a_word_is_anchored_rather_than_invented(self):
        written, _ = entry_of(TOMES["volumes"]["1"], "Death Note", "Black Edition", None)
        self.assertEqual(written["chapters"][1],
                         {"title": "Pages préliminaires", "after": 1, "label": ""})

    def test_the_first_chapter_of_all_anchors_at_zero(self):
        volume = {"chapters": [{"number": "bonus", "title": "Avant tout"}]}
        written, highest = entry_of(volume, "Death Note", None, None)
        self.assertEqual(written["chapters"][0]["after"], 0)
        self.assertIsNone(highest)

    def test_a_one_shot_is_a_chapter_and_takes_no_volume_number(self):
        written, _ = entry_of(TOMES["volumes"]["2"], "Death Note", "Black Edition", 1)
        self.assertEqual(written["type"], "CHAPTER")
        self.assertNotIn("number", written)
        # The whole file is the chapter, so no page inside it starts one.
        self.assertNotIn("startPage", written["chapters"][0])


class StaysInsideTheLibrary(unittest.TestCase):
    def test_a_folder_name_comes_back_as_it_went_in(self):
        self.assertEqual(a_folder_name("Black Edition", "edition"), "Black Edition")
        self.assertEqual(inside(pathlib.Path("/a"), "b.cbz"), pathlib.Path("/a/b.cbz"))

    def test_a_name_that_is_a_path_is_refused(self):
        for name in ("..", "../etc", "a/b", "", ".", "/etc"):
            with self.assertRaises(SystemExit, msg=name):
                a_folder_name(name, "work")

    def test_a_path_under_the_library_comes_back_resolved(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            self.assertEqual(under(root, root / "a" / "b"), root / "a" / "b")
            self.assertEqual(under(root, root), root)

    def test_a_symlink_pointing_out_of_the_library_is_refused(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            (root / "library").mkdir()
            (root / "elsewhere").mkdir()
            (root / "library" / "away").symlink_to(root / "elsewhere")
            with self.assertRaises(SystemExit):
                under(root / "library", root / "library" / "away" / "work.json")

    def test_a_volume_named_upwards_never_reaches_the_disk(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            tomes = json.loads(json.dumps(TOMES))
            tomes["volumes"]["1"]["output"] = "../../escaped.cbz"
            source = prepared(root, tomes)
            with self.assertRaises(SystemExit):
                written(source, root / "library")
            self.assertFalse((root.parent / "escaped.cbz").exists())


class WritesTheTree(unittest.TestCase):
    def test_a_work_with_a_named_edition_keeps_them_apart(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            source = prepared(root)
            written(source, root / "library")

            work = json.loads((root / "library/Death Note/work.json").read_text())
            self.assertEqual(work["medium"], "manga")
            self.assertEqual(work["readingDirection"], "RIGHT_TO_LEFT")
            self.assertEqual(work["status"], "completed")
            self.assertEqual(work["author"], "Tsugumi Ohba")
            self.assertEqual(work["genres"], ["Thriller", "Surnaturel"])
            self.assertNotIn("volumeCount", work)

            edition = json.loads(
                (root / "library/Death Note/Black Edition/edition.json").read_text()
            )
            self.assertEqual(edition["name"], "Black Edition")
            # The numbered volumes, not the one-shot beside them.
            self.assertEqual(edition["volumeCount"], 6)
            self.assertEqual(edition["chapterLabel"], "Chapitre {n:000}")
            self.assertEqual(edition["language"], "fr")
            self.assertEqual(len(edition["arcs"]), 2)

    def test_an_archive_holds_the_pages_in_order_and_its_own_entry_json(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            written(prepared(root), root / "library")

            path = root / "library/Death Note/Black Edition/Tome 1.cbz"
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.namelist(), ["9.jpg", "010.jpg", "entry.json"])
                entry = json.loads(archive.read("entry.json"))
            self.assertEqual(entry["work"], "Death Note")
            self.assertEqual(entry["edition"], "Black Edition")
            self.assertEqual(entry["title"], "Ennui")

    def test_a_work_with_one_edition_has_no_folder_for_it(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            written(prepared(root), root / "library", edition_name=None)

            work = json.loads((root / "library/Death Note/work.json").read_text())
            # Everything the edition would have said, said here instead.
            self.assertEqual(work["volumeCount"], 6)
            self.assertEqual(work["chapterLabel"], "Chapitre {n:000}")
            self.assertNotIn("name", work)
            self.assertFalse((root / "library/Death Note/edition.json").exists())
            self.assertTrue((root / "library/Death Note/Tome 1.cbz").is_file())

    def test_a_universe_gets_a_file_and_a_folder_above_the_work(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            written(prepared(root), root / "library", universe_name="Parasite")

            universe = json.loads((root / "library/Parasite/universe.json").read_text())
            self.assertEqual(universe["name"], "Parasite")
            self.assertTrue((root / "library/Parasite/Death Note/work.json").is_file())

    def test_a_second_edition_leaves_the_first_ones_work_json_alone(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            source = prepared(root)
            written(source, root / "library")
            written(source, root / "library", status="ongoing", edition_name="Poche")

            work = json.loads((root / "library/Death Note/work.json").read_text())
            self.assertEqual(work["status"], "completed")
            self.assertTrue((root / "library/Death Note/Poche/edition.json").is_file())

    def test_a_dry_run_writes_nothing_at_all(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            written(prepared(root), root / "library", dry_run=True, universe_name="Parasite")
            self.assertFalse((root / "library").exists())

    def test_a_volume_whose_folder_is_missing_is_said_and_skipped(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            source = prepared(root)
            for image in (source / "One-Shot").rglob("*.jpg"):
                image.unlink()
            (source / "One-Shot" / "Chapitre 001").rmdir()
            (source / "One-Shot").rmdir()

            said = io.StringIO()
            with contextlib.redirect_stdout(said):
                build(source, root / "library", False, "completed", "Death Note",
                      "Black Edition", None)
            self.assertIn("One-Shot: no such folder, skipped", said.getvalue())
            self.assertFalse((root / "library/Death Note/Black Edition/One-Shot.cbz").exists())


if __name__ == "__main__":
    unittest.main()
