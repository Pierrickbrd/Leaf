#!/usr/bin/env python3
"""Refuses a client test that would speak on the real session bus.

`Notifier` puts `org.freedesktop.Notifications` calls on a D-Bus connection. Read from inside
rather than given, that connection is the session bus — the one belonging to whoever is
running the tests. So `warns_once` built a notifier, called `show`, passed, and put
« Scan terminé · 21 séries » on the desktop of the person running `ctest`. Nine of them
arrived in forty minutes while the branch was being built, every one of them a green test run,
and they read as the server scanning on its own: the only thing that looked wrong was the one
thing that was not.

`crosses_the_seam` reached it the other way round. It calls `Boot::run`, which wires the
notifier to `Toasts`, and several of its tests raise something worth announcing over a window
that has no focus — which is precisely when an event is escalated to the desktop.

What this refuses, therefore, is a bus taken by default:

    Notifier bus;                       ← the session bus, and somebody's desktop
    Boot::run(engine, *qGuiApp);        ← the same, one step removed

Both must be handed one, and the tests hand them `nowhere()`: a connection under a name nobody
registered, never connected, so the notifier is built, asked, and silent — which is also what
it does on a machine with no daemon, the case it exists to survive.

**It refuses to see nothing.** A run that finds no `Boot::run` at all is a broken guard and
says so, rather than passing in silence — which is what `client_knows_the_contract.py` did the
first time its own regex was renamed out from under it.

    tools/tests_stay_off_the_bus.py

Exits non-zero, naming every call with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "desktop" / "tests"

# `Notifier x;` and `Notifier x(...)` with nothing in the parentheses — a declaration that
# takes the default bus. `Notifier::announcement(...)` is static and sends nothing, so the
# word alone is not the thing being refused.
TAKES_THE_DEFAULT_BUS = re.compile(r"\bNotifier\s+\w+\s*(?:;|\(\s*\))")
# `Boot::run(engine, app)` — the two-argument form, which defaults the bus in the same way.
BOOTS_ON_IT = re.compile(r"\bBoot::run\([^(),;]+,[^(),;]+\)")
BOOTS_AT_ALL = re.compile(r"\bBoot::run\s*\(")


def speaking(file: Path) -> list[tuple[int, str]]:
    said: list[tuple[int, str]] = []
    for number, line in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
        # A line of comment explains the rule; it does not break it.
        if line.lstrip().startswith(("//", "///", "*")):
            continue
        if TAKES_THE_DEFAULT_BUS.search(line) or BOOTS_ON_IT.search(line):
            said.append((number, line.strip()))
    return said


def main() -> int:
    files = sorted(TESTS.glob("*.cpp"))
    boots = sum(len(BOOTS_AT_ALL.findall(f.read_text(encoding="utf-8"))) for f in files)

    if not files or boots == 0:
        print(
            "✗ this guard is broken, not the tests: it found no `Boot::run` at all in "
            f"{TESTS.relative_to(ROOT)}. Something moved, and a guard that sees nothing "
            "passes everything.",
            file=sys.stderr,
        )
        return 2

    loud = [(f, at, line) for f in files for at, line in speaking(f)]

    if not loud:
        print(
            f"✓ {boots} boot(s) across {len(files)} test file(s) — every one of them is "
            "handed a bus, and none of them is the session's"
        )
        return 0

    for file, at, line in loud:
        print(f"✗ {file.relative_to(ROOT)}:{at} — {line}", file=sys.stderr)
    print(
        f"\n{len(loud)} place(s) taking the session bus by default. A test that reaches it "
        "puts a notification on the desktop of whoever ran it, and passes while doing so. "
        "Hand it `nowhere()`.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
