# Leaf

A reading server for a comics library, and the desktop client that goes with it.

It exists for one thing that reading servers usually get wrong: they make a book both the
thing you open and the thing that carries a number, so *volume 1* and *chapter 1* collide.
A library of 335 files declaring 2 678 chapters has nowhere to put them.

```
contract/   the API, as an OpenAPI file — the only coupling between the blocks
server/     Rust · cargo             the server
desktop/    C++ / QML · cmake        the Ubuntu client
tools/      Python                   the guards that keep the two in step
assets/     fonts and logo, owned by neither
```

Three toolchains, and deliberately no attempt to unify their builds: cargo has no reason to
know that cmake exists. They are in one repository because the contract is the only thing
that couples them, and a contract change should be one commit rather than two.

## The rule that holds everything together

**A file stays the unit of reading, but stops being the unit of numbering.**

```
UNIVERSE          optional, a label rather than a container
└── WORK          the thing you name
    └── EDITION   optional; carries the chapters, the arcs and the progress
        ├── ENTRY   a file: a VOLUME or a standalone CHAPTER
        │   └── CHAPTER   a marker inside a volume, or an entry of its own
        └── ARC     a range of chapters, never a list of volumes
```

A chapter has two possible existences — an entry of its own, or a marker inside a volume —
and occupies a number in the edition either way. That is what lets a series switch from
volumes to loose chapters and back without a break, and what lets a standalone 45.5 read
between 45 and 46.

Two fields do two jobs that are usually confused: **number** identifies and is shared across
editions; **position** orders and belongs to the edition. A side story numbered −108 to −97
that reads at volume 36 sorts correctly on position and wrongly on number.

A "series" in the API is an **edition**. `/series` never answers with a universe or a work.

## The files on disk

The library *is* the truth; the SQLite index is derived and can be rebuilt by rescanning.
Four files carry the metadata, each holding only what belongs to its level:

| File | Where | Holds |
|---|---|---|
| `universe.json` | universe folder | a name, and optionally reading orders |
| `work.json` | work folder | title, authors, artists, status, reading direction, genres, tags, age rating |
| `edition.json` | edition folder | publisher, collection, volume count, language, chapter label pattern, arcs, colour |
| `entry.json` | **inside the CBZ** | number, title, chapters, their start pages and the volume they came from |

`entry.json` lives inside the archive so a volume downloaded for offline reading carries its
own chapter markers — otherwise "jump to chapter 103" stops working on a plane.

**The sidecars decide what a folder is.** `Dragon Ball/{Perfect Edition, Original Edition}`
and `Terres d'Arran/{Elfes, Mages}` have exactly the same shape on disk and opposite
meanings: one is a work in two editions, the other a universe of two works. No heuristic can
tell them apart, which is what the level files are for. A folder that declares nothing and
holds only folders is a **shelf** — walked through, not recorded.

A cover can be chosen on disk rather than taken from page 0: drop `Tome 1.jpg` beside
`Tome 1.cbz`, or `cover.jpg` in the folder to speak for the whole series. Page 0 is right
most of the time and wrong often enough to matter — a colour insert, a scanlation credit —
and overriding it should not mean editing the archive.

`ComicInfo.xml` is read as a fallback, so the server runs on an untouched library.

Search is an FTS5 index inside the same database — ranked by relevance, accents folded in
the tokenizer, and half-typed words already match. It searches titles, authors, artists,
genres, tags and summaries; hits are series, entries and chapters, never a universe or a
work, which are reached through the editions that carry them.

## Running it

```bash
cd server && cargo build --release

LEAF_LIBRARY=/path/to/library ./target/release/leaf-server scan
LEAF_LIBRARY=/path/to/library ./target/release/leaf-server serve
```

`scan` analyses and exits with a report naming every folder it could not classify, every
required field missing and every file whose declared identity disagrees with where it sits.
It refuses nothing — it only stops pretending it found what it did not. `--no-dimensions`
skips measuring every page, which is most of a scan's cost.

`serve` scans in the background, so the server answers from its first second rather than
after the whole library has been read. `POST /scan` answers at once and `GET /scan` says
where it has got to. Nothing here is a durable queue: a scan lost to a restart costs
nothing, because the index is rebuildable from the files.

The binary depends only on `libc`, `libm`, `libgcc_s` and the linker — SQLite is compiled
in, so there is no runtime and no system library to install.

### Configuration

