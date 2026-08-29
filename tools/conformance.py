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

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8477"
KEY = sys.argv[2] if len(sys.argv) > 2 else ""
PIN = (sys.argv[3] if len(sys.argv) > 3 else "").replace(":", "").lower()

SPEC = yaml.safe_load(open("contract/openapi.yaml"))
# The contract's own path templates. They are the keys this script looks the schemas up
# under, so they have to read exactly as `openapi.yaml` spells them — naming them once
# means a rename in the contract is a rename here, and not four of them.
SERIES = "/series"
ONE_SERIES = "/series/{id}"
ONE_ENTRY = "/entries/{id}"
ENTRY_PROGRESS = "/entries/{id}/progress"
UP_NEXT = "/next"
IMPORT = "/import"
ONE_IMPORT = "/import/{id}"

CONTEXT = None

if BASE.startswith("https://"):
    host, _, port = BASE[len("https://"):].partition(":")
    offered = ssl.get_server_certificate((host, int(port or 443)))
    digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(offered)).hexdigest()
    if not PIN:
        sys.exit("this server signs its own certificate: pass its fingerprint as the third "
                 f"argument.\n  it is offering {digest}")
    if digest != PIN:
        sys.exit(f"the certificate offered is not the one pinned\n  offered {digest}\n  pinned  {PIN}")
    print(f"certificate pinned: {digest[:16]}…")
    # Verified by hand above, which is the whole point of pinning: no authority is involved.
    CONTEXT = ssl._create_unverified_context()

failures = []


def call(path, method="GET", body=None, headers=None):
    sent = {"X-Leaf-Key": KEY} if KEY else {}
    sent.update(headers or {})
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        sent["Content-Type"] = "application/json"
    request = urllib.request.Request(BASE + path, data=data, headers=sent, method=method)
    try:
        with urllib.request.urlopen(request, context=CONTEXT) as response:
            raw = response.read()
            return response.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read()
        return e.code, (json.loads(raw) if raw else None)


def resolve(node, seen=()):
    """Follows $ref, and stops rather than looping on a schema that refers to itself."""
    if isinstance(node, dict):
        if "$ref" in node:
            key = node["$ref"]
            if key in seen:
                return {}
            target = SPEC
            for part in key.lstrip("#/").split("/"):
                target = target[part]
            return resolve(target, seen + (key,))
        return {k: resolve(v, seen) for k, v in node.items()}
    if isinstance(node, list):
        return [resolve(v, seen) for v in node]
    return node


def schema_for(template, method, status):
    operation = SPEC["paths"].get(template, {}).get(method.lower())
    if not operation:
        return None, f"the contract declares no {method} {template}"
    response = operation.get("responses", {}).get(str(status))
    if not response:
        return None, f"the contract declares no {status} for {method} {template}"
    content = resolve(response).get("content", {}).get("application/json")
    return (content["schema"] if content else None), None


def check(label, path, template, method="GET", body=None, headers=None):
    status, payload = call(path, method, body, headers)
    schema, why = schema_for(template, method, status)
    if why:
        failures.append(f"{label}: {status} — {why}")
        print(f"  ✗ {label:34} {status}  {why}")
        return payload
    if schema is None:
        print(f"  · {label:34} {status}  no body declared")
        return payload
    errors = sorted(Draft202012Validator(schema).iter_errors(payload), key=lambda e: list(e.path))
    if errors:
        for e in errors[:4]:
            where = "/".join(str(p) for p in e.absolute_path) or "(root)"
            failures.append(f"{label}: {where}: {e.message}")
            print(f"  ✗ {label:34} {status}  {where}: {e.message[:100]}")
    else:
        print(f"  ✓ {label:34} {status}")
    return payload


print("— reading —")
check("health", "/health", "/health")
page = check("series", SERIES, SERIES)
series = page["items"][0]["id"]
check("filters", "/filters", "/filters")
check("format", "/format", "/format")
check("one series", f"/series/{series}", ONE_SERIES)
entries = check("series entries", f"/series/{series}/entries", "/series/{id}/entries")
check("series chapters", f"/series/{series}/chapters", "/series/{id}/chapters")
check("series arcs", f"/series/{series}/arcs", "/series/{id}/arcs")
check("series progress", f"/series/{series}/progress", "/series/{id}/progress")
check("unknown series", "/series/nope", ONE_SERIES)
entry = entries[0]["id"]
check("one entry", f"/entries/{entry}", ONE_ENTRY)
check("entry chapters", f"/entries/{entry}/chapters", "/entries/{id}/chapters")
check("entry pages", f"/entries/{entry}/pages", "/entries/{id}/pages")
check("unknown entry", "/entries/nope", ONE_ENTRY)
check("search", "/search?q=a", "/search")
check("search by kind", "/search?q=a&kind=CHAPTER", "/search")
check("up next", UP_NEXT, UP_NEXT)
check("scan status", "/scan", "/scan")

print("— progress —")
check("record", f"/entries/{entry}/progress", ENTRY_PROGRESS, "PATCH", {"page": 1})
check("read back", f"/entries/{entry}/progress", ENTRY_PROGRESS)
check("up next after", UP_NEXT, UP_NEXT)
check("forget", f"/entries/{entry}/progress", ENTRY_PROGRESS, "DELETE")

print("— records —")
check("patch series", f"/series/{series}", ONE_SERIES, "PATCH", {"summary": "…"})
check("patch unknown", "/series/nope", ONE_SERIES, "PATCH", {"summary": "…"})
check("patch arcs", f"/series/{series}/arcs", "/series/{id}/arcs", "PATCH",
      [{"name": "Un cycle", "unit": "VOLUME", "from": 1, "to": 2}])
check("patch entry", f"/entries/{entry}", ONE_ENTRY, "PATCH", {"title": "Un titre"})

print("— import —")
check("drop listing", "/drop", "/drop")
check("waiting intake", "/intake", "/intake")
check("open imports", IMPORT, IMPORT)
opened = check("open import", IMPORT, IMPORT, "POST",
               {"root": "Essai", "files": [{"path": "Tome 1.cbz", "size": 4}]})
check("import state", f"/import/{opened['id']}", ONE_IMPORT)
check("unknown import", "/import/imp_deadbeef", ONE_IMPORT)
check("abandon import", f"/import/{opened['id']}", ONE_IMPORT, "DELETE")
check("cleanup", "/cleanup", "/cleanup", "POST", {"root": "Essai", "files": []})

if KEY:
    print("— the guard —")
    check("no key", SERIES, SERIES, headers={"X-Leaf-Key": ""})

print()
if failures:
    print(f"{len(failures)} divergence(s) between the server and its contract")
    sys.exit(1)
print("the server answers what the contract promises")
