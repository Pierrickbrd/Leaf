#!/usr/bin/env python3
"""Times a scan, and never records a number without the disk that produced it.

A cause written from a plausible mechanism rather than from a measurement can stand false
for a long time before anyone notices. A scan time on its own says nothing. It says something when it arrives with the
device, its `rotational` flag, the driver behind it and the read-ahead in force — which is
what separates "the code is slow" from "the disk is slow".

    tools/scan_bench.py --binary ./leaf-server --corpus /path/to/library \
        --db /var/lib/bench/bench.sqlite
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import time

# The format of `server/src/scan/report.rs:46`, word for word.
_HEADER = re.compile(
    r"(\d+)\s+universe\(s\),\s*(\d+)\s+work\(s\),\s*(\d+)\s+edition\(s\),\s*"
    r"(\d+)\s+entry\(ies\),\s*(\d+)\s+chapter\(s\),\s*(\d+)\s+page\(s\)"
)
_REANALYSED = re.compile(r"(\d+)\s+entry\(ies\)\s+reanalysed")

_COUNTERS = ("universes", "works", "editions", "entries", "chapters", "pages")

_HOST = re.compile(r"/host(\d+)/")

_SYSFS_BLOCK = "/sys/block"

# Where the production index lives. Every pass starts by deleting the index it is given,
# and read progress is the one thing in the whole installation a rescan cannot rebuild.
_PRODUCTION_INDEX = "/var/lib/leaf"

# The two variants, in the order one round runs them.
_VARIANTS = (("whole", False), ("no dimensions", True))

# Systèmes de fichiers en mémoire : un index posé là ne touche aucun disque.
_VOLATILE = ("tmpfs", "ramfs", "devtmpfs")


def parse_summary(text):
    """The seven counters of a scan report.

    Refuses output that is not a report rather than reading it as an empty one: a binary
    that failed would otherwise pass for a scan that found nothing.
    """
    header = _HEADER.search(text)
    if header is None:
        raise ValueError(
            "this output carries no report header — the scan most likely failed"
        )
    counts = {name: int(value) for name, value in zip(_COUNTERS, header.groups())}
    reanalysed = _REANALYSED.search(text)
    counts["reanalysed"] = int(reanalysed.group(1)) if reanalysed else 0
    return counts


def stats(times):
    """The minimum, the median and the maximum — not the mean.

    The median, because one pass slowed down by something else on the machine must not move
    the number that gets written down.
    """
    if not times:
        raise ValueError("no pass to sum up")
    ordered = sorted(times)
    middle = len(ordered) // 2
    median = (
        ordered[middle]
        if len(ordered) % 2
        else (ordered[middle - 1] + ordered[middle]) / 2
    )
    return {"passes": len(ordered), "min": ordered[0], "median": median, "max": ordered[-1]}


class System:
    """The real I/O, isolated so that everything else tests without a disk."""

    def run(self, *cmd):
        """What the command printed on stdout, or nothing at all.

        A command that is not installed answers nothing rather than raising: the callers
        refuse on an empty answer, and say which fact they could not establish — which is
        more use than a traceback about `findmnt`.
        """
        try:
            return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout
        except OSError:
            return ""

    def read(self, path):
        try:
            with open(path, encoding="utf-8") as handle:
                return handle.read()
        except OSError:
            return None

    def realpath(self, path):
        return os.path.realpath(path)

    def exists(self, path):
        return os.path.exists(path)


def _int_or_none(text):
    """The integer a sysfs file holds, or None — never an exception.

    sysfs answers with a word, a range or nothing at all often enough that a traceback
    here would cost a whole run of measurements.
    """
    try:
        return int(text.strip())
    except (AttributeError, ValueError):
        return None


def disk_facts(system, path):
    """The disk behind a path, as it will have to be quoted beside the measurement.

    Raises `ValueError` rather than answering half. `rotational` is the single field that
    decides whether parallelising the scan can mean anything, and a probe that returned
    nothing must never be printed as `no`: a confident wrong answer is worse here than no
    answer at all.
    """
    source = system.run("findmnt", "-no", "SOURCE", "--target", path).strip()
    if not source:
        raise ValueError(
            f"cannot name the device holding {path}: "
            f"`findmnt -no SOURCE --target {path}` answered nothing"
        )

    # Through the symlink first. /dev/mapper/ubuntu--vg-ubuntu--lv is a name sysfs has
    # never heard of; the /dev/dm-0 it points at is a directory under /sys/block. A dm
    # target has no PKNAME either, so the basename of the resolved path is what carries.
    resolved = system.realpath(source)
    parent = system.run("lsblk", "-ndo", "PKNAME", resolved).strip()
    base = parent or os.path.basename(resolved)

    block = f"{_SYSFS_BLOCK}/{base}"
    if not system.exists(block):
        raise ValueError(
            f"cannot name the disk holding {path}: {source} resolves to {resolved}, "
            f"whose base is {base!r}, and {block} does not exist — sysfs does not know "
            "this device (a tmpfs, a network mount, or a name nothing resolved)"
        )

    rotational = _int_or_none(system.read(f"{block}/queue/rotational"))
    if rotational is None:
        raise ValueError(
            f"cannot tell whether the disk holding {path} rotates: "
            f"{block}/queue/rotational is unreadable, or holds no number. "
            "No timing is worth recording without that field"
        )

    transport = system.run("lsblk", "-ndo", "TRAN", f"/dev/{base}").strip() or None

    driver = None
    link_mbps = None
    if transport == "usb":
        real = system.realpath(f"{_SYSFS_BLOCK}/{base}")
        host = _HOST.search(real + "/")
        if host:
            # proc_name says literally "uas" or "usb-storage".
            name = system.read(f"/sys/class/scsi_host/host{host.group(1)}/proc_name")
            driver = (name or "").strip() or None
        # The speed lives on the USB device directory, somewhere above the block
        # device: walk up until one carries `speed`. Longest match first, so this
        # finds the device rather than the root hub it hangs off.
        parts = real.split("/")
        for cut in range(len(parts), 2, -1):
            speed = system.read("/".join(parts[:cut]) + "/speed")
            if speed:
                link_mbps = _int_or_none(speed)
                break

    return {
        "source": source,
        "resolved": resolved,
        "base": base,
        "rotational": bool(rotational),
        "transport": transport,
        "driver": driver,
        "link_mbps": link_mbps,
        "read_ahead_kb": _int_or_none(system.read(f"{block}/queue/read_ahead_kb")),
    }


def refuse_the_production_index(db):
    """Refuses an index the bench is not allowed to delete.

    Every pass starts by unlinking the index it is given. In production that index is
    `/var/lib/leaf/leaf.sqlite`, which holds read progress — the only thing in the whole
    installation a rescan cannot rebuild.
    """
    resolved = os.path.realpath(db)
    root = os.path.realpath(_PRODUCTION_INDEX)
    if resolved == root or resolved.startswith(root + os.sep):
        raise ValueError(
            f"refusing --db {db} ({resolved}): it lives under {_PRODUCTION_INDEX}, where "
            "the production index does. Every pass deletes the index it is given, and "
            "read progress is the one thing a rescan cannot rebuild. Point --db at a "
            "bench index of its own, on the internal disk"
        )


def refuse_a_volatile_index(system, db):
    """Refuses an index that would live in RAM.

    `/tmp` is a tmpfs on the server this bench was written for. An index put there never
    touches a disk, so the SQLite writer — one of the three things this bench exists to tell
    apart — disappears from the measurement without a word. The scan looks faster and the
    reason is invisible.
    """
    kind = system.run("findmnt", "-no", "FSTYPE", "--target", os.path.dirname(os.path.realpath(db)) or "/").strip()
    if kind in _VOLATILE:
        raise ValueError(
            f"refusing --db {db}: it sits on a {kind}, which is memory, not a disk. The "
            "index would never be written anywhere, and the SQLite writer would vanish "
            "from the measurement. Point --db at a real disk — the internal one, as "
            "production does"
        )


def run_pass(binary, corpus, db, no_dimensions=False):
    """One scan pass, on a fresh index.

    The index is cleared first: a scan that finds everything already in place does not
    measure the same thing as a first scan, and mixing the two gives neither.

    Returns the wall seconds, what the binary printed, and its exit status — the status
    because a binary that fails must be found out after one pass, not after three.
    """
    for suffix in ("", "-wal", "-shm", "-journal"):
        pathlib.Path(db + suffix).unlink(missing_ok=True)

    command = [binary, "scan", corpus]
    if no_dimensions:
        command.append("--no-dimensions")

    env = {**os.environ, "LEAF_DB": db}
    started = time.perf_counter()
    done = subprocess.run(command, capture_output=True, text=True, check=False, env=env)
    seconds = time.perf_counter() - started
    return seconds, done.stdout + done.stderr, done.returncode


def _every_counter_is_zero(measures):
    """True when the passes counted something, and everything they counted was zero."""
    counted = [
        value
        for measure in measures.values()
        for value in (measure.get("counts") or {}).values()
    ]
    return bool(counted) and not any(counted)


def render_report(corpus, facts, measures):
    """The disk first, the numbers after. Never the other way round."""
    rotational = "unknown" if facts["rotational"] is None else (
        "yes" if facts["rotational"] else "no"
    )
    link = f"{facts['link_mbps']} Mbit/s" if facts["link_mbps"] else "—"
    read_ahead = (
        "unknown" if facts["read_ahead_kb"] is None else f"{facts['read_ahead_kb']} kB"
    )

    lines = ["Scan bench", ""]

    if _every_counter_is_zero(measures):
        lines += [
            "  !! EVERY COUNTER IS ZERO — these passes scanned nothing at all.",
            "  !! Check --corpus: a path that holds no library scans in under a second",
            "  !! and reports timings that measure nothing. Do not record these numbers.",
            "",
        ]

    lines += [
        f"  corpus       {corpus}",
        f"  device       {facts['source']}  (base {facts['base']})",
        f"  rotational   {rotational}",
        f"  transport    {facts['transport'] or 'unknown'}",
        f"  driver       {facts['driver'] or '—'}",
        f"  link         {link}",
        f"  read-ahead   {read_ahead}",
        "",
    ]

    for label, measure in measures.items():
        summary = measure["stats"]
        lines.append(
            f"  {label:<16} median {summary['median']:.1f} s"
            f"   (min {summary['min']:.1f} · max {summary['max']:.1f}"
            f" · {summary['passes']} pass(es))"
        )
        if measure.get("first") is not None:
            lines.append(f"                   first (coldest) {measure['first']:.1f} s")
        if measure.get("counts"):
            lines.append(
                "                   "
                + ", ".join(f"{value} {name}" for name, value in measure["counts"].items())
            )

    if any(measure.get("first") is not None for measure in measures.values()):
        lines += [
            "",
            "  Passes after the first are warm: they read the page cache the first filled.",
        ]

    whole = measures.get("whole")
    without = measures.get("no dimensions")
    if whole and without:
        gap = whole["stats"]["median"] - without["stats"]["median"]
        if gap > 0:
            share = 100 * gap / whole["stats"]["median"]
            lines += [
                "",
                f"  Reading the pages costs {gap:.1f} s, which is {share:.0f} % of the scan.",
                "  If that share is small, parallelising the reads will buy nothing,",
                "  whatever the disk.",
            ]
        else:
            lines += [
                "",
                f"  Skipping the pages did not make the scan faster: "
                f"{without['stats']['median']:.1f} s without dimensions against "
                f"{whole['stats']['median']:.1f} s whole.",
                "  Reading the pages is not where this scan spends its time, so",
                "  parallelising the reads has nothing to win here.",
            ]

    return "\n".join(lines)


def main(argv=None, system=None):
    parser = argparse.ArgumentParser(
        description="Times a scan, and names the disk that produced the number."
    )
    parser.add_argument("--binary", required=True, help="the leaf-server to measure")
    parser.add_argument("--corpus", required=True, help="the library root to scan")
    parser.add_argument("--db", required=True, help="the index — on the internal disk, always")
    parser.add_argument("--passes", type=int, default=3)
    args = parser.parse_args(argv)

    if args.passes < 1:
        print(
            f"--passes must be at least 1, and is {args.passes}: "
            "there would be nothing to time.",
            file=sys.stderr,
        )
        return 2

    try:
        refuse_the_production_index(args.db)
    except ValueError as refusal:
        print(str(refusal), file=sys.stderr)
        return 2

    system = system or System()
    try:
        refuse_a_volatile_index(system, args.db)
    except ValueError as refusal:
        print(str(refusal), file=sys.stderr)
        return 2

    try:
        facts = disk_facts(system, args.corpus)
    except ValueError as refusal:
        print(str(refusal), file=sys.stderr)
        return 2

    times = {label: [] for label, _ in _VARIANTS}
    outputs = {label: "" for label, _ in _VARIANTS}

    # Interleaved, one pass of each variant per round. Running one variant to the end
    # first would leave the second reading a page cache the first had warmed, and the
    # gap between the two is exactly what the report turns into a verdict on
    # parallelising the reads.
    for _ in range(args.passes):
        for label, no_dimensions in _VARIANTS:
            seconds, output, status = run_pass(
                args.binary, args.corpus, args.db, no_dimensions
            )
            if status != 0:
                print(
                    f"the {label} pass failed: {args.binary} exited {status}. "
                    "Stopping here rather than timing a binary that does not work.",
                    file=sys.stderr,
                )
                print(output.strip(), file=sys.stderr)
                return 1
            times[label].append(seconds)
            outputs[label] = output

    try:
        measures = {
            label: {
                "stats": stats(times[label]),
                "counts": parse_summary(outputs[label]),
                "first": times[label][0],
            }
            for label, _ in _VARIANTS
        }
    except ValueError as refusal:
        print(f"the passes ran, but their output is not a report: {refusal}", file=sys.stderr)
        return 1

    print(render_report(args.corpus, facts, measures))
    return 0


if __name__ == "__main__":
    sys.exit(main())
