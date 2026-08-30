"""The bench, tested without a disk, without a binary and without a server.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import contextlib
import io
import pathlib
import stat
import tempfile
import unittest

from scan_bench import (
    System,
    _VARIANTS,
    disk_facts,
    main,
    check_the_arguments,
    parse_summary,
    refuse_a_volatile_index,
    refuse_the_production_index,
    render_report,
    run_pass,
    stats,
)

REAL_OUTPUT = """
12 universe(s), 40 work(s), 44 edition(s), 335 entry(ies), 2677 chapter(s), 57686 page(s)
335 entry(ies) reanalysed

Chapters without a start page (3):
\t· Bleach / Tome 12 / 101
"""


class ReadsTheReport(unittest.TestCase):
    def test_takes_the_seven_counters(self):
        self.assertEqual(
            parse_summary(REAL_OUTPUT),
            {
                "universes": 12,
                "works": 40,
                "editions": 44,
                "entries": 335,
                "chapters": 2677,
                "pages": 57686,
                "reanalysed": 335,
            },
        )

    def test_a_scan_that_reanalysed_nothing_counts_zero(self):
        text = "0 universe(s), 0 work(s), 0 edition(s), 0 entry(ies), 0 chapter(s), 0 page(s)"
        self.assertEqual(parse_summary(text)["reanalysed"], 0)

    def test_output_that_is_not_a_report_is_refused(self):
        # A binary that failed prints something else. Reading that as an empty
        # report would dress a crash up as a measurement.
        with self.assertRaises(ValueError):
            parse_summary("error: opening the index")


class SumsUpThePasses(unittest.TestCase):
    def test_median_of_an_odd_number_of_passes(self):
        self.assertEqual(stats([3.0, 1.0, 2.0]), {"passes": 3, "min": 1.0, "median": 2.0, "max": 3.0})

    def test_median_of_an_even_number_is_the_mean_of_the_middle_two(self):
        self.assertEqual(stats([1.0, 2.0, 3.0, 5.0])["median"], 2.5)

    def test_no_passes_is_an_error(self):
        with self.assertRaises(ValueError):
            stats([])


class FakeSystem:
    """A laboratory disk: what the commands would have answered.

    Every command must be spelled out, so an unexpected one fails loudly instead of
    quietly returning nothing — with one exception. The filesystem probe behind the
    `--db` guard is asked about paths the tests generate (temporary directories), so it
    answers from `fstype` rather than from the dict. `ext4` unless a test says otherwise.
    """

    _FSTYPE = "findmnt -no FSTYPE --target "
    _SOURCE = "findmnt -no SOURCE --target "

    def __init__(self, commands, files, links=None, fstype="ext4", source=None):
        self.commands = commands
        self.files = files
        self.links = links or {}
        self.fstype = fstype
        # Tests that need a corpus which really exists on disk cannot spell its
        # temporary path into `commands`. They give `source` instead, and every
        # findmnt asks the same question of it.
        self.source = source

    def run(self, *cmd):
        joined = " ".join(cmd)
        if joined.startswith(self._FSTYPE):
            return self.fstype + "\n"
        if self.source is not None and joined.startswith(self._SOURCE):
            return self.source + "\n"
        return self.commands[joined]

    def read(self, path):
        return self.files.get(path)

    def realpath(self, path):
        return self.links.get(path, path)

    def exists(self, path):
        prefix = path.rstrip("/") + "/"
        return path in self.links or any(
            known == path or known.startswith(prefix) for known in self.files
        )


SPINNING_USB = FakeSystem(
    commands={
        "findmnt -no SOURCE --target /mnt/shelf/library": "/dev/sda1\n",
        "lsblk -ndo PKNAME /dev/sda1": "sda\n",
        "lsblk -ndo TRAN /dev/sda": "usb\n",
    },
    files={
        "/sys/block/sda/queue/rotational": "1\n",
        "/sys/block/sda/queue/read_ahead_kb": "2048\n",
        "/sys/class/scsi_host/host2/proc_name": "usb-storage\n",
        "/sys/devices/pci0000:00/usb4/4-1/speed": "5000\n",
    },
    links={
        "/sys/block/sda": "/sys/devices/pci0000:00/usb4/4-1/4-1:1.0/host2/target2:0:0/2:0:0:0/block/sda"
    },
)

# An internal volume behind LVM, as such a machine really answers: the mount sits on an LVM
# logical volume, `lsblk -ndo PKNAME` of a dm target answers nothing at all, and
# /sys/block holds `dm-0` — never the mapper name. Only the realpath route finds it.
INTERNAL_LV = FakeSystem(
    commands={
        "findmnt -no SOURCE --target /mnt/bench/library": "/dev/mapper/ubuntu--vg-ubuntu--lv\n",
        "lsblk -ndo PKNAME /dev/dm-0": "\n",
        "lsblk -ndo TRAN /dev/dm-0": "\n",
    },
    files={
        "/sys/block/dm-0/queue/rotational": "0\n",
        "/sys/block/dm-0/queue/read_ahead_kb": "128\n",
    },
    links={"/dev/mapper/ubuntu--vg-ubuntu--lv": "/dev/dm-0"},
)


def _spinning_usb_for(corpus):
    """The rotational-USB fixture, answering for a corpus that really exists."""
    return FakeSystem(
        commands={"lsblk -ndo PKNAME /dev/sda1": "sda\n", "lsblk -ndo TRAN /dev/sda": "usb\n"},
        files=dict(SPINNING_USB.files),
        links=dict(SPINNING_USB.links),
        source="/dev/sda1",
    )

class NamesTheDisk(unittest.TestCase):
    def test_a_rotational_usb_disk_says_its_driver_and_its_link(self):
        facts = disk_facts(SPINNING_USB, "/mnt/shelf/library")
        self.assertEqual(facts["base"], "sda")
        self.assertTrue(facts["rotational"])
        self.assertEqual(facts["transport"], "usb")
        self.assertEqual(facts["driver"], "usb-storage")
        self.assertEqual(facts["link_mbps"], 5000)
        self.assertEqual(facts["read_ahead_kb"], 2048)

    def test_a_logical_volume_is_followed_to_the_dm_device_sysfs_knows(self):
        # The mapper name is not in /sys/block and a dm target has no PKNAME, so
        # only resolving the symlink first reaches a disk that can be described.
        facts = disk_facts(INTERNAL_LV, "/mnt/bench/library")
        self.assertEqual(facts["source"], "/dev/mapper/ubuntu--vg-ubuntu--lv")
        self.assertEqual(facts["base"], "dm-0")
        self.assertFalse(facts["rotational"])
        self.assertIsNone(facts["transport"])
        self.assertIsNone(facts["driver"])
        self.assertIsNone(facts["link_mbps"])
        self.assertEqual(facts["read_ahead_kb"], 128)

    def test_uas_is_told_apart_from_usb_storage(self):
        # This is the single field that decides whether parallelising the scan
        # can mean anything: under BOT there is no command queue at all.
        flash_usb = FakeSystem(
            commands={
                "findmnt -no SOURCE --target /mnt/flash/library": "/dev/sdb1\n",
                "lsblk -ndo PKNAME /dev/sdb1": "sdb\n",
                "lsblk -ndo TRAN /dev/sdb": "usb\n",
            },
            files={
                "/sys/block/sdb/queue/rotational": "0\n",
                "/sys/block/sdb/queue/read_ahead_kb": "128\n",
                "/sys/class/scsi_host/host6/proc_name": "uas\n",
                "/sys/devices/pci0000:00/usb2/2-1/speed": "20000\n",
            },
            links={
                "/sys/block/sdb": "/sys/devices/pci0000:00/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb"
            },
        )
        facts = disk_facts(flash_usb, "/mnt/flash/library")
        self.assertEqual(facts["driver"], "uas")
        self.assertEqual(facts["link_mbps"], 20000)

    def test_a_device_findmnt_cannot_name_is_refused(self):
        # `findmnt` absent, or answering nothing: the bench must stop, not invent a disk.
        nowhere = FakeSystem(
            commands={"findmnt -no SOURCE --target /srv/library": ""}, files={}
        )
        with self.assertRaises(ValueError) as refusal:
            disk_facts(nowhere, "/srv/library")
        self.assertIn("findmnt", str(refusal.exception))
        self.assertIn("/srv/library", str(refusal.exception))

    def test_a_disk_sysfs_does_not_know_is_refused(self):
        # /tmp is tmpfs on this server: there is no block device under it, and no
        # timing taken there means anything about a disk.
        in_ram = FakeSystem(
            commands={
                "findmnt -no SOURCE --target /tmp/library": "tmpfs\n",
                "lsblk -ndo PKNAME /tmp/library/tmpfs": "\n",
            },
            files={},
            links={"tmpfs": "/tmp/library/tmpfs"},
        )
        with self.assertRaises(ValueError) as refusal:
            disk_facts(in_ram, "/tmp/library")
        self.assertIn("/sys/block/tmpfs", str(refusal.exception))

    def test_a_disk_whose_rotational_cannot_be_read_is_refused(self):
        # Never `rotational no` on the strength of a probe that answered nothing:
        # that field alone decides the parallelism question.
        mute = FakeSystem(
            commands={
                "findmnt -no SOURCE --target /srv/library": "/dev/sdc1\n",
                "lsblk -ndo PKNAME /dev/sdc1": "sdc\n",
            },
            files={"/sys/block/sdc/queue/read_ahead_kb": "128\n"},
        )
        with self.assertRaises(ValueError) as refusal:
            disk_facts(mute, "/srv/library")
        self.assertIn("/sys/block/sdc/queue/rotational", str(refusal.exception))

    def test_a_sysfs_value_that_is_not_a_number_reads_as_unknown(self):
        odd = FakeSystem(
            commands={
                "findmnt -no SOURCE --target /srv/library": "/dev/sdd1\n",
                "lsblk -ndo PKNAME /dev/sdd1": "sdd\n",
                "lsblk -ndo TRAN /dev/sdd": "sata\n",
            },
            files={
                "/sys/block/sdd/queue/rotational": "1\n",
                "/sys/block/sdd/queue/read_ahead_kb": "none of your business\n",
            },
        )
        self.assertIsNone(disk_facts(odd, "/srv/library")["read_ahead_kb"])

    def test_a_command_that_is_not_installed_answers_nothing(self):
        # `findmnt` missing must reach `disk_facts`'s refusal, not a traceback.
        self.assertEqual(System().run("leaf-no-such-command-anywhere"), "")


class RendersTheResult(unittest.TestCase):
    def test_the_report_cites_the_disk_before_the_numbers(self):
        rendered = render_report(
            "/mnt/shelf/library",
            disk_facts(SPINNING_USB, "/mnt/shelf/library"),
            {
                "whole": {
                    "stats": {"passes": 3, "min": 288.0, "median": 291.0, "max": 297.0},
                    "counts": {"pages": 57686, "entries": 335},
                },
            },
        )
        self.assertIn("/dev/sda1", rendered)
        self.assertIn("usb-storage", rendered)
        self.assertIn("5000", rendered)
        self.assertIn("2048", rendered)
        self.assertIn("291", rendered)
        # The disk must come before the numbers: a reader must not be able to
        # carry away a time without having read what it was taken on.
        self.assertLess(rendered.index("usb-storage"), rendered.index("291"))

    def test_the_gap_between_variants_is_computed(self):
        rendered = render_report(
            "/mnt/bench/library",
            disk_facts(INTERNAL_LV, "/mnt/bench/library"),
            {
                "whole": {"stats": {"passes": 1, "min": 40.0, "median": 40.0, "max": 40.0}, "counts": {}},
                "no dimensions": {"stats": {"passes": 1, "min": 10.0, "median": 10.0, "max": 10.0}, "counts": {}},
            },
        )
        # 40 - 10 = 30 s spent reading pages, which is 75 % of the scan.
        self.assertIn("30", rendered)
        self.assertIn("75", rendered)

    def test_a_field_that_is_unknown_is_never_printed_as_no(self):
        facts = dict(
            disk_facts(SPINNING_USB, "/mnt/shelf/library"),
            rotational=None,
            read_ahead_kb=None,
        )
        rendered = render_report("/srv/library", facts, {})
        self.assertIn("rotational   unknown", rendered)
        self.assertIn("read-ahead   unknown", rendered)
        self.assertNotIn("rotational   no", rendered)

    def test_the_first_pass_of_each_variant_is_shown_and_said_to_be_the_cold_one(self):
        rendered = render_report(
            "/mnt/bench/library",
            disk_facts(INTERNAL_LV, "/mnt/bench/library"),
            {
                "whole": {
                    "stats": {"passes": 3, "min": 40.0, "median": 41.0, "max": 62.0},
                    "counts": {"pages": 4},
                    "first": 62.0,
                },
            },
        )
        self.assertIn("first (coldest) 62.0 s", rendered)
        self.assertIn("warm", rendered)

    def test_a_variant_that_is_not_faster_is_not_printed_as_a_negative_cost(self):
        rendered = render_report(
            "/mnt/bench/library",
            disk_facts(INTERNAL_LV, "/mnt/bench/library"),
            {
                "whole": {"stats": {"passes": 1, "min": 10.0, "median": 10.0, "max": 10.0}, "counts": {}},
                "no dimensions": {"stats": {"passes": 1, "min": 11.0, "median": 11.0, "max": 11.0}, "counts": {}},
            },
        )
        self.assertNotIn("-1.0 s", rendered)
        self.assertNotIn("-10 %", rendered)
        self.assertIn("did not make the scan faster", rendered)

    def test_a_report_where_every_counter_is_zero_is_flagged_loudly(self):
        empty = {name: 0 for name in ("universes", "works", "editions", "entries", "chapters", "pages")}
        rendered = render_report(
            "/srv/typo",
            disk_facts(SPINNING_USB, "/mnt/shelf/library"),
            {
                "whole": {
                    "stats": {"passes": 3, "min": 0.1, "median": 0.1, "max": 0.2},
                    "counts": empty,
                },
            },
        )
        self.assertIn("EVERY COUNTER IS ZERO", rendered)
        # And the warning stands above the timings it disqualifies.
        self.assertLess(rendered.index("EVERY COUNTER IS ZERO"), rendered.index("median"))


def _fake_binary(folder, name="fake-leaf", body="", status=0):
    """A stand-in for leaf-server that prints a report and exits as told."""
    fake = pathlib.Path(folder, name)
    fake.write_text(
        "#!/bin/sh\n"
        f"{body}"
        "echo '1 universe(s), 1 work(s), 1 edition(s), 2 entry(ies), 3 chapter(s), 4 page(s)'\n"
        "echo '0 entry(ies) reanalysed'\n"
        f"exit {status}\n"
    )
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
    return fake


class RefusesArgumentsThatNameNothing(unittest.TestCase):
    """Everything the bench is given reaches subprocess or the filesystem.

    A typo in --corpus used to mean three scans of nothing and a report full of zeroes;
    a typo in --binary meant a traceback after the first pass. Both are worth catching
    before the clock starts.
    """

    def test_a_binary_that_is_not_a_file_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError) as refused:
                check_the_arguments(str(pathlib.Path(folder, "nope")), folder, str(pathlib.Path(folder, "i.sqlite")))
            self.assertIn("not a file", str(refused.exception))

    def test_a_binary_that_cannot_be_executed_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            plain = pathlib.Path(folder, "plain")
            plain.write_text("not a program")
            with self.assertRaises(ValueError) as refused:
                check_the_arguments(str(plain), folder, str(pathlib.Path(folder, "i.sqlite")))
            self.assertIn("not executable", str(refused.exception))

    def test_a_corpus_that_is_not_a_directory_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = _fake_binary(folder, body="true\n")
            with self.assertRaises(ValueError) as refused:
                check_the_arguments(str(fake), str(fake), str(pathlib.Path(folder, "i.sqlite")))
            self.assertIn("not a directory", str(refused.exception))

    def test_an_index_whose_folder_is_missing_is_refused(self):
        # Not created on the fly: a typo would then silently make the folder rather
        # than say the path was wrong.
        with tempfile.TemporaryDirectory() as folder:
            fake = _fake_binary(folder, body="true\n")
            with self.assertRaises(ValueError) as refused:
                check_the_arguments(str(fake), folder, str(pathlib.Path(folder, "gone", "i.sqlite")))
            self.assertIn("does not exist", str(refused.exception))

    def test_arguments_that_all_name_what_they_claim_are_allowed(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = _fake_binary(folder, body="true\n")
            binary, corpus, index = check_the_arguments(
                str(fake), folder, str(pathlib.Path(folder, "i.sqlite")))
            self.assertTrue(binary.is_file())
            self.assertTrue(corpus.is_dir())
            self.assertEqual(index.name, "i.sqlite")

class RefusesAnIndexInMemory(unittest.TestCase):
    """`/tmp` is a tmpfs on the server this bench was written for.

    An index there is never written to a disk, so the SQLite writer drops out of the
    measurement without a word — and telling the writer apart from the cores and the
    archives is one of the three things this bench exists for.
    """

    def test_a_tmpfs_index_stops_the_bench(self):
        in_memory = FakeSystem(commands={}, files={}, fstype="tmpfs")
        with self.assertRaises(ValueError) as refused:
            refuse_a_volatile_index(in_memory, "/tmp/bench.sqlite")
        self.assertIn("tmpfs", str(refused.exception))
        self.assertIn("memory", str(refused.exception))

    def test_a_ramfs_index_stops_the_bench_too(self):
        in_memory = FakeSystem(commands={}, files={}, fstype="ramfs")
        with self.assertRaises(ValueError):
            refuse_a_volatile_index(in_memory, "/tmp/bench.sqlite")

    def test_an_index_on_a_real_filesystem_is_allowed(self):
        # ext4 under a home directory — what the corrected plan actually uses, /srv
        # being root-owned on this server.
        refuse_a_volatile_index(
            FakeSystem(commands={}, files={}, fstype="ext4"),
            "/var/lib/bench/bench.sqlite",
        )

    def test_the_refusal_happens_before_any_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder, "calls")
            fake = _fake_binary(folder, body=f"echo \"$@\" >> {log}\n")
            code, said = _bench(
                [
                    "--binary", str(fake),
                    "--corpus", folder,
                    "--db", "/tmp/bench.sqlite",
                ],
                FakeSystem(
                    commands={}, files=dict(SPINNING_USB.files),
                    links=dict(SPINNING_USB.links), fstype="tmpfs", source="/dev/sda1",
                ),
            )
            self.assertEqual(code, 2)
            self.assertIn("tmpfs", said)
            self.assertFalse(log.exists(), "not one pass may run against an index in RAM")


class RunsAPass(unittest.TestCase):
    def test_it_times_the_pass_and_clears_the_index_first(self):
        with tempfile.TemporaryDirectory() as folder:
            fake = _fake_binary(folder)
            index = pathlib.Path(folder, "leaf.sqlite")
            index.write_text("a stale index that would skew the pass")

            seconds, output, status = run_pass(str(fake), "/corpus", str(index))

            self.assertGreaterEqual(seconds, 0.0)
            self.assertEqual(status, 0)
            self.assertEqual(parse_summary(output)["pages"], 4)
            self.assertFalse(index.exists(), "the index must be cleared before the pass")

    def test_it_hands_back_the_exit_status_of_the_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            broken = _fake_binary(folder, body="echo 'error: no library' >&2\n", status=3)
            _, output, status = run_pass(str(broken), "/corpus", str(pathlib.Path(folder, "i.sqlite")))
            self.assertEqual(status, 3)
            self.assertIn("error: no library", output)


class GuardsTheIndex(unittest.TestCase):
    def test_the_production_index_is_refused(self):
        # A pass unlinks the index it is given. Read progress is the only thing in
        # the installation a rescan cannot rebuild.
        with self.assertRaises(ValueError) as refusal:
            refuse_the_production_index("/var/lib/leaf/leaf.sqlite")
        self.assertIn("read progress", str(refusal.exception))

    def test_a_bench_index_of_its_own_is_accepted(self):
        refuse_the_production_index("/srv/bench/index/bench.sqlite")


def _bench(argv, system):
    """`main`, with everything it prints held back so the test output stays readable."""
    printed = io.StringIO()
    with contextlib.redirect_stdout(printed), contextlib.redirect_stderr(printed):
        code = main(argv, system=system)
    return code, printed.getvalue()


class RunsTheBench(unittest.TestCase):
    def test_a_db_under_var_lib_leaf_stops_the_bench_before_any_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder, "calls")
            fake = _fake_binary(folder, body=f"echo \"$@\" >> {log}\n")
            code, said = _bench(
                [
                    "--binary", str(fake),
                    "--corpus", folder,
                    "--db", "/var/lib/leaf/leaf.sqlite",
                ],
                _spinning_usb_for(folder),
            )
            self.assertEqual(code, 2)
            self.assertIn("read progress", said)
            self.assertFalse(log.exists(), "not one pass may run against the real index")

    def test_asking_for_no_pass_at_all_is_refused(self):
        code, said = _bench(
            ["--binary", "/nowhere", "--corpus", "/srv", "--db", "/srv/bench/i.sqlite", "--passes", "0"],
            SPINNING_USB,
        )
        self.assertEqual(code, 2)
        self.assertIn("--passes", said)

    def test_a_disk_that_cannot_be_named_stops_the_bench(self):
        # The arguments all name real things — it is the disk probe that comes up
        # empty, and that alone must stop the bench.
        with tempfile.TemporaryDirectory() as folder:
            fake = _fake_binary(folder, body="true\n")
            nowhere = FakeSystem(commands={}, files={}, source="")
            code, said = _bench(
                [
                    "--binary", str(fake),
                    "--corpus", folder,
                    "--db", str(pathlib.Path(folder, "i.sqlite")),
                ],
                nowhere,
            )
            self.assertEqual(code, 2)
            self.assertIn("findmnt", said)

    def test_a_failing_binary_stops_after_the_first_pass(self):
        # Three full scans of the real library take a quarter of an hour. A binary
        # that does not work must be found out on the first one, with its own words.
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder, "calls")
            broken = _fake_binary(
                folder,
                body=f"echo \"$@\" >> {log}\necho 'error: the index will not open' >&2\n",
                status=1,
            )
            code, said = _bench(
                [
                    "--binary", str(broken),
                    "--corpus", folder,
                    "--db", str(pathlib.Path(folder, "bench.sqlite")),
                    "--passes", "3",
                ],
                _spinning_usb_for(folder),
            )
            self.assertEqual(code, 1)
            self.assertIn("error: the index will not open", said)
            self.assertEqual(log.read_text().splitlines(), [f"scan {folder}"])

    def test_the_two_variants_alternate_instead_of_running_one_after_the_other(self):
        # Running every whole pass first would leave the no-dimensions passes reading
        # a page cache the whole passes warmed, and the gap between them is what the
        # report turns into a verdict on parallelising the reads.
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder, "calls")
            fake = _fake_binary(folder, body=f"echo \"$@\" >> {log}\n")
            code, said = _bench(
                [
                    "--binary", str(fake),
                    "--corpus", folder,
                    "--db", str(pathlib.Path(folder, "bench.sqlite")),
                    "--passes", "2",
                ],
                _spinning_usb_for(folder),
            )
            self.assertEqual(code, 0)
            self.assertIn("first (coldest)", said)
            self.assertEqual(
                log.read_text().splitlines(),
                [
                    f"scan {folder}",
                    f"scan {folder} --no-dimensions",
                    f"scan {folder}",
                    f"scan {folder} --no-dimensions",
                ],
            )



def _mute_binary(folder):
    """A binary that works and says nothing a report can be read out of."""
    fake = pathlib.Path(folder, "mute-leaf")
    fake.write_text("#!/bin/sh\necho 'scanning…'\nexit 0\n")
    fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
    return fake


class TheRealSystem(unittest.TestCase):
    """The three calls the fake System stands in for, against a real disk."""

    def test_it_reads_a_file_and_says_nothing_about_one_it_cannot(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder, "rotational")
            path.write_text("0\n")
            self.assertEqual(System().read(str(path)), "0\n")
            # A probe that is not there is not an error — every disk answers a different
            # subset of them, and `disk_facts` is what decides that none answered.
            self.assertIsNone(System().read(str(pathlib.Path(folder, "absent"))))

    def test_it_resolves_a_link_and_it_looks(self):
        with tempfile.TemporaryDirectory() as folder:
            here = pathlib.Path(folder, "here")
            here.mkdir()
            (here / "link").symlink_to(here)
            self.assertEqual(System().realpath(str(here / "link")), str(here))
            self.assertTrue(System().exists(str(here)))
            self.assertFalse(System().exists(str(pathlib.Path(folder, "nowhere"))))


class StopsBeforeTheClockStarts(unittest.TestCase):
    def test_an_argument_that_names_nothing_stops_the_bench(self):
        # `check_the_arguments` refuses on its own account elsewhere; what is checked here
        # is that the bench acts on the refusal instead of running three scans anyway.
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder, "calls")
            code, said = _bench(
                ["--binary", str(pathlib.Path(folder, "absent")),
                 "--corpus", folder,
                 "--db", str(pathlib.Path(folder, "i.sqlite")),
                 "--passes", "1"],
                _spinning_usb_for(folder),
            )
            self.assertEqual(code, 2)
            self.assertIn("is not a file", said)
            self.assertFalse(log.exists())

    def test_output_that_is_not_a_report_is_refused_after_the_passes(self):
        # It exits 0, so every pass "worked" — and printed no summary line at all. This is
        # the case that used to reach `parse_summary` and come back out as a traceback.
        with tempfile.TemporaryDirectory() as folder:
            code, said = _bench(
                ["--binary", str(_mute_binary(folder)),
                 "--corpus", folder,
                 "--db", str(pathlib.Path(folder, "i.sqlite")),
                 "--passes", "1"],
                _spinning_usb_for(folder),
            )
            self.assertEqual(code, 1)
            self.assertIn("not a report", said)


if __name__ == "__main__":
    unittest.main()
