"""The small library the conformance check runs against, built into a temporary folder.

It exists to carry one of everything the contract can describe, so what is checked here is
that it still does: a universe, a work without one, chapter markers with and without a
title, and a file waiting in the drop. A fixture that quietly lost its universe would make
the conformance check pass by having nothing to disagree about.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import contextlib
import io
import json
import pathlib
import tempfile
import unittest
import zipfile

from fixture import JPEG, archive, main


class WritesASmallLibrary(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.folder.name)
        with contextlib.redirect_stdout(io.StringIO()) as said:
            main(self.root)
        self.said = said.getvalue()
        self.library = self.root / "library"
        self.addCleanup(self.folder.cleanup)

    def test_the_server_s_four_folders_are_there(self):
        for folder in ("inbox", "cache", "drop", "data"):
            self.assertTrue((self.root / folder).is_dir(), folder)

    def test_a_universe_holds_a_work_that_declares_itself(self):
        universe = json.loads((self.library / "Terres d'Arran/universe.json").read_text())
        self.assertEqual(universe["name"], "Terres d'Arran")
        work = json.loads((self.library / "Terres d'Arran/Nains/work.json").read_text())
        self.assertEqual(work["medium"], "bd")
        self.assertEqual(work["arcs"][0]["unit"], "VOLUME")

    def test_a_work_with_no_universe_above_it(self):
        work = json.loads((self.library / "Bleach/work.json").read_text())
        self.assertEqual(work["readingDirection"], "RIGHT_TO_LEFT")
        self.assertEqual(work["status"], "completed")

    def test_a_volume_carries_its_pages_and_its_own_entry_json(self):
        with zipfile.ZipFile(self.library / "Bleach/Tome 1.cbz") as z:
            self.assertEqual(z.namelist(), [f"{n:03d}.jpg" for n in range(6)] + ["entry.json"])
            entry = json.loads(z.read("entry.json"))
            # One marker with a title and one without: both shapes the client has to draw.
            self.assertEqual(entry["chapters"][0]["title"], "Death & Strawberry")
            self.assertNotIn("title", entry["chapters"][1])
            self.assertEqual(z.read("000.jpg"), JPEG)

    def test_the_pages_are_real_images_the_server_can_measure(self):
        # A zero-byte page would make every dimension in the index null, and the
        # conformance check would pass on a contract that promises numbers.
        self.assertTrue(JPEG.startswith(b"\xff\xd8\xff"))
        self.assertIn(b"\xff\xd9", JPEG[-2:])

    def test_one_file_waits_in_the_drop_for_the_short_path(self):
        self.assertTrue((self.root / "drop/Tome 3.cbz").is_file())
        self.assertFalse((self.library / "Nains/Tome 3.cbz").exists())

    def test_it_says_what_it_wrote(self):
        self.assertIn("2 series, 4 volumes and 6 chapters", self.said)

    def test_running_it_twice_over_leaves_the_same_library(self):
        before = sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*"))
        with contextlib.redirect_stdout(io.StringIO()):
            main(self.root)
        after = sorted(p.relative_to(self.root).as_posix() for p in self.root.rglob("*"))
        self.assertEqual(before, after)


class WritesOneArchive(unittest.TestCase):
    def test_it_makes_the_folders_it_needs(self):
        with tempfile.TemporaryDirectory() as root:
            path = pathlib.Path(root) / "deep" / "deeper" / "One.cbz"
            archive(path, 2, {"leaf": 1, "work": "Essai"})
            with zipfile.ZipFile(path) as z:
                self.assertEqual(z.namelist(), ["000.jpg", "001.jpg", "entry.json"])
                self.assertEqual(json.loads(z.read("entry.json"))["work"], "Essai")


if __name__ == "__main__":
    unittest.main()