| Variable | Default | |
|---|---|---|
| `LEAF_LIBRARY` | the first root | read and written |
| `LEAF_INBOX` | `../inbox` | transfers in flight — **same filesystem as the library** |
| `LEAF_CACHE` | `../cache` | resized pages, disposable |
| `LEAF_DB` | `data/leaf.sqlite` | the index, rebuildable |
| `LEAF_KEYS` | none | `desktop:secret:read,import  phone:secret:read` — 16 characters minimum, and required to bind anywhere but the loopback |
| `LEAF_HOST` · `LEAF_PORT` | `127.0.0.1` · `8081` | |
| `LEAF_DROP` | none | a folder shared with an application on the same machine |
| `LEAF_MAX_UPLOAD_MB` | `2048` | ceiling on a single file, both upload paths |
| `LEAF_TRUST_PROXY` | off | read `X-Forwarded-For` — only behind **exactly one** proxy that sets it |
| `LEAF_TLS_CERT` · `_KEY` · `_HOSTS` | none | serve HTTPS directly — two PEM files, generated if the certificate is not there |
| `LEAF_JPEG_QUALITY` | `0.85` | |
| `LEAF_MAX_CACHE_MB` | `4096` | ceiling on resized pages, oldest read go first |
| `LEAF_NO_SCAN` | | skip the scan on start |

The inbox must sit on the same filesystem as the library: committing an import is a rename,
instant and atomic. On another volume it becomes a multi-gigabyte copy, and the server warns
at startup when it detects that.

## Security

**A secret string per device**, compared in constant time against the configuration. No
accounts, no passwords, no sign-in screen. Keys carry different rights: a desktop imports, a
phone only reads — so a lost phone gives access to comics, not to writing on the disk. Keys
belong in the applications' settings, never compiled into them.

**Wrong keys are throttled.** Ten failures from one address and it is refused for fifteen
minutes, with `Retry-After` saying how long. Failures age out, so a device that gets it wrong
once a week never accumulates its way into a lockout. A key that works clears the slate.

**It binds the loopback by default,** and refuses to start bound wider with no key
configured — a port open with no key is not a risky setting, it is an open library.

**TLS, if nothing else provides it.** A reverse proxy is the better answer: it holds a
certificate browsers already trust. But a port opened without one would send the key in clear
on every request, so the server can do it itself: set `LEAF_TLS_CERT` and it generates a
self-signed certificate on first start and prints its fingerprint. Pin that in the
applications rather than trusting an authority — it names exactly one server. Two PEM files
and no keystore: the key sits beside the certificate unless `LEAF_TLS_KEY` names it
elsewhere, and it is guarded by its mode — checked on every start, and closed to everybody
but its owner when it is not — rather than by a password to configure, lose, or leave in an
environment variable.

**Uploads have a ceiling**, malformed input answers `400` rather than `500`, and an
unforeseen failure answers `internal error` with the detail logged server-side — an exception
message can carry a path, a query, a piece of the schema.

## The API

The contract lives in [contract/openapi.yaml](contract/openapi.yaml): every route, every
shape, and what each one means. The summary below is the shape of it.

```
GET    /health                        answers without a key, and says what it is
GET    /series                        a "series" is an edition
GET    /series?author=&genre=&medium=&status=&universe=&language=&publisher=&read=
                                      repeat a parameter to widen, add another to narrow
GET    /series?work=                  the other editions of this one
GET    /series?sort=&page=&size=      name | added | updated | volumes · 100 by default,
                                      size=0 for all; `total` always says how many match
GET    /filters                       the values worth offering, each with its count
GET    /series/{id}/entries           volumes and loose chapters, in reading order
GET    /series/{id}/chapters          the whole sequence, whatever the materialisation
GET    /series/{id}/arcs
GET    /series/{id}/cover?width=
GET    /entries/{id}/chapters         a volume's markers
GET    /entries/{id}/pages
GET    /entries/{id}/pages/{n}?width=
GET    /entries/{id}/cover?width=
GET    /entries/{id}/file             the original, stamped with its identity
GET    /entries/{id}/progress   ·  PATCH  ·  DELETE
GET    /next                          what you are reading, then what follows it
GET    /search?q=&kind=&limit=        ranked, accents folded, half-typed words match
GET    /scan                          where a running scan has got to
PATCH  /series/{id}  ·  /series/{id}/arcs  ·  /entries/{id}
POST   /entries                       drop a file, the server proposes a destination
POST   /intake/{id}/file              you confirm, only then is it filed
POST   /import  ·  /import/{id}/commit   a whole folder, resumable
POST   /cleanup  ·  /scan
```

`/health` answers without a key and says what it is:

