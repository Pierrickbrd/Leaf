#!/usr/bin/env python3
"""Checks a running server against contract/openapi.yaml.

The contract is what both clients will be generated from, so a divergence between it and
the server is not a documentation problem — it is a client that will not compile, or worse,
one that compiles and reads a field that is never sent.

Hand-written assertions cannot cover thirty-six operations and their schemas. This can:
every response is validated against the schema the contract declares for the status it
came back with, so a renamed field, a number spelled as text or a missing required key is
caught the moment it happens.

    tools/conformance.py [base-url] [key] [certificate-fingerprint]

Over https the server holds a certificate it signed itself, which is the arrangement the
clients use: they trust the fingerprint the server prints at startup rather than a public
authority. Give that fingerprint as the third argument and this speaks to it the same way —
which also makes it the only thing that checks the pinning story works at all.

Exits non-zero on the first mismatch, so CI can hold the line.
"""

import hashlib
import json
import ssl
import sys
import urllib.error
import urllib.request

import yaml
from jsonschema import Draft202012Validator

DEFAULT_BASE = "http://127.0.0.1:8477"
CONTRACT = "contract/openapi.yaml"

# The contract's own path templates. They are the keys the schemas are looked up under, so
# they have to read exactly as `openapi.yaml` spells them — naming them once means a rename
# in the contract is a rename here, and not four of them.
SERIES = "/series"
ONE_SERIES = "/series/{id}"
ONE_ENTRY = "/entries/{id}"
ENTRY_PROGRESS = "/entries/{id}/progress"
UP_NEXT = "/next"
IMPORT = "/import"
ONE_IMPORT = "/import/{id}"


def trust(base, pin):
    """How to trust a server that signed its own certificate, or nothing over plain http.

    The pinned certificate becomes the only authority there is: Python verifies against it,
    and against nothing else. Which certificate that is was settled by the fingerprint — so
    the check is not skipped, its trust anchor is simply this one server rather than a public
    root store. Hostname verification stays on: the certificate carries the host it was
    generated for as a subject alternative name.
    """
    if not base.startswith("https://"):
        return None
    host, _, port = base[len("https://"):].partition(":")
    offered = ssl.get_server_certificate((host, int(port or 443)))
    digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(offered)).hexdigest()
    if not pin:
        raise ValueError("this server signs its own certificate: pass its fingerprint as "
                         f"the third argument.\n  it is offering {digest}")
    if digest != pin:
        raise ValueError("the certificate offered is not the one pinned"
                         f"\n  offered {digest}\n  pinned  {pin}")
    print(f"certificate pinned: {digest[:16]}…")
    context = ssl.create_default_context(cadata=offered)
    # Written down rather than inherited. Python's defaults are already this on any version
    # this script would run on, but a floor that depends on the interpreter is a floor
    # nobody can read from here — and the server speaks 1.2 and 1.3, nothing older.
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    return context


class Server:
    """The server under test: where it is, the key to reach it, and how to trust it."""

    def __init__(self, base, key="", context=None):
        self.base = base
        self.key = key
        self.context = context

    def call(self, path, method="GET", body=None, headers=None):
        sent = {"X-Leaf-Key": self.key} if self.key else {}
        sent.update(headers or {})
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            sent["Content-Type"] = "application/json"
        request = urllib.request.Request(self.base + path, data=data, headers=sent, method=method)
        try:
            with urllib.request.urlopen(request, context=self.context) as response:
                raw = response.read()
                return response.status, (json.loads(raw) if raw else None)
        except urllib.error.HTTPError as e:
            raw = e.read()
            return e.code, (json.loads(raw) if raw else None)


class Contract:
    """The contract, and the schema it declares for one answer."""

    def __init__(self, spec):
        self.spec = spec

    @classmethod
    def read(cls, path):
        with open(path, encoding="utf-8") as handle:
            return cls(yaml.safe_load(handle))

    def resolve(self, node, seen=()):
        """Follows $ref, and stops rather than looping on a schema that refers to itself."""
        if isinstance(node, dict):
            if "$ref" in node:
                key = node["$ref"]
                if key in seen:
                    return {}
                target = self.spec
                for part in key.lstrip("#/").split("/"):
                    target = target[part]
                return self.resolve(target, seen + (key,))
            return {k: self.resolve(v, seen) for k, v in node.items()}
        if isinstance(node, list):
            return [self.resolve(v, seen) for v in node]
        return node

    def schema_for(self, template, method, status):
        operation = self.spec["paths"].get(template, {}).get(method.lower())
        if not operation:
            return None, f"the contract declares no {method} {template}"
        response = operation.get("responses", {}).get(str(status))
        if not response:
            return None, f"the contract declares no {status} for {method} {template}"
        content = self.resolve(response).get("content", {}).get("application/json")
        return (content["schema"] if content else None), None


