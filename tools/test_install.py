"""The installer, tested by sourcing it: every decision it takes is a function, and nothing
runs as root here — `as_root` only prints what it would have run.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import glob
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def run(snippet, env=None, stdin=subprocess.DEVNULL):
    """Sources install.sh without running it, then runs `snippet` in the same shell."""
    script = (
        "LEAF_INSTALL_SOURCE_ONLY=1; source install.sh; "
        'as_root() { printf "%s\\n" "$*"; }; ' + snippet
    )
    return subprocess.run(
        ["bash", "-c", script],
        cwd=ROOT,
        env=env,
        stdin=stdin,
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


if __name__ == "__main__":
    unittest.main()