```json
{ "status": "ok", "api": 1, "format": 1, "library": 12 }
```

`api` is what a client checks on connecting, so a client carrying last month's build says
"update the application" instead of failing obscurely three screens later. There is no `/v1`
prefix on purpose. `format` is the version of the files on disk; migrations of the index are
tracked by SQLite's own `PRAGMA user_version`, each running once, in order, a failure
stopping the start rather than leaving a half-migrated schema behind without a word.

### Two ways in

Both end the same way: the server proposes a destination, and nothing is filed until you
confirm.

**Over the network** — `POST /entries` with the file as the body, resumable in bulk through
`/import` for a whole folder.

**Through a shared folder**, when the application and the server are on the same machine. Set
`LEAF_DROP`, have the application put the file there, and `POST /drop` with its name: the
server renames it into the library. Nine gigabytes already on the disk are not written a
second time, and nothing crosses the loopback. The folder has to sit inside the same mount as
the library, or the rename becomes a copy and the point is lost.

## Behaviour worth knowing before it surprises you

**`?width=` is the width you want per page.** A landscape image is a double-page spread and
gets twice the requested width. Without the parameter the original bytes are served
untouched, and nothing is ever upscaled or returned larger than its source.

**`GET /entries/{id}/file` writes before it serves.** It stamps the archive with its own
identity — work, edition, number — so the file can find its way home when it comes back. That
means a GET modifies something on disk, which no cache, proxy or client expects. Deliberate,
and the stamp has to be in the copy that leaves rather than the one that stays; worth knowing
before putting a caching proxy in front.

**Resized pages are encoded at 4:4:4**, not the 4:2:0 that most encoders choose below quality
90. Halving the colour resolution costs nothing measurable on line art and saves about 40 % of
the bytes, but it is a change to the fidelity of every page the server sends and it has no
business arriving as a side effect. On pure chroma noise — which no page contains — 4:4:4
costs 1 217 KB against 747 KB for an average error of 6.62 against 13.43.

**What a page that is not a JPEG becomes.** The resize path has one output format, so:

| source | asked at a width |
|---|---|
| JPEG, PNG, BMP | decoded, shrunk, re-encoded as JPEG |
| WebP, GIF | **may come back untouched** — re-encoding them can cost more bytes than it saves |
| anything with transparency | flattened onto **white** before encoding |

That last row was a defect once: dropping the alpha channel rather than compositing it left a
PNG saved with a transparent background carrying black under the clear part, so a page came
back with ink where a reader expects paper.

**The approximate search fallback is offered only when a series was among the levels asked
for.** A client that asked for chapters wants "no chapters", not "here is a series you might
have meant". It stays series-only because it has no index behind it and reads what it
compares, so what it reads has to stay bounded by the shelf.

**The page prefetch reads forward.** Requesting pages in order therefore measures warm pages
dressed as cold ones.

## Working on it

```bash
# the server
cd server
cargo fmt --check
cargo clippy --all-targets -- -D warnings   # warnings are errors
cargo test --all-targets
cargo test --test scan -- name_of_the_test  # one test

# the client
cd desktop
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
tests/opens.sh build/leaf-desktop           # linking is not opening

# the guards, from the repository root
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/fixture.py /tmp/leaf                       # a small library on disk
python3 tools/conformance.py <url> <key>                 # every answer against the contract
python3 tools/client_knows_the_contract.py               # every declared field, read by the client
python3 tools/bytes_stay_utf8.py                         # refuses Latin-1 in the client
```

Warnings are errors in every block: `-D warnings` for Rust, `-Werror` for C++.

Tests are named as sentences — `patching_something_that_is_not_there_is_a_404` — and each one
exists because something in that list was once wrong.

**Every literal in the client is `u"…"_s`** — UTF-16, which carries a title in any script.
There is no second form to choose between, and `bytes_stay_utf8.py` refuses the word `Latin1`
anywhere so that there never is one again.

The rule earns its keep because the failure is silent. The sources are UTF-8, so `é` is two
bytes; read as Latin-1 it becomes `Ã©`, a perfectly valid string that is simply the wrong one.
No run-time test catches that — it would have to be a test of every literal ever written — so
the guard checks the *name* rather than the argument. An argument has an unbounded number of
shapes (`"\xC3\xA9"`, the same in octal, a raw string, two adjacent literals, a variable);
the name has one.

## Licence

All rights reserved. This repository is public so the code can be read, not reused — see
[LICENSE](LICENSE). The fonts under `assets/fonts/` are third-party and carry their own SIL
Open Font Licence, included beside them.