class Run:
    """One conformance run: every call it makes, and every divergence it found."""

    def __init__(self, server, contract):
        self.server = server
        self.contract = contract
        self.failures = []

    def check(self, label, path, template, method="GET", body=None, headers=None):
        status, payload = self.server.call(path, method, body, headers)
        schema, why = self.contract.schema_for(template, method, status)
        if why:
            self.failures.append(f"{label}: {status} — {why}")
            print(f"  ✗ {label:34} {status}  {why}")
            return payload
        if schema is None:
            print(f"  · {label:34} {status}  no body declared")
            return payload
        errors = sorted(Draft202012Validator(schema).iter_errors(payload),
                        key=lambda e: list(e.path))
        if not errors:
            print(f"  ✓ {label:34} {status}")
            return payload
        for e in errors[:4]:
            where = "/".join(str(p) for p in e.absolute_path) or "(root)"
            self.failures.append(f"{label}: {where}: {e.message}")
            print(f"  ✗ {label:34} {status}  {where}: {e.message[:100]}")
        return payload


def reading(run):
    """Everything that only reads, and the two ids the rest of the run needs."""
    print("— reading —")
    run.check("health", "/health", "/health")
    page = run.check("series", SERIES, SERIES)
    series = page["items"][0]["id"]
    run.check("filters", "/filters", "/filters")
    run.check("format", "/format", "/format")
    run.check("one series", f"/series/{series}", ONE_SERIES)
    entries = run.check("series entries", f"/series/{series}/entries", "/series/{id}/entries")
    run.check("series chapters", f"/series/{series}/chapters", "/series/{id}/chapters")
    run.check("series arcs", f"/series/{series}/arcs", "/series/{id}/arcs")
    run.check("series progress", f"/series/{series}/progress", "/series/{id}/progress")
    run.check("unknown series", "/series/nope", ONE_SERIES)
    entry = entries[0]["id"]
    run.check("one entry", f"/entries/{entry}", ONE_ENTRY)
    run.check("entry chapters", f"/entries/{entry}/chapters", "/entries/{id}/chapters")
    run.check("entry pages", f"/entries/{entry}/pages", "/entries/{id}/pages")
    run.check("unknown entry", "/entries/nope", ONE_ENTRY)
    run.check("search", "/search?q=a", "/search")
    run.check("search by kind", "/search?q=a&kind=CHAPTER", "/search")
    run.check("up next", UP_NEXT, UP_NEXT)
    run.check("scan status", "/scan", "/scan")
    return series, entry


def progress(run, entry):
    print("— progress —")
    run.check("record", f"/entries/{entry}/progress", ENTRY_PROGRESS, "PATCH", {"page": 1})
    run.check("read back", f"/entries/{entry}/progress", ENTRY_PROGRESS)
    run.check("up next after", UP_NEXT, UP_NEXT)
    run.check("forget", f"/entries/{entry}/progress", ENTRY_PROGRESS, "DELETE")


def records(run, series, entry):
    print("— records —")
    run.check("patch series", f"/series/{series}", ONE_SERIES, "PATCH", {"summary": "…"})
    run.check("patch unknown", "/series/nope", ONE_SERIES, "PATCH", {"summary": "…"})
    run.check("patch arcs", f"/series/{series}/arcs", "/series/{id}/arcs", "PATCH",
              [{"name": "Un cycle", "unit": "VOLUME", "from": 1, "to": 2}])
    run.check("patch entry", f"/entries/{entry}", ONE_ENTRY, "PATCH", {"title": "Un titre"})


def importing(run):
    print("— import —")
    run.check("drop listing", "/drop", "/drop")
    run.check("waiting intake", "/intake", "/intake")
    run.check("open imports", IMPORT, IMPORT)
    opened = run.check("open import", IMPORT, IMPORT, "POST",
                       {"root": "Essai", "files": [{"path": "Tome 1.cbz", "size": 4}]})
    run.check("import state", f"/import/{opened['id']}", ONE_IMPORT)
    run.check("unknown import", "/import/imp_deadbeef", ONE_IMPORT)
    run.check("abandon import", f"/import/{opened['id']}", ONE_IMPORT, "DELETE")
    run.check("cleanup", "/cleanup", "/cleanup", "POST", {"root": "Essai", "files": []})


def the_guard(run):
    """Only when a key was given: without one there is nothing for the guard to refuse."""
    print("— the guard —")
    run.check("no key", SERIES, SERIES, headers={"X-Leaf-Key": ""})


def main(argv=None, contract=CONTRACT):
    argv = sys.argv[1:] if argv is None else argv
    base = argv[0] if argv else DEFAULT_BASE
    key = argv[1] if len(argv) > 1 else ""
    pin = (argv[2] if len(argv) > 2 else "").replace(":", "").lower()

    try:
        context = trust(base, pin)
    except ValueError as refusal:
        print(str(refusal), file=sys.stderr)
        return 2

    run = Run(Server(base, key, context), Contract.read(contract))
    series, entry = reading(run)
    progress(run, entry)
    records(run, series, entry)
    importing(run)
    if key:
        the_guard(run)

    print()
    if run.failures:
        print(f"{len(run.failures)} divergence(s) between the server and its contract")
        return 1
    print("the server answers what the contract promises")
    return 0


if __name__ == "__main__":
    sys.exit(main())
