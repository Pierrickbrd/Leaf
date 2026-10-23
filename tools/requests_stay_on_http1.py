#!/usr/bin/env python3
"""Refuses a network request this client builds anywhere but `Pinning`.

The server offers HTTP/2 in its TLS handshake, and Qt takes the offer unless a request says
otherwise. Under HTTP/2 one connection carries every request, and what the pin relies on —
a connection that `Pinning::renew` drops and the next request reopens through a handshake
judged against the new pin — is a property nobody has measured there. So every request is
built by `Pinning::request`, which holds it to HTTP/1.1, and a request built any other way
is one that may ride HTTP/2 with the key in it.

**It looks at the client's sources, not its tests.** A test builds whatever request it needs
to prove something, the HTTP/2 offer included.

**It refuses to see nothing.** `Pinning.cpp` must itself set the attribute; a rename that left
this guarding a helper that no longer does would otherwise pass in silence.

    tools/requests_stay_on_http1.py

Exits non-zero, naming every place with its line.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "desktop" / "src"
OWNER = "Pinning.cpp"
# Its header declares the helper, which reads like a request made.
OWNED_BY_IT = {OWNER, "Pinning.h"}

# A request made: a temporary `QNetworkRequest(url)` or `QNetworkRequest{url}`, a variable
# constructed `QNetworkRequest r(url)` / `r{url}` / `r;`, or one assigned from anything but
# the helper. Not a parameter (`const QNetworkRequest &`), nor `QNetworkRequest::Attribute`.
MADE = re.compile(
    r"\bQNetworkRequest\s*[({]"
    r"|\bQNetworkRequest\s+\w+\s*[({;]"
    r"|\bQNetworkRequest\s+\w+\s*=(?!\s*Pinning::request\()"
)


def made_here(path: Path):
    """Every line of one file that makes a request."""
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = line.split("//", 1)[0]
        if MADE.search(code):
            yield number, line.strip()


def main() -> int:
    owner = SOURCES / OWNER
    if not owner.is_file() or "Http2AllowedAttribute" not in owner.read_text(encoding="utf-8"):
        print(f"✗ desktop/src/{OWNER} no longer holds requests to HTTP/1.1: this guard "
              "would guard nothing.")
        return 1

    found = [
        (path.relative_to(ROOT), number, line)
        for path in sorted(SOURCES.rglob("*"))
        if path.suffix in {".h", ".cpp"} and path.name not in OWNED_BY_IT
        for number, line in made_here(path)
    ]
    for path, number, line in found:
        print(f"✗ {path}:{number}\n    {line}")
    if found:
        print(f"\n{len(found)} request(s) built outside Pinning: use Pinning::request, "
              "which holds them to HTTP/1.1.")
        return 1

    print("Every request of the client is built by Pinning::request.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
