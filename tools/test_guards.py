"""The two guards that stand between the client and the contract, tested as functions.

They run under ctest as well, as whole programs. That is where they earn their keep and it
is also why nothing measured them: a subprocess is invisible to coverage, so 91 lines of
guard read as 91 lines nobody exercises.

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


if __name__ == "__main__":
    unittest.main()
