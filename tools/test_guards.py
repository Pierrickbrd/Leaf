"""The three guards that stand between the client and the contract, and the client and its
own French, tested as functions.

They run under ctest as well, as whole programs. That is where they earn their keep and it
is also why nothing measured them: a subprocess is invisible to coverage, so a guard's lines
read as lines nobody exercises.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import contextlib
import io
import json
import pathlib
import tempfile
import unittest

import bytes_stay_utf8
import client_knows_the_contract as contract
import covers_stay_round
import nothing_talks_to_the_console as console
import one_file_owns_it as owns
import tests_stay_off_the_bus as bus
import words_stay_french


def in_a_file(text: str, suffix: str = ".cpp"):
    """`text` written to a temporary file, and the file's path."""
    handle = tempfile.NamedTemporaryFile("w", suffix=suffix, encoding="utf-8", delete=False)
    handle.write(text)
    handle.close()
    return pathlib.Path(handle.name)


class RefusesLatin1(unittest.TestCase):
    def named(self, line: str) -> list:
        path = in_a_file(line)
        try:
            return [word for _, word, _ in bytes_stay_utf8.asks_for_latin1(path)]
        finally:
            path.unlink()

    def test_every_spelling_of_the_name_is_caught(self):
        for line, word in (
            ('auto a = QLatin1String("é");', "QLatin1String"),
            ('auto b = name.toLatin1();', "toLatin1"),
            ('QStringDecoder(QStringConverter::Latin1);', "Latin1"),
            ('auto c = u"é"_L1;', "_L1"),
            ('QLatin1StringView field;', "QLatin1StringView"),
        ):
            self.assertEqual(self.named(line), [word], line)

    def test_a_parameter_type_is_no_longer_an_excuse(self):
        # The client is UTF-16 throughout, so the rule has no exceptions left to make.
        self.assertEqual(self.named("int read(QLatin1StringView name, int n);"),
                         ["QLatin1StringView"])

    def test_what_is_not_latin1_goes_through(self):
        for line in ('auto a = u"Haikyū"_s;', "int n = _L1x;", "QStringView name;",
                     "// nothing here", 'auto b = name.toUtf8();'):
            self.assertEqual(self.named(line), [], line)

    def test_it_reads_the_client_and_nothing_else(self):
        suffixes = {p.suffix for p in bytes_stay_utf8.files_to_read()}
        self.assertTrue(suffixes)
        self.assertTrue(suffixes <= {".h", ".cpp", ".qml"}, suffixes)

    def test_a_place_that_is_not_there_is_walked_past(self):
        # The client's three folders are named ahead of time; a checkout without one of them
        # is not an error, it is simply nothing to read.
        was = bytes_stay_utf8.LOOKED_AT
        try:
            bytes_stay_utf8.LOOKED_AT = [pathlib.Path("/no/such/folder/anywhere")]
            self.assertEqual(list(bytes_stay_utf8.files_to_read()), [])
        finally:
            bytes_stay_utf8.LOOKED_AT = was

    def test_the_client_as_it_stands_has_none(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            self.assertEqual(bytes_stay_utf8.main(), 0)
        self.assertIn("nowhere", said.getvalue())

    def test_a_client_that_had_some_would_be_refused(self):
        with tempfile.TemporaryDirectory() as root:
            path = pathlib.Path(root) / "Api.cpp"
            path.write_text('QLatin1String("é");', encoding="utf-8")
            was = (bytes_stay_utf8.files_to_read, bytes_stay_utf8.ROOT)
            try:
                bytes_stay_utf8.files_to_read = lambda: [path]
                bytes_stay_utf8.ROOT = pathlib.Path(root)
                with contextlib.redirect_stdout(io.StringIO()) as said:
                    self.assertEqual(bytes_stay_utf8.main(), 1)
                self.assertIn("QLatin1String", said.getvalue())
                self.assertIn("1 place(s) asking for Latin-1", said.getvalue())
            finally:
                bytes_stay_utf8.files_to_read, bytes_stay_utf8.ROOT = was


class KnowsTheContract(unittest.TestCase):
    SOURCE = """
Read<Series> series(const QJsonObject &from)
{
    one.id = field.text(u"id"_s);
    if (broken) { return {}; }
    one.work = field.text(u"work"_s);
}

Read<Page> page(const QJsonObject &from)
{
    some.total = field.whole(u"total"_s);
}
"""

    def test_a_body_is_found_by_counting_its_braces(self):
        body = contract.body_of(self.SOURCE, "series")
        self.assertIn('u"work"_s', body)
        # Not the next function's, which a naive search to the first closing brace would take.
        self.assertNotIn('u"total"_s', body)

    def test_the_field_names_come_out_of_the_body(self):
        found = set(contract.READS.findall(contract.body_of(self.SOURCE, "series")))
        self.assertEqual(found, {"id", "work"})

    def test_a_function_that_is_not_there_stops_the_run(self):
        with self.assertRaises(SystemExit) as refused:
            contract.body_of(self.SOURCE, "facets")
        self.assertIn("this guard needs updating", str(refused.exception))

    def test_unbalanced_braces_stop_the_run(self):
        with self.assertRaises(SystemExit) as refused:
            contract.body_of("void series()\n{\n  int a = 1;\n", "series")
        self.assertIn("unbalanced", str(refused.exception))

    def test_every_watched_schema_names_a_reader(self):
        # A schema whose reader is not in Api.cpp would make the guard raise, not report.
        self.assertTrue(contract.WATCHED)
        for _, (depth, functions) in contract.WATCHED.items():
            self.assertIn(depth, ("whole", "part"))
            self.assertTrue(functions)

    def against(self, source: str, schemas: dict, watched: dict):
        """`main`, reading a contract and a client written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            (folder / "openapi.yaml").write_text(
                json.dumps({"components": {"schemas": schemas}}), encoding="utf-8"
            )
            (folder / "Api.cpp").write_text(source, encoding="utf-8")
            was = (contract.CONTRACT, contract.SOURCE, contract.WATCHED)
            try:
                contract.CONTRACT = folder / "openapi.yaml"
                contract.SOURCE = folder / "Api.cpp"
                contract.WATCHED = watched
                said = io.StringIO()
                with contextlib.redirect_stdout(said):
                    return contract.main(), said.getvalue()
            finally:
                contract.CONTRACT, contract.SOURCE, contract.WATCHED = was

    def test_a_guard_that_reads_no_field_says_so_about_itself(self):
        # The one failure that must never be reported as the client's. A rename of how
        # Api.cpp names a field once left this matching nothing, and every schema looked
        # perfectly read.
        code, said = self.against(
            'Read<Series> series(const QJsonObject &from)\n{\n    nothing_here();\n}\n',
            {"Series": {"properties": {"id": {}}}},
            {"Series": ("whole", ["series"])},
        )
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken, not the client", said)

    def test_a_schema_the_contract_no_longer_has(self):
        code, said = self.against(
            self.SOURCE, {"Page": {"properties": {"total": {}}}},
            {"Series": ("whole", ["series"]), "Page": ("whole", ["page"])},
        )
        self.assertEqual(code, 1)
        self.assertIn("the contract no longer has this schema", said)
        self.assertIn("The contract moved and the client did not", said)

    def test_a_field_declared_and_never_read(self):
        code, said = self.against(
            self.SOURCE,
            {"Series": {"properties": {"id": {}, "work": {}, "universe": {}}}},
            {"Series": ("whole", ["series"])},
        )
        self.assertEqual(code, 1)
        self.assertIn("declared but never read: universe", said)

    def test_a_schema_read_in_part_says_what_it_left(self):
        code, said = self.against(
            self.SOURCE,
            {"Series": {"properties": {"id": {}, "work": {}, "universe": {}}}},
            {"Series": ("part", ["series"])},
        )
        self.assertEqual(code, 0)
        self.assertIn("2/3 read on purpose, left: universe", said)

    def test_the_client_as_it_stands_knows_it(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            self.assertEqual(contract.main(), 0)
        self.assertIn("The client knows the contract.", said.getvalue())


class KeepsFrenchTypography(unittest.TestCase):
    def against(self, source: str):
        """`main`, reading a `Words.cpp` written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            (folder / "Words.cpp").write_text(source, encoding="utf-8")
            was = (words_stay_french.SOURCE, words_stay_french.ROOT)
            try:
                words_stay_french.SOURCE = folder / "Words.cpp"
                words_stay_french.ROOT = folder
                said = io.StringIO()
                with contextlib.redirect_stdout(said):
                    return words_stay_french.main(), said.getvalue()
            finally:
                words_stay_french.SOURCE, words_stay_french.ROOT = was

    def test_a_straight_apostrophe_is_refused(self):
        code, said = self.against('auto a = u"c\'est prêt"_s;\n')
        self.assertEqual(code, 1)
        self.assertIn("a straight apostrophe", said)

    def test_an_ordinary_space_before_the_colon_is_refused(self):
        code, said = self.against('auto a = u"Titre : Sous-titre"_s;\n')
        self.assertEqual(code, 1)
        self.assertIn("an ordinary space before : ; ! ?", said)

    def test_the_composition_that_starts_the_literal_on_the_sign_goes_through(self):
        # `label + Words::Nbsp + u": valeur"_s` is the idiom Words.cpp actually uses: the
        # non-breaking space lives outside the literal, so the literal itself starts on the
        # colon and never has a space of its own in front of it to catch.
        code, said = self.against('auto a = label + Nbsp + u": valeur"_s;\n')
        self.assertEqual(code, 0, said)

    def test_a_guillemet_with_no_non_breaking_space_is_refused(self):
        code, said = self.against('auto a = u"« Elfes » trouvé"_s;\n')
        self.assertEqual(code, 1)
        self.assertIn("« or » without its non-breaking space", said)

    def test_a_guillemet_with_its_non_breaking_space_goes_through(self):
        code, said = self.against('auto a = u"« Elfes » trouvé"_s;\n')
        self.assertEqual(code, 0, said)

    def test_a_fault_in_the_first_fragment_of_a_split_literal_is_refused(self):
        # A literal spread across several adjacent `u"…"` tokens, with only the last one
        # carrying `_s`, is one string once the compiler concatenates them in translation
        # phase 6 — the same shape `noSeriesForThisFile()` and `verifyingMeans()` are built
        # from. A straight apostrophe sitting in the first fragment is exactly what an
        # earlier version of this guard, matching only the suffixed token, could not see.
        code, said = self.against('auto a = u"c\'est "\n          u"prêt"_s;\n')
        self.assertEqual(code, 1)
        self.assertIn("a straight apostrophe", said)

    def test_a_well_composed_literal_split_across_tokens_goes_through(self):
        code, said = self.against('auto a = u"Ceci "\n          u"est prêt."_s;\n')
        self.assertEqual(code, 0, said)

    def test_a_guillemet_split_across_the_seam_is_read_as_one_literal(self):
        # A « ending one fragment with its own content starting the next is invisible to a
        # rule that checks each fragment alone; recombining the run before checking the
        # rule is what makes the seam itself checkable.
        code, said = self.against('auto a = u"«"\n          u" Elfes » trouvé"_s;\n')
        self.assertEqual(code, 0, said)
        code, said = self.against('auto a = u"«"\n          u"Elfes » trouvé"_s;\n')
        self.assertEqual(code, 1)
        self.assertIn("« or » without its non-breaking space", said)

    def test_a_guard_that_recombines_no_literal_says_so_about_itself(self):
        # The one failure that must never be reported as Words.cpp's. The shape of a literal
        # in this client has already been rewritten three times (`_L1` → `_ascii` →
        # `u"…"_s`); the first time, a guard's pattern was not renamed with it and matched
        # nothing while reporting everything as fine. This is that case, caught before it
        # can repeat.
        code, said = self.against('QString empty() { return QString(); }\n')
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken, not", said)

    def test_a_raw_token_left_out_of_every_run_says_the_guard_is_broken(self):
        # The narrower form of the same blind-pattern failure: not every literal missed,
        # just a fragment of one — an ordinary space before `;` hid exactly this way, in a
        # fragment with no `_s` of its own, in `noSeriesForThisFile()` and
        # `verifyingMeans()`, until this check existed to catch a leftover token rather
        # than silently drop it.
        code, said = self.against(
            'auto ok = u"fine"_s;\n'
            'auto bad = u"c\'est "\n'
            '           u"cassé"_ascii;\n'
        )
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken, not", said)
        self.assertIn("joined no literal ending in `_s`", said)

    def test_the_client_as_it_stands_keeps_its_french_typography(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            self.assertEqual(words_stay_french.main(), 0)
        self.assertIn("keeps its French typography", said.getvalue())


class NothingTalksToTheConsole(unittest.TestCase):
    """The guard that exists because a debugging line reached somebody's terminal."""

    def against(self, files: dict[str, str]):
        """`main`, reading three blocks written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            # Every block must hold something, or the guard refuses for the right reason
            # and the case under test never runs.
            laid = {
                "desktop/qml/Placeholder.qml": "Item { }\n",
                "desktop/src/placeholder.cpp": "int placeholder() { return 0; }\n",
                "server/src/placeholder.rs": "pub fn placeholder() {}\n",
            }
            laid.update(files)
            for name, body in laid.items():
                at = folder / name
                at.parent.mkdir(parents=True, exist_ok=True)
                at.write_text(body, encoding="utf-8")

            was = (console.ROOT, [b.where for b in console.BLOCKS])
            try:
                console.ROOT = folder
                for block in console.BLOCKS:
                    block.where = folder / block.where.relative_to(was[0])
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    return console.main(), said.getvalue()
            finally:
                console.ROOT = was[0]
                for block, where in zip(console.BLOCKS, was[1]):
                    block.where = where

    def test_three_quiet_blocks_pass(self):
        code, said = self.against({})
        self.assertEqual(code, 0)
        self.assertIn("none of them talks to the console", said)

    def test_the_line_that_actually_shipped_is_refused(self):
        code, said = self.against(
            {"desktop/qml/ImportRow.qml":
             'Item {\n  onStageChanged: console.warn("SONDE", stage)\n}\n'}
        )
        self.assertEqual(code, 1)
        self.assertIn("ImportRow.qml:2", said)

    def test_every_way_of_reaching_the_console_is_refused(self):
        for call in ("console.log(1)", "console.debug(1)", "console.trace()",
                     "console . warn(1)"):
            with self.subTest(call=call):
                code, _ = self.against(
                    {"desktop/qml/A.qml": "Item { Component.onCompleted: %s }\n" % call})
                self.assertEqual(code, 1, call)

    def test_a_developers_line_in_the_client_is_refused(self):
        for call in ("qDebug() << x;", "qInfo() << x;"):
            with self.subTest(call=call):
                code, _ = self.against({"desktop/src/A.cpp": "void a() { %s }\n" % call})
                self.assertEqual(code, 1, call)

    def test_the_diagnostics_the_program_means_to_emit_are_left_alone(self):
        # `qWarning` is how this client says a singleton could not be resolved. A rule that
        # swept it away would be a rule with exceptions, and an exception is a judgement
        # somebody has to make every time.
        code, said = self.against(
            {"desktop/src/A.cpp": 'void a() { qWarning() << "no singleton"; }\n'})
        self.assertEqual(code, 0, said)

    def test_a_developers_line_in_the_server_is_refused(self):
        for call in ("dbg!(x);", "println!(\"{x}\");", "eprintln!(\"{x}\");"):
            with self.subTest(call=call):
                code, _ = self.against({"server/src/a.rs": "fn a() { %s }\n" % call})
                self.assertEqual(code, 1, call)

    def test_the_command_line_says_its_version_and_is_left_alone(self):
        # `main.rs` has a terminal to write to; nothing else in the server does.
        code, said = self.against({"server/src/main.rs": 'fn main() { println!("leaf"); }\n'})
        self.assertEqual(code, 0, said)

    def test_a_guard_that_reads_nothing_says_so_rather_than_passing(self):
        # The failure this shape exists to prevent, and the one `client_knows_the_contract`
        # names: a path that stops matching passes in silence and goes on passing.
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            was = (console.ROOT, [b.where for b in console.BLOCKS])
            try:
                console.ROOT = folder
                for block in console.BLOCKS:
                    block.where = folder / block.where.relative_to(was[0])
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    code = console.main()
            finally:
                console.ROOT = was[0]
                for block, where in zip(console.BLOCKS, was[1]):
                    block.where = where
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken, not the client", said.getvalue())

    def test_the_three_blocks_as_they_stand_say_nothing(self):
        said = io.StringIO()
        with contextlib.redirect_stdout(said):
            self.assertEqual(console.main(), 0)
        self.assertIn("none of them talks to the console", said.getvalue())


class CoversStayRound(unittest.TestCase):
    """The guard that exists because three shipped covers had square corners."""

    def against(self, files: dict[str, str]):
        """`main`, reading a `qml/` written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            qml = folder / "desktop" / "qml"
            qml.mkdir(parents=True)
            # Something innocent, always: a run that matches no Rectangle refuses for the
            # right reason, and the case under test would never run.
            laid = {"Fine.qml": "Rectangle {\n    radius: 6\n}\n"}
            laid.update(files)
            for name, body in laid.items():
                (qml / name).write_text(body, encoding="utf-8")

            was = (covers_stay_round.ROOT, covers_stay_round.QML)
            try:
                covers_stay_round.ROOT = folder
                covers_stay_round.QML = qml
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    return covers_stay_round.main(), said.getvalue()
            finally:
                covers_stay_round.ROOT, covers_stay_round.QML = was

    def test_a_rounded_rectangle_that_does_not_clip_passes(self):
        code, said = self.against({})
        self.assertEqual(code, 0, said)
        self.assertIn("none of them clips to a rounding it does not have", said)

    def test_the_cover_that_actually_shipped_is_refused(self):
        code, said = self.against(
            {"SeriesHeader.qml": "Item {\n"
                                 "    Rectangle {\n"
                                 "        radius: Theme.coverRadius\n"
                                 "        clip: true\n"
                                 "        Image { }\n"
                                 "    }\n"
                                 "}\n"}
        )
        self.assertEqual(code, 1)
        self.assertIn("SeriesHeader.qml:2", said)

    def test_clipping_without_a_rounding_is_left_alone(self):
        # A square rectangle that clips tells the truth: there is no corner to lie about.
        code, said = self.against({"List.qml": "Rectangle {\n    clip: true\n}\n"})
        self.assertEqual(code, 0, said)

    def test_a_childs_clip_is_not_its_parents(self):
        # The row of the import dialog rounds its own corners and holds a clipped list. It
        # is not the defect, and a guard reading the whole subtree would accuse every panel.
        code, said = self.against(
            {"Row.qml": "Rectangle {\n"
                        "    radius: 8\n"
                        "    ListView {\n"
                        "        clip: true\n"
                        "    }\n"
                        "}\n"}
        )
        self.assertEqual(code, 0, said)

    def test_it_refuses_to_see_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            empty = folder / "desktop" / "qml"
            empty.mkdir(parents=True)
            was = (covers_stay_round.ROOT, covers_stay_round.QML)
            try:
                covers_stay_round.ROOT, covers_stay_round.QML = folder, empty
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    code = covers_stay_round.main()
            finally:
                covers_stay_round.ROOT, covers_stay_round.QML = was
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken", said.getvalue())


class TestsStayOffTheBus(unittest.TestCase):
    """The guard that exists because green test runs wrote on somebody's desktop."""

    def against(self, files: dict[str, str]):
        """`main`, reading a `desktop/tests/` written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            tests = folder / "desktop" / "tests"
            tests.mkdir(parents=True)
            # A boot that is handed one, always: a run that finds none refuses for the right
            # reason and the case under test never runs.
            laid = {"fine.cpp": "void a() { Boot::run(engine, *qGuiApp, nowhere()); }\n"}
            laid.update(files)
            for name, body in laid.items():
                (tests / name).write_text(body, encoding="utf-8")

            was = (bus.ROOT, bus.TESTS)
            try:
                bus.ROOT, bus.TESTS = folder, tests
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    return bus.main(), said.getvalue()
            finally:
                bus.ROOT, bus.TESTS = was

    def test_a_boot_handed_a_bus_passes(self):
        code, said = self.against({})
        self.assertEqual(code, 0, said)
        self.assertIn("none of them is the session's", said)

    def test_the_boot_that_actually_shipped_is_refused(self):
        code, said = self.against(
            {"seam.cpp": "void a() { Boot::run(engine, *qGuiApp); }\n"})
        self.assertEqual(code, 1)
        self.assertIn("seam.cpp:1", said)

    def test_a_notifier_on_the_default_bus_is_refused(self):
        for built in ("Notifier bus;", "Notifier bus();"):
            with self.subTest(built=built):
                code, _ = self.against({"warns.cpp": "void a() { %s }\n" % built})
                self.assertEqual(code, 1, built)

    def test_a_notifier_handed_one_is_left_alone(self):
        code, said = self.against(
            {"warns.cpp": 'void a() { Notifier bus(QDBusConnection(u"nowhere"_s)); }\n'})
        self.assertEqual(code, 0, said)

    def test_the_static_that_sends_nothing_is_left_alone(self):
        # `announcement` composes and returns; it has no bus to speak on.
        code, said = self.against(
            {"warns.cpp": 'void a() { auto m = Notifier::announcement(u"a"_s, u"b"_s); }\n'})
        self.assertEqual(code, 0, said)

    def test_a_line_of_comment_explains_the_rule_without_breaking_it(self):
        code, said = self.against(
            {"warns.cpp": "// Handed one, never `Boot::run(engine, app)` on its own.\n"})
        self.assertEqual(code, 0, said)

    def test_it_refuses_to_see_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            empty = folder / "desktop" / "tests"
            empty.mkdir(parents=True)
            was = (bus.ROOT, bus.TESTS)
            try:
                bus.ROOT, bus.TESTS = folder, empty
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    code = bus.main()
            finally:
                bus.ROOT, bus.TESTS = was
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken", said.getvalue())


class OneFileOwnsIt(unittest.TestCase):
    """The guard that exists because eleven places wrote the same six lines."""

    def against(self, files: dict[str, str]):
        """`main`, reading a `qml/` written for the occasion."""
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            qml = folder / "desktop" / "qml"
            qml.mkdir(parents=True)
            # Every owner must hold its own shape, or the guard refuses for the right reason
            # and the case under test never runs.
            laid = {
                "Glyph.qml": "Item { ColorOverlay { } }\n",
                "CardLift.qml": 'Rectangle {\n  color: "#000000"\n  z: -1\n}\n',
                "RoundedCover.qml": "Item { OpacityMask { } }\n",
            }
            laid.update(files)
            for name, body in laid.items():
                (qml / name).write_text(body, encoding="utf-8")

            was = (owns.ROOT, owns.QML)
            try:
                owns.ROOT, owns.QML = folder, qml
                said = io.StringIO()
                with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
                    return owns.main(), said.getvalue()
            finally:
                owns.ROOT, owns.QML = was

    def test_one_owner_each_passes(self):
        code, said = self.against({})
        self.assertEqual(code, 0, said)
        self.assertIn("no second copy", said)

    def test_a_second_tinted_glyph_is_refused(self):
        code, said = self.against({"Bar.qml": "Item { ColorOverlay { source: x } }\n"})
        self.assertEqual(code, 1)
        self.assertIn("Bar.qml:1", said)

    def test_a_second_lift_is_refused(self):
        code, said = self.against(
            {"Card.qml": 'Rectangle {\n  color: "#000000"\n  z: -1\n}\n'})
        self.assertEqual(code, 1, said)

    def test_a_hairline_behind_a_row_is_not_a_lift(self):
        # `z: -1` on its own is any line drawn behind something. The pair is the shape.
        code, said = self.against(
            {"Tabs.qml": "Rectangle {\n  color: Theme.rule\n  z: -1\n}\n"})
        self.assertEqual(code, 0, said)

    def test_a_veil_is_not_a_lift_either(self):
        # A black fill with nothing behind it is a veil, which several covers wear.
        code, said = self.against(
            {"Menu.qml": 'Rectangle {\n  color: "#000000"\n  opacity: 0.74\n}\n'})
        self.assertEqual(code, 0, said)

    def test_a_second_rounded_mask_is_refused(self):
        code, said = self.against(
            {"Band.qml": "Image { layer.effect: OpacityMask { } }\n"})
        self.assertEqual(code, 1, said)

    def test_a_line_of_comment_explains_the_rule_without_breaking_it(self):
        code, said = self.against({"Note.qml": "// A ColorOverlay belongs in Glyph.qml.\n"})
        self.assertEqual(code, 0, said)

    def test_it_refuses_an_owner_that_lost_its_own_shape(self):
        code, said = self.against({"Glyph.qml": "Item { }\n"})
        self.assertEqual(code, 2)
        self.assertIn("this guard is broken", said)


if __name__ == "__main__":
    unittest.main()
