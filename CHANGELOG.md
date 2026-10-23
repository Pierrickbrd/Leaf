# Changelog

This file records the visible changes made to Leaf. Until the first stable release, the
current version may continue to receive compatible fixes.

## 0.1.0 — Unreleased

- Add separate one-command installers for the server and Ubuntu client.
- Add an `all` installation that connects a local client to a local server.
- End the server installation with a first `leaf://` link and its QR code, made by
  `leaf-server device add`, instead of a client key to copy. The installer asks for the
  public address and writes it as `LEAF_PUBLIC_URL`; an update adds that line only when it
  is missing and a terminal can ask. No key is written into `LEAF_KEYS` any more, and the
  keys already there keep working.
- `install.sh client` asks for nothing: the client links itself from its own screen and
  opens `leaf://` links. `--address` and `--key-file` remain, optional. `all` links the
  client of the same machine by itself.
- Leaf speaks https everywhere but the loopback. The clients of an installation that
  listened over plain http on the network must be linked again, to learn the fingerprint
  of its certificate.
- Link a device with a one-time code: `leaf-server device add` prints a `leaf://` link per
  address and the QR code of one; the client links from its own screen, with the link or the
  code, and keeps its key in the keychain. `device list`, `device revoke` and `device enroll`
  complete it. The library closes as soon as a device is linked.
- The client's Bond tab renames the device, moves it to another server or unlinks it, and
  « Relier un autre appareil » makes a code and its QR code for the next one.
- The database now also holds the reading progress and the linked devices: back it up with
  the library, a rescan cannot rebuild them.
- Document linking, https off the loopback, and the Traefik transport a proxy on another
  machine needs to trust Leaf's certificate.
- Install the Leaf icon and application launcher with the Ubuntu client.
- Expose the application version from both binaries and record installed component versions.
- Add `leaf-install update server|client|all` for component-specific updates.
- Run a new server installation as a dedicated `leaf` system account; updates keep the
  account already in use, and `--user` still names another one.
- Add `--share-with USER` to give a person read and write access to the library files
  through the `leaf` group and default ACLs; `all` shares with the person installing.
- Ask where the library goes when no `--library` is given at a terminal, and refuse paths
  under `/home`, `/root` and `/run/user`, which the service cannot see.
- Add `--binary PATH` to install a server built elsewhere, with no toolchain on the machine.
- Harden the generated unit: `ProtectHome=yes`, `ProtectClock`, `ProtectHostname`, a
  shared `UMask=0002` for the library, and state and cache directories closed to the group
  (`0700`).
- Refuse a library whose parent folders another account than root could write.
