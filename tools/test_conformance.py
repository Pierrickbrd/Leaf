"""The conformance check, run against a server invented from the contract itself.

The server here answers whatever `openapi.yaml` says it should, generated from the schema
for the status it returns. That makes the happy run circular on purpose: what is being
tested is not the server — there is none — but that every check in the list is reached, that
a divergence is caught and named, and that a broken contract is reported as a broken
contract rather than a broken server.

    python3 -m unittest discover -s tools -p 'test_*.py'
"""

import contextlib
import hashlib
import http.server
import io
import json
import re
import threading
import unittest
import urllib.error
from unittest import mock

import conformance
from conformance import Contract, Run, Server, main, trust


def an_instance(schema, contract, depth=0):
    """The smallest value that satisfies `schema`.

    Enough of JSON Schema for this contract and no more: objects keep only their required
    properties, arrays hold exactly one element, and a `$ref` is followed once.
    """
    schema = contract.resolve(schema) if depth == 0 else schema
    for combinator in ("oneOf", "anyOf", "allOf"):
        if combinator in schema:
            return an_instance(schema[combinator][0], contract, depth + 1)
    if "enum" in schema:
        return schema["enum"][0]
    kind = schema.get("type")
    if isinstance(kind, list):
        kind = next((k for k in kind if k != "null"), "string")
    if kind == "object":
        properties = schema.get("properties", {})
        return {
            name: an_instance(properties[name], contract, depth + 1)
            for name in schema.get("required", [])
            if name in properties
        }
    if kind == "array":
        item = schema.get("items")
        return [an_instance(item, contract, depth + 1)] if item else []
    if kind == "integer":
        return 1
    if kind == "number":
        return 1.0
    if kind == "boolean":
        return False
    if kind == "null":
        return None
    return "x"


class FromTheContract(http.server.BaseHTTPRequestHandler):
    """Answers every route the way the contract says it will."""

    contract = None
    templates = ()
    perturb = None

    def log_message(self, *_):
        pass

    def _template(self, path):
        for template, pattern in self.templates:
            if pattern.fullmatch(path):
                return template
        return None

    def _answer(self):
        path = self.path.split("?")[0]
        template = self._template(path)
        operation = (self.contract.spec["paths"].get(template) or {}).get(self.command.lower())
        if operation is None:
            self.send_response(404)
            self.end_headers()
            return
        declared = operation.get("responses", {})
        # A made-up id asks for the "not there" answer, which is what the run checks next to
        # every "here it is". Otherwise the first success — the contract does not list its
        # responses in numerical order, and /series/{id}/entries declares 403 before 200.
        missing = "nope" in path or "deadbeef" in path
        if missing and "404" in declared:
            status = "404"
        else:
            status = next((s for s in declared if s.startswith("2")), next(iter(declared)))
        content = self.contract.resolve(declared[status]).get("content", {})
        schema = (content.get("application/json") or {}).get("schema")

        body = b""
        if schema is not None:
            value = an_instance(schema, self.contract)
            if self.perturb:
                value = self.perturb(template, value)
            body = json.dumps(value).encode()
        self.send_response(int(status))
        if body:
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    do_GET = do_POST = do_PATCH = do_DELETE = _answer


def templates_of(contract):
    """Each path template, with the regex that recognises a real path as one of it."""
    out = []
    for template in contract.spec["paths"]:
        pattern = re.escape(template).replace(r"\{id\}", r"[^/]+").replace(r"\{n\}", r"[^/]+")
        out.append((template, re.compile(pattern)))
    # Longest first, so /series/{id}/entries wins over /series/{id}.
    return sorted(out, key=lambda t: -len(t[0]))


@contextlib.contextmanager
def a_server(contract, perturb=None):
    # `staticmethod`, because a plain function put on a class becomes a bound method and
    # would be handed `self` as its first argument.
    handler = type("Bound", (FromTheContract,), {
        "contract": contract,
        "templates": templates_of(contract),
        "perturb": staticmethod(perturb) if perturb else None,
    })
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{httpd.server_address[1]}"
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=5)


def quietly(*argv):
    said = io.StringIO()
    with contextlib.redirect_stdout(said), contextlib.redirect_stderr(said):
        code = main(list(argv))
    return code, said.getvalue()


CONTRACT = Contract.read(conformance.CONTRACT)


class RunsEveryCheck(unittest.TestCase):
    def test_a_server_that_answers_the_contract_passes(self):
        with a_server(CONTRACT) as base:
            code, said = quietly(base)
        self.assertEqual(code, 0, said)
        self.assertIn("the server answers what the contract promises", said)
        for stage in ("— reading —", "— progress —", "— records —", "— import —"):
            self.assertIn(stage, said)
        self.assertNotIn("✗", said)

    def test_every_check_in_the_list_is_reached(self):
        with a_server(CONTRACT) as base:
            _, said = quietly(base)
        # One line per check, and the count is the thing: a stage that stopped early would
        # leave the rest silently unrun.
        self.assertEqual(said.count("✓") + said.count("·"), 34)

    def test_a_key_brings_the_guard_out(self):
        with a_server(CONTRACT) as base:
            code, said = quietly(base, "a-key")
        self.assertEqual(code, 0, said)
        self.assertIn("— the guard —", said)
        self.assertEqual(said.count("✓") + said.count("·"), 35)

    def test_an_answer_that_diverges_is_named_and_fails_the_run(self):
        def drop_the_name(template, value):
            if template == "/series/{id}" and isinstance(value, dict):
                value.pop("name", None)
            return value

        with a_server(CONTRACT, perturb=drop_the_name) as base:
            code, said = quietly(base)
        self.assertEqual(code, 1)
        self.assertIn("divergence(s) between the server and its contract", said)
        self.assertIn("'name' is a required property", said)


