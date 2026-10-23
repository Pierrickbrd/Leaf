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
| `universe.json` | universe folder | a name, a default order, and named reading orders |
| `work.json` | work folder | title, authors, artists, status, reading direction, genres, tags, age rating |
| `edition.json` | edition folder | publisher, collection, volume count, language, chapter label pattern (`Chap. {n}` when none is given), arcs, colour |
| `entry.json` | **inside the CBZ** | number, title, chapters, their start pages and the volume they came from |

`entry.json` lives inside the archive so a volume downloaded for offline reading carries its
own chapter markers — otherwise "jump to chapter 103" stops working on a plane.

**The three folder files also carry the folder's identity**, under `id`. The scan writes one
the first time it meets a folder without one, creating the file if there is none, and never
touches it again. That is what makes a rename survivable: identifiers used to be the hash of
the absolute path, so renaming `Death Note` to `Death Note (VF)` gave every row a new
identity, the old ones were pruned, and the reading positions went with them — the one thing
a rescan cannot rebuild. A folder copied *without* its sidecars is therefore a new folder,
and two folders carrying the same `id` are reported rather than merged. A library mounted
read-only is indexed exactly as before, by path, and the scan report says so.

An entry has no identifier written anywhere: it is « this file, in this edition », so nothing
is rewritten inside thousands of archives for one field. Renaming a *file* is therefore a new
entry, and its reading position is lost — deliberately, because following a file through a
rename would mean identifying it by its contents.

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

### Universe reading orders

An order belongs to an universe and contains ordered **work segments**, not a flat list of
editions. The same work may appear more than once, so this can describe “work A, part 1;
work B, part 1; work A, part 2”.

```json
{
  "leaf": 1,
  "name": "An example universe",
  "defaultOrder": "recommended",
  "orders": [
    {
      "id": "recommended",
      "name": "Recommended order",
      "steps": [
        { "work": "Series A", "unit": "CHAPTER", "from": 1, "to": 120 },
        { "work": "Series B", "unit": "CHAPTER", "from": 1, "to": 55 },
        { "work": "Series A", "unit": "CHAPTER", "from": 121 }
      ]
    }
  ]
}
```

A whole work is a step without a range:

```json
{ "work": "Dragon Ball" }
```

The rules are:
- an order lists only works currently present in the universe; a future work is added to
  the order when it is imported, not predeclared as an unresolved step;

- `work` is the work-folder path relative to the universe, not its display title;
- ranges are inclusive; an omitted `from` means the start and an omitted `to` means the
  current and future end;
- chapter ranges are canonical because chapter numbers identify story content across
  differently divided editions;
- a `CHAPTER` step never names an edition;
- a `VOLUME` step always names its `edition`, because “volumes 1–7” describes different
  content in a 42-volume edition and a 34-volume edition;
- a reader's preferred edition is user state, not universe metadata, and progress remains
  attached to entries and editions;
- an universe may offer several named orders, with one optional `defaultOrder`;
- missing references and invalid ranges belong in the scan report and must not prevent
  unrelated content from being indexed.

`GET /universes` lists them with how many orders each declares, and
`GET /universes/{id}/orders` answers one universe's orders with their steps — two queries
whatever the answer holds.

A step that cannot be resolved is **left out and reported**, never guessed at: a work the
universe does not hold, a volume range naming no edition, a chapter range naming one, a
range running backwards. The scan report says which order and which step, and everything
else in the library is indexed regardless. An order that reaches a client is therefore
always walkable.

The rows are derived and rewritten whole at every scan — `universe.json` is the truth, and
a half-updated order is an order that reads wrong.

For Dragon Ball, the declaration can remain deliberately simple:

```json
{
  "defaultOrder": "main",
  "orders": [
    {
      "id": "main",
      "name": "Main order",
      "steps": [{ "work": "Dragon Ball" }]
    }
  ]
}
```

Search is an FTS5 index inside the same database — ranked by relevance, accents folded in
the tokenizer, and half-typed words already match. It searches titles, authors, artists,
genres, tags and summaries; hits are series, entries and chapters, never a universe or a
work, which are reached through the editions that carry them.

