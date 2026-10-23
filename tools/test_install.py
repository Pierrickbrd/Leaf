"""The installer, tested by sourcing it: every decision it takes is a function, and nothing
runs as root here — `as_root` only prints what it would have run.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import glob
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def run(snippet, env=None, stdin=subprocess.DEVNULL, input=None):
    """Sources install.sh without running it, then runs `snippet` in the same shell.

    `input` is what a person would type: it replaces the closed stdin, and the snippet
    redefines `at_a_terminal` for the installer to ask at all."""
    script = (
        "LEAF_INSTALL_SOURCE_ONLY=1; source install.sh; "
        'as_root() { printf "%s\\n" "$*"; }; ' + snippet
    )
    return subprocess.run(
        ["bash", "-c", script],
        cwd=ROOT,
        env=env,
        stdin=stdin if input is None else None,
        input=input,
        capture_output=True,
        text=True,
        check=False,
    )


class SourcingTheInstaller(unittest.TestCase):
    def test_runs_nothing_and_creates_nothing(self):
        before = set(glob.glob("/tmp/leaf-install.*"))
        # Sourced without the guard, the script reached its usage text and exited 0 before
        # this snippet ran, which looked like success. The functions must be listed.
        result = run("declare -F install_server main")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.split(), ["install_server", "main"])
        self.assertEqual(set(glob.glob("/tmp/leaf-install.*")), before)

    def test_a_script_without_a_file_trusts_no_directory(self):
        # `bash -c "$(curl …)"` gives the script no file of its own. Taking the current
        # directory for the sources would build, as root, whatever sits there under the
        # names VERSION, server and desktop.
        script = (
            "LEAF_INSTALL_SOURCE_ONLY=1\n"
            + (ROOT / "install.sh").read_text()
            + '\nprintf "[%s]" "$LEAF_ROOT"'
        )
        with tempfile.TemporaryDirectory() as folder:
            pathlib.Path(folder, "VERSION").write_text("6.6.6\n")
            pathlib.Path(folder, "server").mkdir()
            pathlib.Path(folder, "desktop").mkdir()
            result = subprocess.run(
                ["bash", "-c", script],
                cwd=folder,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "[]")

    def test_an_empty_root_is_not_a_checkout(self):
        # With no root, the checks would otherwise look for /VERSION, /server and /desktop.
        result = run('is_checkout "" && echo checkout || echo download')
        self.assertEqual(result.stdout.strip(), "download")

    def test_this_repository_is_a_checkout(self):
        result = run('is_checkout "$LEAF_ROOT" && echo checkout || echo download')
        self.assertEqual(result.stdout.strip(), "checkout")

    def test_an_unknown_command_is_refused(self):
        result = subprocess.run(
            ["bash", "install.sh", "nonsense"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("commande inconnue", result.stderr)


class RendersTheUnit(unittest.TestCase):
    def unit(self):
        result = run("render_server_unit leaf leaf /srv/leaf")
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.splitlines()

    def test_runs_as_the_service_account_with_a_shared_umask(self):
        unit = self.unit()
        for line in ("User=leaf", "Group=leaf", "UMask=0002"):
            self.assertIn(line, unit)

    def test_the_group_gets_nothing_outside_the_library(self):
        # With UMask=0002 every file there is group-writable, and writing an existing file
        # needs no write right on its folder: 0750 would let the person shared with edit
        # the database, and the device keys it will hold, or the cached pages.
        unit = self.unit()
        for line in ("StateDirectoryMode=0700", "CacheDirectoryMode=0700"):
            self.assertIn(line, unit)

    def test_hides_home_and_guards_the_mount(self):
        unit = self.unit()
        for line in (
            "ProtectHome=yes",
            "ProtectClock=yes",
            "ProtectHostname=yes",
            "RequiresMountsFor=/srv/leaf",
            "ReadWritePaths=/srv/leaf",
            "EnvironmentFile=/etc/leaf/server.env",
            "StateDirectory=leaf",
            "CacheDirectory=leaf",
        ):
            self.assertIn(line, unit)
        self.assertNotIn("ProtectHome=read-only", unit)

    @unittest.skipUnless(shutil.which("systemd-analyze"), "systemd-analyze is not installed")
    def test_is_accepted_by_systemd(self):
        # The binary is not installed here, and verify refuses an ExecStart it cannot run.
        unit = "\n".join(
            "ExecStart=/bin/true" if line.startswith("ExecStart=") else line
            for line in self.unit()
        )
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder, "leaf-server.service")
            path.write_text(unit + "\n")
            result = subprocess.run(
                ["systemd-analyze", "verify", str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("leaf-server.service:", result.stderr)


class ChoosesTheLibrary(unittest.TestCase):
    def check(self, path):
        return run(f"check_library_path '{path}'")

    def test_a_relative_path_is_refused(self):
        result = self.check("library")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("chemin absolu", result.stderr)

    def test_home_root_and_user_runtime_are_refused(self):
        # ProtectHome=yes hides all three from the service: a library there would be an
        # empty folder to it, and an empty scan prunes the progress.
        for path in ("/home/a/lib", "/root/lib", "/run/user/1000/lib", "/home"):
            with self.subTest(path=path):
                result = self.check(path)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ProtectHome", result.stderr)

    def test_spaces_are_refused(self):
        result = self.check("/srv/my lib")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("espace", result.stderr)

    def test_a_trailing_slash_is_dropped(self):
        # Kept, it would leave inbox and drop beside the library rather than inside its
        # parent, and the base the unit guards would be the library itself.
        result = self.check("/srv/x/library/")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "/srv/x/library\n")

    def test_without_a_terminal_the_default_is_taken(self):
        result = run('existing_server_value() { :; }; choose_library; printf "%s" "$library"')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "/srv/leaf/library")

    def test_the_installed_library_is_kept_on_an_update(self):
        result = run(
            'existing_server_value() { [[ $1 == LEAF_LIBRARY ]] && printf /data/lib; }; '
            'choose_library; printf "%s" "$library"'
        )
        self.assertEqual(result.stdout, "/data/lib")


ABSENT = "getent() { return 2; }; "


class RunsAsItsOwnAccount(unittest.TestCase):
    def test_a_fresh_install_runs_as_leaf(self):
        result = run("resolve_service_user /nonexistent.service")
        self.assertEqual(result.stdout, "leaf\n")

    def test_an_update_keeps_the_account_it_found(self):
        # Switching an existing installation to `leaf` would leave it in front of a library
        # it cannot write to.
        with tempfile.TemporaryDirectory() as folder:
            unit = pathlib.Path(folder, "leaf-server.service")
            unit.write_text("[Service]\nUser=alice\nGroup=alice\n")
            result = run(f"resolve_service_user '{unit}'")
        self.assertEqual(result.stdout, "alice\n")

    def test_the_option_wins_over_the_installed_unit(self):
        with tempfile.TemporaryDirectory() as folder:
            unit = pathlib.Path(folder, "leaf-server.service")
            unit.write_text("[Service]\nUser=alice\n")
            result = run(f"service_user=bob; resolve_service_user '{unit}'")
        self.assertEqual(result.stdout, "bob\n")

    def test_creates_a_system_account_without_home_or_shell(self):
        result = run(ABSENT + "ensure_service_account leaf")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "useradd --system --user-group --no-create-home --home-dir /nonexistent "
            "--shell /usr/sbin/nologin leaf\n",
        )

    def test_an_unknown_named_account_is_refused(self):
        result = run(ABSENT + "ensure_service_account nobody-here")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("n’existe pas", result.stderr)
        self.assertNotIn("useradd", result.stdout)

    def test_sharing_gives_group_and_default_acls(self):
        result = run("share_library_with alice leaf /x/library")
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "usermod -aG leaf alice",
                "setfacl -m g:leaf:rwX,u:alice:rwX /x/library",
                "setfacl -d -m g:leaf:rwX,u:alice:rwX /x/library",
            ],
        )

    def test_missing_folders_are_created_shared_and_setgid(self):
        with tempfile.TemporaryDirectory() as folder:
            result = run(f"prepare_library_dirs leaf leaf '{folder}/lib' '{folder}'")
        self.assertEqual(
            result.stdout.splitlines(),
            [
                f"install -d -m 2775 -o leaf -g leaf {folder}/lib",
                f"install -d -m 2775 -o leaf -g leaf {folder}/inbox",
                f"install -d -m 2775 -o leaf -g leaf {folder}/drop",
            ],
        )

    def test_an_existing_library_is_never_reowned(self):
        # A public installer handed a path has no business running chown -R on it.
        with tempfile.TemporaryDirectory() as folder:
            library = pathlib.Path(folder, "lib")
            library.mkdir()
            pathlib.Path(library, "Tome 1.cbz").write_text("")
            result = run(f"prepare_library_dirs leaf leaf '{library}' '{folder}'")
        executed = [l for l in result.stdout.splitlines() if l.startswith(("install ", "chown "))]
        self.assertNotIn(f"install -d -m 2775 -o leaf -g leaf {library}", executed)
        self.assertFalse([l for l in executed if l.startswith("chown ")])
        self.assertIn(f"sudo chown -R leaf:leaf {library}", result.stdout + result.stderr)

    def test_a_second_run_changes_nothing(self):
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        with tempfile.TemporaryDirectory() as folder:
            for name in ("lib", "inbox", "drop"):
                pathlib.Path(folder, name).mkdir()
            result = run(f"prepare_library_dirs {user} {user} '{folder}/lib' '{folder}'")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout + result.stderr, "")

    def test_all_shares_with_the_person_installing(self):
        result = run(
            'parse_arguments all; printf "%s" "$share_with"',
            env={"PATH": "/usr/bin:/bin", "SUDO_USER": "alice"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "alice")

    def test_all_refuses_to_share_with_root(self):
        # Root's client configuration would go to /root, and sharing a library with root
        # grants nothing root does not already have.
        result = run(
            "parse_arguments all",
            env={"PATH": "/usr/bin:/bin", "LEAF_INSTALL_USER": "root"},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LEAF_INSTALL_USER", result.stderr)

    def test_sharing_without_setfacl_says_which_package(self):
        result = run(
            "with_dependencies=0; "
            'command() { [[ ${2:-} == setfacl ]] && return 1; builtin command "$@"; }; '
            "need_command setfacl acl"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("paquet acl", result.stderr)

    def test_client_configuration_goes_to_the_person_not_the_service(self):
        result = run(
            "human_home=/home/alice; service_user=leaf; "
            'as_root() { printf "%s\\n" "$*"; return 1; }; read_client_configuration'
        )
        self.assertIn("test -f /home/alice/.config/Leaf/leaf.conf", result.stdout)


class InstallsAGivenBinary(unittest.TestCase):
    def fake(self, folder, output, executable=True):
        path = pathlib.Path(folder, "leaf-server")
        path.write_text(f"#!/bin/sh\necho '{output}'\n")
        if executable:
            path.chmod(0o755)
        return path

    def test_reads_the_version_the_binary_reports(self):
        with tempfile.TemporaryDirectory() as folder:
            result = run(f"check_server_binary '{self.fake(folder, 'leaf-server 9.9.9')}'")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "9.9.9\n")

    def test_something_else_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            result = run(f"check_server_binary '{self.fake(folder, 'other 1.0')}'")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("n’est pas un binaire leaf-server", result.stderr)

    def test_a_file_that_cannot_run_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = self.fake(folder, "leaf-server 9.9.9", executable=False)
            result = run(f"check_server_binary '{path}'")
        self.assertNotEqual(result.returncode, 0)

    def test_a_given_binary_needs_no_toolchain(self):
        # The point of --binary: a server that never receives Rust nor build-essential.
        with tempfile.TemporaryDirectory() as folder:
            path = self.fake(folder, "leaf-server 9.9.9")
            result = run(
                f"binary='{path}'; "
                "ensure_rust() { echo TOOLCHAIN; }; cargo() { echo TOOLCHAIN; }; "
                'obtain_server_binary; printf "%s" "$server_binary"'
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("TOOLCHAIN", result.stdout)
        self.assertEqual(result.stdout, str(path))

    def test_without_a_binary_the_sources_are_built(self):
        result = run(
            "ensure_rust() { :; }; cargo() { echo CARGO >&2; }; "
            'obtain_server_binary; printf "%s" "$server_binary"'
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CARGO", result.stderr)
        self.assertTrue(result.stdout.endswith("server/target/release/leaf-server"))


class ServerHarness:
    """install_server end to end on a fresh machine, with every system change printed."""

    def install(self, folder, extra="", real_parent_check=False, input=None):
        binary = pathlib.Path(folder, "leaf-server")
        binary.write_text("#!/bin/sh\necho 'leaf-server 9.9.9'\n")
        binary.chmod(0o755)
        return run(
            f"LEAF_TMP='{folder}'; LEAF_VERSION=0.1.0; library='{folder}/srv/library'; "
            f"binary='{binary}'; "
            'as_root() { [[ "$*" == "test -f /etc/leaf/server.env" ]] && return 1; '
            'printf "ROOT %s\\n" "$*"; }; '
            'getent() { [[ $2 == leaf || $2 == nobody-here ]] && return 2; '
            'printf "%s:x:1000:1000::/home/%s:/bin/bash\\n" "$2" "$2"; }; '
            'id() { case $1 in -gn) echo leaf ;; -u) echo 999 ;; *) builtin command id "$@" ;; esac; }; '
            "curl() { :; }; sleep() { :; }; "
            # as_root runs nothing here, so the parent check cannot ask the kernel. It has
            # its own tests, and one below that leaves it in place.
            + ("" if real_parent_check else "check_parent_out_of_reach() { :; }; ")
            + extra
            + "; install_server; echo FINISHED",
            input=input,
        )

    def changes(self, result):
        return [line for line in result.stdout.splitlines() if line.startswith("ROOT ")]


class InstallsTheServer(ServerHarness, unittest.TestCase):

    def test_a_fresh_install_reaches_the_end(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "share_with=alice")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FINISHED", result.stdout)
        self.assertTrue([l for l in self.changes(result) if "useradd" in l])

    def test_a_fresh_install_invents_no_key(self):
        # A device links itself with a code now; a key made up here would sit in clear in
        # server.env and keep the library open to whoever read it.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, ":")
            env = pathlib.Path(folder, "server.env").read_text().splitlines()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("LEAF_KEYS=", env)
        self.assertTrue([l for l in self.changes(result) if l.endswith("/etc/leaf/server.env")])

    def test_a_parent_the_service_can_write_stops_before_any_change(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder,
                "service_user=alice; "
                'as_root() { [[ $1 == runuser ]] && return 0; '
                '[[ "$*" == "test -f /etc/leaf/server.env" ]] && return 1; printf "ROOT %s\\n" "$*"; }',
                real_parent_check=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/tmp", result.stderr)
        self.assertEqual(self.changes(result), [])

    def test_the_parent_is_checked_again_once_the_account_exists(self):
        # Before useradd there is no account to ask the kernel about; a leftover `leaf`
        # group with an ACL on the parent would only show once it exists.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder,
                "created=0; "
                'getent() { [[ $2 == leaf ]] && (( ! created )) && return 2; '
                'printf "%s:x:1000:1000::/home/%s:/bin/bash\\n" "$2" "$2"; }; '
                'stat() { echo "0 755"; }; getfacl() { :; }; '
                'as_root() { case $1 in useradd) created=1 ;; '
                'runuser) [[ ${@: -1} == true ]] && return 0; echo "RUNUSER ${@: -1}"; return 1 ;; esac; '
                '[[ "$*" == "test -f /etc/leaf/server.env" ]] && return 1; printf "ROOT %s\\n" "$*"; }',
                real_parent_check=True,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        created = next(i for i, l in enumerate(lines) if l.startswith("ROOT install -d -m 2775"))
        self.assertIn(f"RUNUSER {folder}/srv", lines[:created])

    def test_an_unknown_person_to_share_with_stops_before_any_change(self):
        # Found after the binary, the account and the folders, it would leave a
        # half-made installation that the next run no longer shares.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "share_with=nobody-here")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.changes(result), [])

    def test_sharing_without_setfacl_stops_before_any_change(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder,
                "share_with=alice; with_dependencies=0; "
                'command() { [[ ${2:-} == setfacl ]] && return 1; builtin command "$@"; }',
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.changes(result), [])

    def test_a_failed_build_installs_nothing(self):
        # Built inside $(…), a failing cargo went unnoticed and an older binary left in
        # target/ was installed under the new version number.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, 'binary=""; ensure_rust() { :; }; cargo() { return 101; }')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("FINISHED", result.stdout)
        self.assertFalse([l for l in self.changes(result) if "/usr/local/bin/leaf-server" in l])

    def test_sharing_through_a_person_account_is_refused(self):
        # An installation from before `leaf` runs as a person; sharing its group would
        # hand that person's home to someone else.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder,
                "share_with=alice; service_user=bob; "
                'id() { case $1 in -gn) echo bob ;; -u) echo 1000 ;; *) builtin command id "$@" ;; esac; }',
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("compte système", result.stderr)
        self.assertEqual(self.changes(result), [])


class SharesWhatTheServiceOwns(unittest.TestCase):
    def test_folders_the_service_already_owns_are_shared_too(self):
        # A first run that stopped after creating them, or --share-with added later: the
        # folders exist, belong to the service, and still need their ACL.
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        with tempfile.TemporaryDirectory() as folder:
            for name in ("lib", "inbox", "drop"):
                pathlib.Path(folder, name).mkdir()
            result = run(
                f"prepare_library_dirs {user} {user} '{folder}/lib' '{folder}'; "
                'printf "%s\\n" "${owned_library_dirs[@]}"'
            )
        self.assertEqual(
            result.stdout.splitlines(),
            [f"{folder}/lib", f"{folder}/inbox", f"{folder}/drop"],
        )


    def test_a_symbolic_link_in_place_of_a_folder_is_refused(self):
        # A link the service account made would point setfacl, run as root, at its target.
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        with tempfile.TemporaryDirectory() as folder:
            pathlib.Path(folder, "lib").mkdir()
            pathlib.Path(folder, "elsewhere").mkdir()
            pathlib.Path(folder, "inbox").symlink_to(pathlib.Path(folder, "elsewhere"))
            result = run(
                f"prepare_library_dirs {user} {user} '{folder}/lib' '{folder}'; "
                'printf "%s\\n" "${owned_library_dirs[@]}"'
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("lien symbolique", result.stderr)
        self.assertNotIn(f"{folder}/inbox", result.stdout)


class KeepsTheParentOutOfTheServiceReach(unittest.TestCase):
    """Root creates and ACLs folders inside the parent. If the service account could write
    there, it could put a link in their place first, and root would act on the target."""

    def check(self, writable):
        # The kernel answers for the account: group, supplementary groups and ACLs alike.
        return run(
            'getent() { return 0; }; '
            f'as_root() {{ [[ $1 == runuser ]] && case "${{@: -1}}" in true|{writable}) return 0 ;; esac; return 1; }}; '
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )

    def test_asks_the_kernel_as_the_service_account(self):
        result = run(
            "getent() { return 0; }; "
            'as_root() { printf "%s\n" "$*" >&2; [[ ${@: -1} == true ]]; }; '
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("runuser -u leaf -- test -w /srv/data/leaf", result.stderr)
        self.assertIn("runuser -u leaf -- test -w /", result.stderr)

    def test_a_runuser_that_does_not_work_refuses_rather_than_passes(self):
        # Its failure would otherwise read as "cannot write", and every check would pass.
        result = run(
            "getent() { return 0; }; as_root() { return 127; }; "
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("runuser", result.stderr)

    def test_a_folder_anyone_can_write_is_refused_even_before_the_account_exists(self):
        # Any local account could put the link there, not only the service.
        result = run(
            "getent() { return 2; }; "
            'stat() { [[ "${@: -1}" == /srv/data ]] && echo "0 1777" || echo "0 755"; }; '
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/srv/data", result.stderr)

    def root_only(self, stat, acl="user::rwx\ngroup::r-x\nother::r-x"):
        # Any account that can write a parent can put a link there while root works in it,
        # not only the service: the person shared with, or whoever owns a mounted disk.
        return run(
            "getent() { return 2; }; "
            f'stat() {{ [[ "${{@: -1}}" == /srv/data ]] && echo "{stat}" || echo "0 755"; }}; '
            f'getfacl() {{ [[ "${{@: -1}}" == /srv/data ]] && printf "{acl}\\n" || printf "user::rwx\\ngroup::r-x\\nother::r-x\\n"; }}; '
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )

    def test_folders_only_root_can_write_pass(self):
        result = self.root_only("0 755")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_folder_owned_by_another_account_is_refused(self):
        result = self.root_only("1000 755")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("chown root:root /srv/data", result.stderr)

    def test_a_folder_its_group_can_write_is_refused(self):
        result = self.root_only("0 775")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/srv/data", result.stderr)

    def test_a_folder_an_acl_lets_another_account_write_is_refused(self):
        result = self.root_only("0 755", "user::rwx\\nuser:alice:rwx\\ngroup::r-x\\nmask::rwx\\nother::r-x")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("setfacl -b /srv/data", result.stderr)

    def test_a_parent_the_service_cannot_write_passes(self):
        result = self.check("/nowhere")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_parent_the_service_can_write_is_refused(self):
        result = self.check("/srv/data/leaf")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("chown root:root /srv/data/leaf", result.stderr)

    def test_an_ancestor_the_service_can_write_is_refused(self):
        result = self.check("/srv/data")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("/srv/data", result.stderr)

    def test_an_account_not_created_yet_can_write_nothing(self):
        result = run(
            "getent() { return 2; }; as_root() { echo CALLED; }; "
            "check_parent_out_of_reach leaf /srv/data/leaf"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("CALLED", result.stdout)


class RefusesLibrariesThatEscape(unittest.TestCase):
    def check(self, path):
        return run(f"check_library_path '{path}'")

    def test_dots_and_double_slashes_do_not_get_past_the_home_refusal(self):
        for path in ("/srv/../home/a/lib", "//home/a/lib"):
            with self.subTest(path=path):
                result = self.check(path)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("ProtectHome", result.stderr)

    def test_a_library_whose_parent_is_the_root_is_refused(self):
        # Its parent becomes ReadWritePaths, which would make the whole disk writable,
        # and inbox and drop would land at the root of the filesystem.
        for path in ("/srv", "/"):
            with self.subTest(path=path):
                result = self.check(path)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("dossier parent", result.stderr)

    def test_the_service_own_directories_are_refused(self):
        for path in ("/var/lib/leaf/library", "/var/cache/leaf/library"):
            with self.subTest(path=path):
                self.assertNotEqual(self.check(path).returncode, 0)


class RendersTheEnvironment(unittest.TestCase):
    def test_quotes_values_and_writes_no_key(self):
        result = run("render_server_env '/srv/a\"b' /srv 127.0.0.1 8081 'https://leaf.example.net'")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        for line in (
            'LEAF_LIBRARY="/srv/a\\"b"',
            'LEAF_INBOX="/srv/inbox"',
            "LEAF_KEYS=",
            'LEAF_PUBLIC_URL="https://leaf.example.net"',
            "LEAF_DB=/var/lib/leaf/leaf.sqlite",
        ):
            self.assertIn(line, lines)

# The server's update path: server.env exists, and every `sed` or `grep` on it finds
# nothing unless the test says otherwise.
UPDATE = (
    'as_root() { case "$*" in "test -f /etc/leaf/server.env") return 0 ;; '
    'sed*|grep*) return 1 ;; esac; printf "ROOT %s\\n" "$*"; }'
)


class AsksForThePublicAddress(ServerHarness, unittest.TestCase):
    def env(self, folder):
        return pathlib.Path(folder, "server.env").read_text().splitlines()

    def test_a_new_server_env_carries_the_public_url_given_at_the_prompt(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "at_a_terminal() { :; }", input="https://leaf.example.net\n")
            env = self.env(folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('LEAF_PUBLIC_URL="https://leaf.example.net"', env)

    def test_an_empty_answer_leaves_the_public_url_empty(self):
        # "Laisser vide si vous ne savez pas encore": the line is there to fill later.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "at_a_terminal() { :; }", input="\n")
            env = self.env(folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('LEAF_PUBLIC_URL=""', env)

    def test_an_address_that_is_not_a_url_stops_before_any_change(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "at_a_terminal() { :; }", input="leaf.example.net\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("https://", result.stderr)
        self.assertEqual(self.changes(result), [])

    def test_an_update_adds_the_line_only_when_it_is_missing(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder, UPDATE + "; at_a_terminal() { :; }", input="https://leaf.example.net\n"
            )
            env = self.env(folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(env[-1], 'LEAF_PUBLIC_URL="https://leaf.example.net"')
        self.assertIn(
            f"ROOT install -m 0600 -o root -g root {folder}/server.env /etc/leaf/server.env",
            self.changes(result),
        )

        present = (
            'as_root() { case "$*" in "test -f /etc/leaf/server.env") return 0 ;; '
            'grep*) return 0 ;; sed*) return 1 ;; esac; printf "ROOT %s\\n" "$*"; }'
        )
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(
                folder, present + "; at_a_terminal() { :; }", input="https://other.example.net\n"
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse([l for l in self.changes(result) if "/etc/leaf/server.env" in l])

    def test_an_update_without_a_terminal_leaves_server_env_untouched(self):
        # Nobody to ask: server.env stays as it is, LEAF_KEYS included.
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, UPDATE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FINISHED", result.stdout)
        self.assertFalse([l for l in self.changes(result) if "/etc/leaf/server.env" in l])

    def test_the_address_option_is_taken_without_a_question(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, "parse_arguments server --address https://leaf.example.net")
            env = self.env(folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('LEAF_PUBLIC_URL="https://leaf.example.net"', env)


DEVICE_ADD = "/usr/local/bin/leaf-server device add"


class ReportsALink(unittest.TestCase):
    def report(self, device_add, component="server"):
        return run(
            f"LEAF_VERSION=0.1.0; component={component}; client_key=SECRET-KEY; "
            "address=http://127.0.0.1:8081; "
            f'as_root() {{ if [[ "$*" == "{DEVICE_ADD}" ]]; then {device_add}; '
            'else printf "ROOT %s\\n" "$*"; fi; }; report_installation'
        )

    def test_a_new_server_ends_with_the_output_of_device_add(self):
        result = self.report('echo "Lien : leaf://code@leaf.example.net"; echo "Valable 10 minutes."')
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = [l for l in result.stdout.splitlines() if l.strip()]
        self.assertEqual(lines[-2:], ["Lien : leaf://code@leaf.example.net", "Valable 10 minutes."])

    def test_on_the_loopback_without_a_public_url_it_prints_the_explanation_not_a_key(self):
        # device add answers 2 there, with no code made: the installation itself succeeded.
        result = self.report("echo \"Leaf n'écoute que sur cette machine\"; return 2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Leaf n'écoute que sur cette machine", result.stdout)
        self.assertNotIn("Clé", result.stdout)

    def test_a_device_add_that_fails_otherwise_says_how_to_retry(self):
        result = self.report("return 1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("leaf-server device add", result.stderr)

    def test_an_unlinked_client_is_told_to_open_the_link_never_to_type_a_code(self):
        # The client reads a whole leaf:// link — address and fingerprint with the code — and
        # has no field for a code alone.
        result = run("LEAF_VERSION=0.1.0; component=client; address=; report_installation")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("collez-le", result.stdout)
        self.assertNotIn("code", result.stdout)

    def test_it_never_prints_a_key(self):
        for component in ("server", "client", "all"):
            with self.subTest(component=component):
                result = self.report("echo Lien", component)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("SECRET-KEY", result.stdout + result.stderr)
                self.assertNotIn("Clé client", result.stdout)


CLIENT = (
    "LEAF_TMP=\"$folder\"; LEAF_VERSION=0.1.0; LEAF_ROOT=/src; human_user=alice; "
    "human_group=alice; human_home=/home/alice; "
    "install_apt() { :; }; cmake() { :; }; xdg-mime() { :; }; "
    "update-desktop-database() { :; }; gtk-update-icon-cache() { :; }; "
)


class LinksTheLocalClient(unittest.TestCase):
    def client(self, folder, snippet, input=None):
        return run(f"folder='{folder}'; " + CLIENT + snippet, input=input)

    def test_all_enrolls_the_local_client_and_writes_leaf_conf_0600(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                "port=8081; uname() { echo machine; }; "
                'as_root() { case "$*" in '
                '"/usr/local/bin/leaf-server device enroll machine") echo LINKED-KEY ;; '
                'test*) return 1 ;; *) printf "ROOT %s\\n" "$*" ;; esac; }; '
                "link_local_client; install_client",
            )
            conf = pathlib.Path(folder, "leaf.conf").read_text().splitlines()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(conf, ["address=http://127.0.0.1:8081", "key=LINKED-KEY"])
        self.assertIn(
            f"ROOT install -m 0600 -o alice -g alice {folder}/leaf.conf /home/alice/.config/Leaf/leaf.conf",
            result.stdout.splitlines(),
        )
        self.assertNotIn("LINKED-KEY", result.stdout + result.stderr)

    def test_all_keeps_the_key_the_local_client_already_holds(self):
        # A second device of the same name would be refused, and the client already reads.
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                "port=8081; uname() { echo machine; }; "
                'as_root() { case "$*" in *enroll*) echo ENROLLED ;; "test -f"*) return 0 ;; '
                '"sed -n s/^key=//p"*) echo OLD-KEY ;; sed*) echo http://127.0.0.1:8081 ;; esac; }; '
                'link_local_client; printf "%s" "$client_key"',
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "OLD-KEY")

    def test_all_keeps_a_client_that_linked_itself_with_its_key_in_the_keychain(self):
        # Unlinked and linked again from the client's own screen: leaf.conf keeps the address,
        # the key went to the keychain. Enrolling again was « nom déjà pris » after the server
        # was updated, or a second device whose key overwrote the file.
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                "port=8081; uname() { echo machine; }; "
                'as_root() { case "$*" in *enroll*) echo ENROLLED ;; "test -f"*) return 0 ;; '
                '"sed -n s/^key=//p"*) ;; sed*) echo https://leaf.example:8443 ;; '
                '*) printf "ROOT %s\\n" "$*" ;; esac; }; '
                'link_local_client; install_client; '
                'printf "[%s|%s|%s]" "$address" "$client_key" "$enrolled_name"',
            )
            written = pathlib.Path(folder, "leaf.conf").exists()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("ENROLLED", result.stdout)
        self.assertTrue(result.stdout.endswith("[https://leaf.example:8443||]"), result.stdout)
        self.assertFalse(written, "leaf.conf is left as it is")
        self.assertNotIn(".config/Leaf/leaf.conf", result.stdout)

    def test_a_refused_enrollment_stops_the_installation(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                "port=8081; uname() { echo machine; }; "
                'as_root() { case "$*" in *enroll*) return 1 ;; test*) return 1 ;; esac; }; '
                "link_local_client; echo FINISHED",
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("FINISHED", result.stdout)
        self.assertIn("device enroll", result.stderr)

    def test_a_client_that_fails_to_install_leaves_no_enrolled_device(self):
        # The client build failed on a missing zlib after the device was enrolled: its key
        # was lost with the process, and the rerun stopped on « nom déjà pris ».
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        result = run(
            'install_server() { port=8081; }; install_client() { return 1; }; '
            "uname() { echo machine; }; "
            'as_root() { case "$*" in *enroll*) echo KEY ;; test*) return 1 ;; '
            '*) echo "AS_ROOT $*" ;; esac; }; main all',
            env={"PATH": "/usr/bin:/bin", "LEAF_INSTALL_USER": user},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AS_ROOT /usr/local/bin/leaf-server device revoke machine", result.stdout.splitlines())

    def test_a_successful_all_revokes_nothing(self):
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        result = run(
            'install_server() { port=8081; }; install_client() { :; }; report_installation() { :; }; '
            "uname() { echo machine; }; "
            'as_root() { case "$*" in *enroll*) echo KEY ;; test*) return 1 ;; '
            '*) echo "AS_ROOT $*" ;; esac; }; main all',
            env={"PATH": "/usr/bin:/bin", "LEAF_INSTALL_USER": user},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("revoke", result.stdout)

    def test_the_enrolled_device_is_kept_once_its_key_is_in_leaf_conf(self):
        # Whatever fails after that (the report, the launcher database) must not revoke a
        # device whose key the client holds.
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                'as_root() { :; }; address=http://127.0.0.1:8081; client_key=K; enrolled_name=machine; '
                'install_client; printf "[%s]" "$enrolled_name"',
            )
        self.assertEqual(result.stdout, "[]")

    def test_the_client_installs_the_zlib_headers_it_builds_against(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                'install_apt() { echo "APT $*"; }; as_root() { :; }; address=; client_key=; '
                "install_client",
            )
        self.assertIn("zlib1g-dev", result.stdout.split())

    def test_all_links_after_the_server_has_started(self):
        # Before the first start /var/lib/leaf does not exist, and device enroll refuses.
        result = run(
            'install_server() { echo SERVER; port=8081; }; install_client() { echo CLIENT; }; '
            "report_installation() { :; }; uname() { echo machine; }; "
            'as_root() { case "$*" in *enroll*) echo ENROLL >&2; echo KEY ;; test*) return 1 ;; esac; }; '
            "main all 2>&1",
            env={"PATH": "/usr/bin:/bin", "LEAF_INSTALL_USER": subprocess.run(
                ["id", "-un"], capture_output=True, text=True).stdout.strip()},
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout.split(), ["SERVER", "ENROLL", "CLIENT"])

    def main_all(self, arguments, as_root='as_root() { echo "AS_ROOT $*"; [[ $1 != test ]]; }; '):
        user = subprocess.run(["id", "-un"], capture_output=True, text=True).stdout.strip()
        return run(
            'install_server() { echo SERVER; port=8081; }; '
            'install_client() { echo "CLIENT $address"; }; '
            "report_installation() { :; }; uname() { echo machine; }; "
            + as_root
            + f"main all {arguments}",
            env={"PATH": "/usr/bin:/bin", "LEAF_INSTALL_USER": user},
        )

    def test_all_refuses_a_host_off_the_loopback_before_touching_anything(self):
        # The local client would get http://127.0.0.1 and a key for a server that speaks
        # only https there, or does not listen on 127.0.0.1 at all.
        for host in ("0.0.0.0", "100.64.0.10", "::"):
            with self.subTest(host=host):
                result = self.main_all(f"--host {host}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("lancez install.sh server", result.stderr)
                self.assertNotIn("AS_ROOT", result.stdout)
                self.assertNotIn("SERVER", result.stdout)

    def test_an_update_of_all_refuses_a_server_env_off_the_loopback(self):
        # server.env is never rewritten: the host it names is the one the server keeps.
        result = self.main_all(
            "",
            'as_root() { case "$*" in "test -f /etc/leaf/server.env") return 0 ;; '
            '"sed -n s/^LEAF_HOST=//p"*) echo 0.0.0.0 ;; *) echo "AS_ROOT $*" ;; esac; }; ',
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("lancez install.sh server", result.stderr)
        self.assertNotIn("SERVER", result.stdout)

    def test_all_on_a_loopback_spelling_reaches_the_server_there(self):
        for arguments, address in (
            ("", "http://127.0.0.1:8081"),
            ("--host localhost", "http://localhost:8081"),
            ("--host ::1", "http://[::1]:8081"),
        ):
            with self.subTest(arguments=arguments):
                result = self.main_all(
                    arguments, 'as_root() { case "$*" in *enroll*) echo KEY ;; test*) return 1 ;; esac; }; '
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"CLIENT {address}", result.stdout.splitlines())

    def test_client_alone_asks_for_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                "at_a_terminal() { :; }; "
                'as_root() { [[ $1 == test ]] && return 1; printf "ROOT %s\\n" "$*"; }; '
                "choose_client_configuration; install_client; echo FINISHED",
                input="",
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FINISHED", result.stdout)
        self.assertNotIn("Adresse", result.stderr)
        self.assertNotIn("Clé", result.stderr)
        self.assertFalse([l for l in result.stdout.splitlines() if "leaf.conf" in l])

    def test_client_still_takes_an_address_and_a_key_file(self):
        with tempfile.TemporaryDirectory() as folder:
            key = pathlib.Path(folder, "key")
            key.write_text("GIVEN-KEY\n")
            result = self.client(
                folder,
                f"address=https://leaf.example.net; key_file='{key}'; "
                'as_root() { [[ $1 == test ]] && return 1; printf "ROOT %s\\n" "$*"; }; '
                "choose_client_configuration; install_client",
            )
            conf = pathlib.Path(folder, "leaf.conf").read_text().splitlines()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(conf, ["address=https://leaf.example.net", "key=GIVEN-KEY"])

    def test_an_address_without_a_key_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                'address=https://leaf.example.net; as_root() { [[ $1 == test ]] && return 1; }; '
                "choose_client_configuration",
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--key-file", result.stderr)

    def test_the_launcher_declares_leaf_links(self):
        launcher = (ROOT / "packaging" / "leaf.desktop").read_text().splitlines()
        self.assertIn("Exec=leaf-desktop %u", launcher)
        self.assertIn("MimeType=x-scheme-handler/leaf;", launcher)
        # Under the person, with their home: run as root, xdg-mime would set root's default.
        with tempfile.TemporaryDirectory() as folder:
            result = self.client(
                folder,
                'as_root() { [[ $1 == test ]] && return 1; printf "ROOT %s\\n" "$*"; }; '
                "install_client",
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "ROOT runuser -u alice -- env HOME=/home/alice xdg-mime default leaf.desktop "
            "x-scheme-handler/leaf",
            result.stdout.splitlines(),
        )


class ChecksHealthWhereItListens(ServerHarness, unittest.TestCase):
    def asked(self, host):
        with tempfile.TemporaryDirectory() as folder:
            result = self.install(folder, f"host={host}; curl() {{ echo \"CURL $*\" >&2; }}")
        self.assertEqual(result.returncode, 0, result.stderr)
        return [l for l in result.stderr.splitlines() if l.startswith("CURL")][-1]

    def test_health_is_asked_over_https_off_the_loopback(self):
        # The server speaks only https there; --insecure because it is this very machine.
        self.assertEqual(self.asked("100.64.0.10"), "CURL -fsS --insecure https://100.64.0.10:8081/health")
        self.assertEqual(self.asked("0.0.0.0"), "CURL -fsS --insecure https://127.0.0.1:8081/health")

    def test_health_stays_plain_on_the_loopback(self):
        self.assertEqual(self.asked("127.0.0.1"), "CURL -fsS http://127.0.0.1:8081/health")


if __name__ == "__main__":
    unittest.main()