class ReadsTheContract(unittest.TestCase):
    def test_a_ref_is_followed(self):
        contract = Contract({"components": {"schemas": {"A": {"type": "string"}}}})
        self.assertEqual(contract.resolve({"$ref": "#/components/schemas/A"}),
                         {"type": "string"})

    def test_a_ref_that_points_at_itself_stops(self):
        # A schema that contains itself — a chapter holding chapters — would otherwise
        # recurse until Python gave up.
        contract = Contract({"components": {"schemas": {
            "Node": {"type": "object",
                     "properties": {"child": {"$ref": "#/components/schemas/Node"}}}}}})
        self.assertEqual(
            contract.resolve({"$ref": "#/components/schemas/Node"}),
            {"type": "object", "properties": {"child": {}}},
        )

    def test_a_list_of_refs_is_followed_through(self):
        contract = Contract({"components": {"schemas": {"A": {"type": "integer"}}}})
        self.assertEqual(contract.resolve([{"$ref": "#/components/schemas/A"}, 3]),
                         [{"type": "integer"}, 3])

    def test_an_operation_the_contract_does_not_declare(self):
        schema, why = Contract({"paths": {}}).schema_for("/series", "GET", 200)
        self.assertIsNone(schema)
        self.assertIn("declares no GET /series", why)

    def test_a_status_the_contract_does_not_declare(self):
        contract = Contract({"paths": {"/series": {"get": {"responses": {"200": {}}}}}})
        schema, why = contract.schema_for("/series", "GET", 500)
        self.assertIsNone(schema)
        self.assertIn("declares no 500", why)

    def test_a_response_that_declares_no_body(self):
        contract = Contract(
            {"paths": {"/x": {"delete": {"responses": {"204": {"description": "gone"}}}}}}
        )
        schema, why = contract.schema_for("/x", "DELETE", 204)
        self.assertIsNone(schema)
        self.assertIsNone(why)


class SaysWhenTheContractIsTheProblem(unittest.TestCase):
    def test_an_undeclared_operation_is_reported_against_the_contract(self):
        contract = Contract({"paths": {}})
        with a_server(CONTRACT) as base:
            run = Run(Server(base), contract)
            with contextlib.redirect_stdout(io.StringIO()) as said:
                run.check("health", "/health", "/health")
        self.assertEqual(len(run.failures), 1)
        self.assertIn("the contract declares no GET /health", said.getvalue())


class TrustsOnlyWhatItWasPinned(unittest.TestCase):
    """The pinning story, with the handshake replaced rather than staged.

    A real self-signed server would test OpenSSL; what matters here is the decision made
    about the certificate it offers, which is this script's alone.
    """

    DER = b"the bytes of a certificate"

    @contextlib.contextmanager
    def offering(self):
        with mock.patch.object(conformance.ssl, "get_server_certificate", return_value="PEM"), \
             mock.patch.object(conformance.ssl, "PEM_cert_to_DER_cert", return_value=self.DER):
            yield hashlib.sha256(self.DER).hexdigest()

    def test_plain_http_needs_no_context_at_all(self):
        self.assertIsNone(trust("http://127.0.0.1:8477", ""))

    def test_no_fingerprint_is_refused_and_the_one_offered_is_named(self):
        # Naming it is the point: it is how you get the fingerprint to pin in the first
        # place, without reading it off the server's startup log.
        with self.offering() as digest:
            with self.assertRaises(ValueError) as refused:
                trust("https://leaf.example:8443", "")
        self.assertIn("pass its fingerprint", str(refused.exception))
        self.assertIn(digest, str(refused.exception))

    def test_a_fingerprint_that_is_not_the_one_offered_is_refused(self):
        with self.offering() as digest:
            with self.assertRaises(ValueError) as refused:
                trust("https://leaf.example:8443", "0" * 64)
        self.assertIn("not the one pinned", str(refused.exception))
        self.assertIn(digest, str(refused.exception))

    def test_a_host_with_no_port_is_read_as_443(self):
        with self.offering(), mock.patch.object(conformance.ssl, "get_server_certificate") as got:
            got.return_value = "PEM"
            with self.assertRaises(ValueError):
                trust("https://leaf.example", "")
            got.assert_called_once_with(("leaf.example", 443))

    def test_the_pinned_certificate_becomes_the_only_authority(self):
        made = mock.MagicMock()
        with self.offering() as digest, \
             mock.patch.object(conformance.ssl, "create_default_context", return_value=made) as build, \
             contextlib.redirect_stdout(io.StringIO()) as said:
            context = trust("https://leaf.example:8443", digest)
        self.assertIs(context, made)
        build.assert_called_once_with(cadata="PEM")
        self.assertEqual(made.minimum_version, conformance.ssl.TLSVersion.TLSv1_2)
        self.assertIn("certificate pinned", said.getvalue())

    def test_a_fingerprint_may_be_pasted_with_its_colons(self):
        # It is printed colon-separated and upper-case; nobody should have to reformat it.
        with self.offering() as digest, \
             mock.patch.object(conformance.ssl, "create_default_context", mock.MagicMock()):
            spaced = ":".join(digest[i:i + 2] for i in range(0, len(digest), 2)).upper()
            with self.assertRaises(urllib.error.URLError):
                # Past the pin, and then straight into a host that does not exist — which is
                # as far as this needs to go.
                quietly("https://leaf.example:8443", "", spaced)

    def test_a_refusal_stops_the_run_before_a_single_call(self):
        with self.offering():
            code, said = quietly("https://leaf.example:8443", "", "0" * 64)
        self.assertEqual(code, 2)
        self.assertIn("not the one pinned", said)


if __name__ == "__main__":
    unittest.main()