## Installing Leaf

On Ubuntu, install the server, the client, or both with one command. No checkout is
required:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Pierrickbrd/Leaf/main/install.sh) server
bash <(curl -fsSL https://raw.githubusercontent.com/Pierrickbrd/Leaf/main/install.sh) client
bash <(curl -fsSL https://raw.githubusercontent.com/Pierrickbrd/Leaf/main/install.sh) all
```

The command downloads the Leaf sources into a temporary directory, builds the selected
component and removes those sources afterwards. The server command asks for the public
address of the server, the one devices type to reach it (through a proxy or a tunnel as
much as directly; leave it empty if you do not know it yet), writes it as `LEAF_PUBLIC_URL`
in `/etc/leaf/server.env` (mode `0600`), starts the service and ends with a first
`leaf://` link and its QR code, made by `leaf-server device add`: open it on the device to
link, or type its code there. It is valid ten minutes, once; run
`sudo leaf-server device add` again for the next device. On `127.0.0.1` without a public
address there is no link to give, and the command says so. No key is written into
`LEAF_KEYS` any more; the keys already there keep working.

The client command asks for nothing: the client opens on its own screen, from which it is
linked to a library, and opens `leaf://` links. `--address` and `--key-file` still write
`~/.config/Leaf/leaf.conf` (mode `0600`) for the scripts that pass them.

**A client and a server on one machine link themselves**, with no code, in whichever order
they are installed: `all`, `client` where the server already is, or `server` (and `update
server`) where the client already is. The installer knows what it installed by what it
wrote — `/usr/local/bin/leaf-server` and its unit for the server,
`/usr/local/share/leaf/client.version` for the client — and enrolls the client of the person
running it, its key in `~/.config/Leaf/leaf.conf`. Only for a server on the loopback, which
that client reaches at `http://127.0.0.1` with no certificate to pin; a server on the
network is linked from the link `device add` prints. A client that already holds a key keeps
it, one that linked itself from its own screen is left alone, and a run that fails after
enrolling takes the device back.

**Where the library goes.** `--library` chooses it. Without the option the server command
asks at a terminal and otherwise takes `/srv/leaf/library`; an update keeps the one already
installed. Its inbox and drop folders are created beside it, in the same parent. Paths under
`/home`, `/root` and `/run/user` are refused: the service cannot see them, and a library it
cannot see is an empty one, whose scan would prune the reading progress. Every folder above
the library must belong to root alone, with no group write and no ACL granting write:
root creates and shares folders there, and any other account able to write in it could
swap one for a link first. On a disk mounted as a person's, `sudo chown root:root` the
mount point, or put the library elsewhere.

**Which account runs it.** A new installation runs the server as `leaf`, a system account
created on the spot with no password, no shell and no home, so that a flaw in the server
reaches the library and nothing else the machine holds. `--user` names another account. An
update keeps the account the installed service already runs as.

**Sharing the files with a person.** `--share-with alice` adds `alice` to the `leaf` group
and gives the folders the installer creates an ACL, the default one included, so that what
she copies in and what the server writes stay writable by both. It covers the library
files, not the index nor the keys. `all` shares with the person running it. The installer
never changes the permissions of a library that already exists: it prints the `chown` and
`setfacl` commands to run instead.

```bash
sudo ./install.sh server --library /data/comics/library --share-with alice
```

**Building on one machine, installing on another.** The server command builds from the
sources, which installs Rust and `build-essential` on the machine. To keep a server free of
both, build where the toolchain already is and hand the binary over:

```bash
cd server && cargo build --release
scp target/release/leaf-server server.example:/tmp/
sudo ./install.sh server --binary /tmp/leaf-server
```

The installer checks that the file is a `leaf-server` and records the version it reports.

The safe default listens only on `127.0.0.1`. For a remote server, bind Leaf to a private
network address reachable from the client; anywhere but the loopback Leaf speaks only
https, with a certificate whose fingerprint the link carries:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/Pierrickbrd/Leaf/main/install.sh) server --host 100.64.0.10
```

For a public hostname, put Leaf behind a TLS reverse proxy and give the public URL at the
question, or with `--address https://leaf.example.net`. An update adds `LEAF_PUBLIC_URL` to
an existing `server.env` only when the line is missing and there is a terminal to ask, or
the option gives it; it never rewrites the rest.

Leaf currently has one product version for the two independently installable components.
The server and client record their installed versions separately and both expose them:

```bash
leaf-server --version
leaf-desktop --version
cat /usr/local/share/leaf/server.version
cat /usr/local/share/leaf/client.version
```

Each installation also provides `leaf-install`. During the pre-1.0 phase, an update
downloads the current `main` branch and replaces only the component installed on that
machine:

```bash
leaf-install update server
leaf-install update client
leaf-install update all
```

From a source checkout, the equivalent commands are `./install.sh server`,
`./install.sh client` and `./install.sh all`. Release notes are kept in
[`CHANGELOG.md`](CHANGELOG.md).

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
| `LEAF_DB` | `data/leaf.sqlite` | the index **and** the devices and reading progress — back it up |
| `LEAF_KEYS` | none | `desktop:secret:read,import  phone:secret:read` — 16 characters minimum; optional now that devices link themselves, and the keys already there keep working |
| `LEAF_HOST` · `LEAF_PORT` | `127.0.0.1` · `8081` | anywhere but the loopback, Leaf speaks https |
| `LEAF_PUBLIC_URL` | none | the address devices type to reach Leaf, behind a proxy or a tunnel as much as directly — what `device add` puts in its link |
| `LEAF_DROP` | none | a folder shared with an application on the same machine |
| `LEAF_MAX_UPLOAD_MB` | `2048` | ceiling on a single file, both upload paths |
| `LEAF_TRUST_PROXY` | off | read `X-Forwarded-For` — only behind **exactly one** proxy that sets it |
| `LEAF_TLS_CERT` · `_KEY` · `_HOSTS` | `tls/leaf.crt` · `tls/leaf.key` beside `LEAF_DB`, off the loopback | the certificate Leaf serves with — two PEM files, generated if the certificate is not there |
| `LEAF_IMAGE_QUALITY` | `0.75` | quality of resized pages, which are lossy WebP; `LEAF_JPEG_QUALITY` is ignored, and said so at startup |
| `LEAF_MAX_CACHE_MB` | `4096` | ceiling on resized pages, oldest read go first |
| `LEAF_NO_SCAN` | | skip the scan on start |

The inbox must sit on the same filesystem as the library: committing an import is a rename,
instant and atomic. On another volume it becomes a multi-gigabyte copy, and the server warns
at startup when it detects that.

**The database is no longer only an index.** It used to be rebuildable from the files and
nothing else; it now also holds the reading progress and the linked devices, which a rescan
cannot give back. Back up `LEAF_DB` (the folder that holds it, `tls/` included) the way you
back up the library.

## Security

**A key per device, made by linking.** No accounts, no passwords, no sign-in screen: a
device is linked once, receives a random key of its own, and the server keeps only a digest
of it, compared in constant time. The key belongs in the application's keychain, never
compiled into it and never printed. What a device may do is decided by the profile
it sits on (see *Profiles*, below), not by its key. The keys of `LEAF_KEYS` keep working,
with the rights they were given, and read as the principal profile.

**Wrong keys are throttled.** Ten failures from one address and it is refused for fifteen
minutes, with `Retry-After` saying how long. Failures age out, so a device that gets it wrong
once a week never accumulates its way into a lockout. A key that works clears the slate.

**The library is closed as soon as a device is linked, and stays closed.** Until then, on
the loopback, it answers without a key, so a machine that serves only itself needs no setup.
Revoking the last device does not open it again, and a key it does not know is refused even
while it is open. Off the loopback
it is never open: with no key and no device it answers only `/health` and the exchange of a
code, so that a first device can still be linked.

### Linking a device

On the machine that runs the server:

```bash
sudo leaf-server device add
```

It makes a code of eight characters, valid **ten minutes** and good **once**, and prints a
link for each address the server listens on, with the QR code of the first (`--qr LABEL`
draws another one):

```
leaf://192.0.2.10:8081/lier/ABCD-EFGH?empreinte=<64 hexadecimal characters>
```

Open the link on the device to link — click it, since the client registers `leaf://`, or
paste it into the client's own linking screen — or scan the QR code. The client takes the
whole link, never the code alone: the link also carries the address and the fingerprint. The device sends the code, gets its key
in return, and keeps it in the keychain; the code is spent. `--address URL` links to another
address this once, and `LEAF_PUBLIC_URL` (in `/etc/leaf/server.env`) is the one `device add`
uses when Leaf sits behind a proxy or a tunnel, where its own addresses are not the ones
devices type. For a server on the loopback, `leaf://` to a loopback address means plain http,
which is how a tunnel is linked.

Once linked, a device on the principal profile can link another: **Bond**, then « Relier un
autre appareil », draws a link and its QR code. The same tab renames the device, moves it to
another server, or unlinks it. Any other profile gets `403` from `POST /link/codes`,
`PATCH /device` and `DELETE /device`.

**The link a device relays is the server's.** `POST /link/codes` answers it whole, built the
way `device add` builds it, from the one address the server knows another device can reach:

1. `LEAF_PUBLIC_URL`, whatever path the asking device takes to the server — a tunnel
   included. The server asks that address once, at start, which certificate it presents,
   and logs the answer: a public authority's pins nothing, Leaf's own is pinned, and
   anything else — or no answer — pins nothing either, so that the device trusts only what
   a public authority signed. A certificate of your own behind a proxy is pinned with
   `device add`, which shows it to you first;
2. otherwise one of the addresses it listens on past the loopback, the home network first
   and never a container's or a virtual machine's bridge, and the panel says the link works
   on the same network as the library;
3. otherwise none. A server on `127.0.0.1` with no public address is reached only from its
   own machine: the tab offers neither « Relier un autre appareil » nor « Délier cet
   appareil » — unlinking there would strand the client with no device able to link it
   back — and says « Cette bibliothèque n'est accessible que depuis cet ordinateur. »

The address the asking device uses is never the one in the link: on the server's own
machine it is the loopback, and through an SSH tunnel it is the tunnel's end, which no other
device can follow. A developer reaching a server through a tunnel, who wants to link a
second device through it, sets `LEAF_PUBLIC_URL` on the server to the address that device
types — `http://127.0.0.1:9099` for a tunnel on each machine — and restarts it, or makes
that one link from the server's terminal:

```bash
sudo leaf-server device add --address http://127.0.0.1:9099
```

```bash
sudo leaf-server device list              # devices, keys of LEAF_KEYS, waiting codes
sudo leaf-server device revoke "Laptop"   # a device by its name, or a waiting code
sudo leaf-server device enroll "Desktop" > key   # for the installer: the key on stdout
```

`revoke` takes effect on the device's next request: it comes back to the linking screen with
its sentence. `enroll` links a device at once and writes its key to its output, which must
not be a terminal — a key is never shown on a screen. These commands read
`/etc/leaf/server.env`, so they are run with `sudo`, and they open the index as the account
that owns its folder.

The client's sentences name no command: when a device forgot a library the server did not
hear about, or came in with a key from `LEAF_KEYS`, it asks the reader to see whoever
installed the library — and that person runs `device revoke` above, or removes the key from
`/etc/leaf/server.env` and restarts the service.

### https on the network

Anywhere but `127.0.0.1`, `localhost` and `::1`, Leaf speaks https and nothing else, with no
setting to turn it off: the key travels on every request, and on a home network it is
anyone's who listens. The first start there makes a self-signed certificate under `tls/`
beside `LEAF_DB` (`leaf.crt` and `leaf.key`, the key closed to everybody but its owner), and
`device add` writes its **fingerprint** — the SHA-256 of the certificate, 64 lowercase
hexadecimal characters — into every link.

The client pins that fingerprint when it links and compares it on every connection. There
is no « continue anyway » button on purpose: a certificate that changed is either a renewal
the device was not told about or someone standing in between, and the screen cannot tell
which, so it stops and says so. Link the device again from a fresh code to accept the new
certificate. Copying `tls/` to another machine keeps the fingerprint, and so keeps the links.

`LEAF_TLS_CERT` and `LEAF_TLS_KEY` name a certificate of your own instead.

### A proxy on another machine

A proxy on the same machine talks to Leaf over the loopback, in plain http, and needs nothing.
A proxy on **another** machine reaches Leaf off the loopback, so it speaks https to it and
must trust Leaf's certificate, which no public authority signed — and, since it checks the
name, a certificate that carries the address it dials. The generated certificate carries
`LEAF_HOST` (`0.0.0.0` is no address anyone dials), so set `LEAF_TLS_HOSTS` to the address or
name of the proxy's upstream **before the first start**; the certificate is made once, and
changing it later means removing `tls/` and linking the devices again. With Traefik, name
`leaf.crt` as a root of the transport the router uses:

```yaml
http:
  serversTransports:
    leaf:
      rootCAs:
        - /etc/traefik/leaf.crt        # a copy of tls/leaf.crt
      serverName: 192.0.2.10           # a name or address the certificate carries (LEAF_TLS_HOSTS)
  services:
    leaf:
      loadBalancer:
        serversTransport: leaf
        servers:
          - url: https://192.0.2.10:8081
```

Then set `LEAF_PUBLIC_URL` to the proxy's public address, so that the links carry it. When
that address answers with a certificate publicly signed, the link carries no fingerprint and
the client trusts the authorities it already knows; when it does not, `device add` shows the
fingerprint it found and asks you to check it is your proxy's.

**TLS, if nothing else provides it.** A reverse proxy with a certificate browsers already
trust is the better answer for a public hostname. Leaf's own certificate is for the
addresses of a private network, where the clients are yours and pin exactly one server. Two
PEM files and no keystore: the key is guarded by its mode — checked on every start, and
closed to everybody but its owner when it is not — rather than by a password to configure,
lose, or leave in an environment variable.

**Uploads have a ceiling**, malformed input answers `400` rather than `500`, and an
unforeseen failure answers `internal error` with the detail logged server-side — an exception
message can carry a path, a query, a piece of the schema.

## The API

The contract lives in [contract/openapi.yaml](contract/openapi.yaml): every route, every
shape, and what each one means. The summary below is the shape of it.

```
GET    /health                        answers without a key, and says what it is
POST   /link                          answers without a key: a code becomes a device's key
POST   /link/codes  ·  GET/DELETE /link/codes/{id}   make, follow or withdraw a code
PATCH  /device  ·  DELETE /device     the calling device renames itself, or leaves
GET    /profiles  ·  POST             who reads, and where this device sits; add a profile
PUT    /profiles/active               read as another profile on this device
PUT    /profiles/code  ·  DELETE      make, change or remove the one code
POST   /profiles/code/recovery      draw the recovery code, shown once (principal)
POST   /profiles/code/recover       a new code from the recovery code, from any device
PATCH  /profiles/{id}  ·  DELETE      change a profile, or delete it and its progress
GET    /profile/reading  ·  PUT       this profile's reading preferences, kept for the client
GET    /devices                       every linked device and the profile it sits on
PATCH  /devices/{id}  ·  DELETE       seat a device on a profile, or remove it
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
PUT    /series/{id}/reading-direction this profile's choice of direction, null for the default
GET    /next                          what you are reading, then what follows it
GET    /search?q=&kind=&limit=        ranked, accents folded, half-typed words match
GET    /scan                          where a running scan has got to
PATCH  /series/{id}  ·  /series/{id}/arcs  ·  /entries/{id}
POST   /entries                       drop a file, the server proposes a destination
POST   /preflight                     hold a place and hear the proposal, before any byte
GET    /intake/{id}                   how much of it the server holds, to resume against
PUT    /intake/{id}/file              the bytes, from an offset — 409 says the right one
POST   /intake/{id}/file              you confirm, only then is it filed
POST   /import  ·  /import/{id}/commit   a whole folder, resumable
POST   /works/{id}/move               a series into a universe, or back out to the root
POST   /cleanup  ·  /scan
```

`/health` answers without a key — and so does `POST /link`, which is where a device without
one gets its own — and says what it is. `linked` is whether the library is closed to anyone
without a key:

```json
{ "status": "ok", "api": 1, "format": 1, "library": 12, "linked": true }
```

To a key it recognises, and to nobody else, it adds `"relay": true | false`: whether another
device can reach the server, which decides what the client's **Bond** tab offers. A key
tried there is counted against the throttle like anywhere else.

`api` is what a client checks on connecting, so a client carrying last month's build says
"update the application" instead of failing obscurely three screens later. There is no `/v1`
prefix on purpose. `format` is the version of the files on disk; migrations of the index are
tracked by SQLite's own `PRAGMA user_version`, which holds the number of the last migration
applied. Each runs once, in order, in one transaction, a failure stopping the start rather
than leaving a half-migrated schema behind without a word. A server older than its index
refuses to open it instead of running on a schema it does not know.

That refusal only works from here on. A build from before this numbering reads the number
as a count of entries, so started on an index that has moved to the new scheme it replays
migrations it has already run, among them one that resets every volume read several times
to once. The way back from a newer server is the backup taken before updating, never an
older binary on the same file.

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

### Profiles

A profile is « who reads », not an account: no identifier, no password per person, no sign-in
screen. Each device sits on one, which the server keeps, and each profile has its own reading
progress and its own reading preferences. There are three kinds:

| | Principal | Reader | Restricted |
|---|---|---|---|
| How many | exactly one, `default` | any | any |
| Series seen | all | all | rated up to its age |
| Import, edit, delete | yes | the rights the principal ticked, none at first | no |
| Whole file of a volume (`/entries/{id}/file`) | yes | yes | no, pages one by one only |
| Switch to a reader or restricted profile | free | free | the code |
| Switch to the principal | free | the code, if it guards everything | the code |
| Add a reader | yes | yes, with the code if it guards everything; no rights | no |
| Add a restricted profile, restrict a reader, fix an age | yes | no | no |
| Rename itself, change its colour | yes | yes | no, the principal does it |
| Change or delete another profile, tick rights, make or change the code | yes | no | no |
| Link a device, see and seat the devices | yes | no | no |

The principal is the first profile, the one that existed before profiles did: the reading
progress already there stays with it. A scan, and the library's address, are the principal's
alone, whatever rights a reader has been given. The server enforces all of it; a client only
declines to show what it would refuse.

**There is one code in the whole of Leaf, the principal's.** Four to eight characters, any
kind, typed twice, kept as a slow digest. It is optional until a profile is restricted, and
cannot be removed while one is (`409`). Changing it, and removing it, ask the code it is now
(`currentCode`): the principal's profile proves nothing about knowing it, since a device that
keeps the principal opens on it with no code. The first one asks nothing. `PUT /profiles/code`
also says what it is for:
`restricted`, to leave a restricted profile, or `everything`, which also guards the way back
to the principal and the addition of a profile by a reader. A wrong code counts against a
brake of its own, per device and apart from the one on keys: ten in five minutes, and every
route that takes the code answers `429` for fifteen. A device that keeps the principal opens
on it without the code; that is the principal's choice. One merely left on the principal,
without « Retenir », asks the code to stay there like any other device.

A forgotten code is recovered with the **recovery code**, which needs no terminal and so
works whatever machine the server runs on. When the principal makes the code, the client asks
the server for one (`POST /profiles/code/recovery`): sixteen characters in four groups, shown
**once**, to be written down and kept apart from the device, of which the server keeps only a
slow digest. On the screen that asks the code, « Code oublié ? » takes it and a new code typed
twice (`POST /profiles/code/recover`), from any device, even one that cannot read yet. The
recovery code is spent by it and replaced by the next, shown once in its turn; drawing another
revokes the one before, which is what mends one that leaked, and asks the current code, like changing it: a recovery code is a code replaced without the old one. A wrong one counts on the brake of
the code, and a household that never drew one answers as for a wrong one.

If the recovery code is lost too, on the server's terminal:

```bash
sudo leaf-server profile reset-code    # asks twice, shows nothing, keeps what the code is for
```

It replaces the code rather than erasing it, because erasing it would let every restricted
profile out, and it revokes the recovery code, which a reset must not leave standing (draw a new
one from the settings). A mismatch or a length outside 4 to 8 changes nothing and exits non-zero.

## Behaviour worth knowing before it surprises you

**A series above a restricted profile's age is a `404`, not a `403`.** « Forbidden » would say
that it exists. The same goes for its cover, its pages, its entries, its progress and its
search hits: they are not there. The age is read from `ageRating` at the scan, as its first
number (« Mature 17+ » is 17) or one of the usual words (`Teen`, `T`, `Tout public`), and a
series with no rating, or one that is not recognised, is hidden rather than guessed. A
restricted profile's age changes what it sees from its next request.

**A device that must choose a profile gets `409` with `choose a profile`, on every route.**
That is what a device sees when the profile it sat on was deleted. Only `/health`,
`GET /profiles` and `PUT /profiles/active` answer it. Reading as the principal in the
meantime would open the whole library on a child's tablet whose profile was just deleted. If
the deleted profile was a restricted one, the device stays held on it: leaving that seat asks
the code.

**`GET /entries/{id}/file` is refused to a restricted profile** (`403`), and its pages still
served one by one: a file that leaves Leaf escapes the age and the code.

**`?width=` is the width you want per page.** A landscape image is a double-page spread and
gets twice the requested width. Without the parameter the original bytes are served
untouched, and nothing is ever upscaled or returned larger than its source.
The width is rounded **up to a step of 200** first — 1001 and 1200 are one image under one
ETag — because every distinct width is a decode and a cache entry, and any key that can read
could otherwise ask a page at all 4096 of them.

**`GET /entries/{id}/file` writes before it serves.** It stamps the archive with its own
identity — work, edition, number — so the file can find its way home when it comes back. That
means a GET modifies something on disk, which no cache, proxy or client expects. Deliberate,
and the stamp has to be in the copy that leaves rather than the one that stays; worth knowing
before putting a caching proxy in front.

**Resized pages are lossy WebP**, quality 0.75, method 4. Measured on 177 pages of a real
library against the JPEG at 0.85 it replaced: the same PSNR to a tenth of a decibel and a
higher SSIM, for 0.56 of the bytes in the median. A resize that came out larger than its
WebP source — 32 % of them at 1000 wide, 54 % at 1200, as JPEG — now comes out larger for
none. The price is the encode, about ten times the JPEG's (84 ms against 8 at 1000 wide),
paid once per page and width and then served from the cache. A page too tall for a WebP at its width — a webtoon strip past 16 383 rows — is a JPEG instead, at the quality ten points up, which a WebP at 0.75 matches. Building the server needs a C
compiler for libwebp, which is linked in: nothing to install beside the binary.

**What a page becomes when a width is asked.** The resize path has one output format, so:

| source | asked at a width |
|---|---|
| JPEG, lossy WebP | decoded, shrunk, re-encoded as WebP — or **untouched** when the width is within a fifth of its own, or the result would be larger |
| PNG, BMP, GIF — and a lossless WebP cover file, whose bytes say so; inside an archive the index cannot tell it from a lossy one | **always** re-encoded as WebP, at its own width when the width asked is near or past it: a PNG scan is several megabytes where the WebP is a tenth — unless the result would be larger |
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
sudo apt-get install cmake ninja-build qt6-base-dev qt6-declarative-dev libqt6svg6-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-dialogs \
  qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtqml \
  qml6-module-qtqml-workerscript qml6-module-qt5compat-graphicaleffects \
  qt6-image-formats-plugins xauth xvfb qtkeychain-qt6-dev
cd desktop
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure                    # in a desktop session
xvfb-run -a ctest --test-dir build --output-on-failure        # over SSH / headless
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
Open Font Licence, included beside them. The `tabler-` icons under `desktop/qml/assets/icons/`
are Tabler Icons, under the MIT licence included beside them as `MIT-Tabler.txt`.
