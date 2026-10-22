# Changelog

This file records the visible changes made to Leaf. Until the first stable release, the
current version may continue to receive compatible fixes.

## 0.1.0 — Unreleased

- Add separate one-command installers for the server and Ubuntu client.
- Add an `all` installation that connects a local client to a local server.
- Generate the first client key during server installation and guide client setup.
  An update whose `server.env` has no key no longer reports a key that exists nowhere:
  it says the library is open.
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
