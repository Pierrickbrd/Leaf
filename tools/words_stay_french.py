#!/usr/bin/env python3
"""Refuses the French typography faults `Words.h` promises and nothing enforced.

`writes_french.cpp::nothing_carries_a_straight_apostrophe` sweeps French strings, but it
sweeps by *calling* each function, once, by name, into one `QStringList`. A function never
added to that list is never swept. Measured: `Words.h` declares 107 functions, and 22 of them
are named nowhere in `writes_french.cpp` — the sweeper is blind to a fifth of what it is
supposed to guard, and stays green regardless, because it never reads a fact out of
`Words.cpp` itself; it only reads what a human remembered to list.

This guard reads `Words.cpp` instead of calling anything, so a function is covered from the
day it is written, not from the day somebody remembers to name it in a list.

`Words.h`'s own header states three habits and says they are "visible the moment they are
wrong and invisible when they are right." The third, a curly apostrophe, was already swept.
The second was not, and it had drifted: an ordinary space instead of a non-breaking one
before `:` or `;`, in four places, and a guillemet with no non-breaking space beside it, in
five. Nothing said so before this file existed.

**A first version of this guard matched only a literal token immediately followed by `_s`.**
That is how most of `Words.cpp` writes a string, but not all of it: a literal split across
several adjacent `u"…"` tokens — with only the last one carrying `_s` — is concatenated by
the compiler into one string at translation time, and is one string at runtime, but that
first version read only the suffixed fragment and never looked at the ones before it. Two
faults hid there, an ordinary space before `;` in `noSeriesForThisFile()` and
`verifyingMeans()`, each sitting in a fragment that fragment-only version never opened —
found only once this guard was made to check its own blind spots rather than trust the one
it already knew about.

The rule, from that header, on every literal `u"…"_s` in `Words.cpp` — a maximal run of
adjacent `u"…"` tokens, recombined into the one string the compiler builds from them, that
ends in `_s`:

  1. no straight apostrophe;
  2. no ordinary space immediately before `:` `;` `!` `?` — `+ Words::Nbsp + u": "_s` is the
     correct composition and never trips this, because the literal then starts on the sign
     itself, not on a space in front of it;
  3. every `«` is immediately followed by U+00A0, and every `»` is immediately preceded by
     it, inside that same recombined string — including across the seam between two
     fragments, now that they are read as one instead of separately.

**And it protects itself, twice over.** `client_knows_the_contract.py` names the failure both
checks guard against: the shape of a literal in this client has been rewritten three times
(`_L1` → `_ascii` → `u"…"_s`), and the first rewrite left that guard's pattern matching
nothing — every field read as covered while the guard actually read none of them.

  - If it recombines zero literals at all, it says the pattern itself no longer matches how
    a string is written — that failure, in full.
  - If it recombines *some*, but a raw `u"…"` token joins none of them — adjacent to nothing
    that ends in `_s`, or ending a run that never does — it says so instead of silently
    dropping that token. This is the narrower form of the same failure, and the one this
    round actually found: not every literal invisible, just a fragment of two of them.

Neither check can see a literal written in a form that does not start with `u"` at all — a
wholesale move to some other way of quoting a string would need a first fault to slip
through unrecombined before this guard could say so, the same way `client_knows_the_contract.py`
only speaks once every field it watches has gone unread.

    tools/words_stay_french.py

Exits non-zero, listing every literal that breaks the rule.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "desktop" / "src" / "Words.cpp"

NBSP = " "

# One raw string-literal token, suffix not required here: the suffix can sit on any fragment
# of an adjacent-token literal, and matching only the suffixed one — as an earlier version of
# this guard did — is exactly how a fault sitting in an earlier fragment went unseen.
TOKEN = re.compile(r'u"((?:\\.|[^"\\])*)"')

# The `_s` user-defined-literal suffix, not a word that merely starts with it: `_s` is how
# `Qt::Literals::StringLiterals` spells "this is a QString", and `_saved` is not that.
SUFFIX = re.compile(r"_s(?!\w)")

SPACE_BEFORE_PUNCTUATION = re.compile(r" [:;!?]")


def runs_of_adjacent_tokens(source: str):
    """Every maximal run of `u"…"` tokens with nothing but whitespace between them — what the
    compiler concatenates into one string literal, in translation phase 6, before `_s` is
    ever applied to what results."""
    run = []
    for token in TOKEN.finditer(source):
        if run and source[run[-1].end():token.start()].strip() == "":
            run.append(token)
        else:
            if run:
                yield run
            run = [token]
    if run:
        yield run


def literals_in(source: str):
    """Every complete `u"…"_s` literal in `source` — a run whose last token carries `_s` —
    recombined into one string and paired with the 1-based line its first token starts on,
    alongside the count of raw tokens that belonged to no such run. A run without `_s`
    anywhere in it is not a literal this guard can vouch for; it is a sign that some way of
    writing one has stopped being recognised.
    """
    literals = []
    orphaned = 0
    for run in runs_of_adjacent_tokens(source):
        last = run[-1]
        if SUFFIX.match(source, last.end()):
            line = source.count("\n", 0, run[0].start()) + 1
            literals.append((line, "".join(token.group(1) for token in run)))
        else:
            orphaned += len(run)
    return literals, orphaned


def straight_apostrophe(text: str) -> bool:
    return "'" in text


def breaking_space_before_punctuation(text: str) -> bool:
    return SPACE_BEFORE_PUNCTUATION.search(text) is not None


def guillemet_without_nbsp(text: str) -> bool:
    """Whether some « in `text` has no U+00A0 right after it, or some » has none right before."""
    for opening in re.finditer("«", text):
        if text[opening.end():opening.end() + 1] != NBSP:
            return True
    for closing in re.finditer("»", text):
        if text[max(0, closing.start() - 1):closing.start()] != NBSP:
            return True
    return False


# One entry per literal per rule it breaks, not one per offending character — a literal with
# two bad guillemets is one fault to fix, and the count should read that way.
RULES = (
    ("a straight apostrophe", straight_apostrophe),
    ("an ordinary space before : ; ! ?", breaking_space_before_punctuation),
    ("« or » without its non-breaking space", guillemet_without_nbsp),
)


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    literals, orphaned = literals_in(source)

    # None at all does not mean Words.cpp lost every string in it — it means this pattern
    # stopped matching how a literal is written there, and is about to wave every fault
    # through unseen. This is the exact shape of the failure `client_knows_the_contract.py`
    # had once, quietly, and the reason both guards check it now.
    if not literals:
        print(f"✗ this guard is broken, not {SOURCE.relative_to(ROOT)}: it recombined no "
              f"literal at all.\n  {TOKEN.pattern} no longer matches how a string is "
              f"written there.")
        return 2

    # Some literals, but a raw `u"…"` token that joined none of them — the narrower form of
    # the same failure: not every literal gone unrecognised, just a fragment of one, the way
    # `noSeriesForThisFile()` and `verifyingMeans()` were before this check existed.
    if orphaned:
        print(f"✗ this guard is broken, not {SOURCE.relative_to(ROOT)}: {orphaned} raw "
              f"u\"…\" token(s) joined no literal ending in `_s`.\n  Some way of writing one "
              f"has changed and this guard no longer recognises it.")
        return 2

    found = [(line, why, text)
             for line, text in literals
             for why, broken in RULES
             if broken(text)]

    for line, why, text in found:
        print(f"✗ {SOURCE.relative_to(ROOT)}:{line} — {why}\n    {text}")

    if found:
        print(f"\n{len(found)} literal(s) breaking a rule Words.h states and nothing else "
              "enforced.")
        return 1

    print(f"Every literal in {SOURCE.relative_to(ROOT)} keeps its French typography.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
