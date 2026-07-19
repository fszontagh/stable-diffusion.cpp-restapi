#!/usr/bin/env python3
"""Generate docs/API_REFERENCE.md from the live /openapi.json.

Usage:
    ./scripts/generate-api-docs.py [--url http://localhost:8077/openapi.json] [--out docs/API_REFERENCE.md]

The generated file is a canonical, always-current reference. Hand-written
docs (API.md, LLM_GUIDE.md, MCP.md) should link here for field-level truth
rather than duplicating field lists that go stale.
"""
from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from pathlib import Path
from typing import Any


def load_spec(src: str) -> dict[str, Any]:
    if src.startswith(("http://", "https://")):
        with urllib.request.urlopen(src, timeout=10) as r:
            return json.load(r)
    return json.loads(Path(src).read_text())


def ref_name(ref: str) -> str:
    return ref.rsplit("/", 1)[-1]


def type_str(schema: dict[str, Any]) -> str:
    if "$ref" in schema:
        n = ref_name(schema["$ref"])
        return f"[{n}](#schema-{n.lower()})"
    if "allOf" in schema:
        parts = [type_str(s) for s in schema["allOf"]]
        return " & ".join(parts)
    if "oneOf" in schema:
        return " | ".join(type_str(s) for s in schema["oneOf"])
    t = schema.get("type", "any")
    if t == "array":
        inner = schema.get("items", {})
        return f"array<{type_str(inner)}>"
    if "enum" in schema:
        vals = ", ".join(f"`{v}`" for v in schema["enum"])
        return f"enum ({vals})"
    if schema.get("format"):
        return f"{t} ({schema['format']})"
    return t


def esc(s: str) -> str:
    return str(s).replace("|", "\\|").replace("\n", " ")


def render_schema(name: str, schema: dict[str, Any]) -> list[str]:
    out: list[str] = [f"### schema `{name}` <a id=\"schema-{name.lower()}\"></a>", ""]
    desc = schema.get("description")
    if desc:
        out += [esc(desc), ""]
    if "allOf" in schema:
        bases = [ref_name(s["$ref"]) for s in schema["allOf"] if "$ref" in s]
        if bases:
            refs = ", ".join(f"[{b}](#schema-{b.lower()})" for b in bases)
            out += [f"Extends: {refs}", ""]
    props = schema.get("properties") or {}
    required = set(schema.get("required") or [])
    if "allOf" in schema:
        for sub in schema["allOf"]:
            if "properties" in sub:
                props = {**sub["properties"], **props}
            required |= set(sub.get("required") or [])
    if not props:
        out += ["_(no properties)_", ""]
        return out
    out += ["| field | type | required | default | description |",
            "|---|---|---|---|---|"]
    for pname, pschema in props.items():
        req = "yes" if pname in required else ""
        default = pschema.get("default", "")
        if default is not None and default != "" and not isinstance(default, (str, int, float, bool)):
            default = json.dumps(default)
        elif isinstance(default, bool):
            default = "true" if default else "false"
        d = pschema.get("description", "")
        extras = []
        if pschema.get("x-architecture-default"):
            extras.append("(default from model_architectures.json)")
        if extras:
            d = (d + " " + " ".join(extras)).strip()
        out.append(f"| `{pname}` | {type_str(pschema)} | {req} | {esc(default) if default != '' else ''} | {esc(d)} |")
    out.append("")
    return out


def render_operation(method: str, path: str, op: dict[str, Any]) -> list[str]:
    op_id = op.get("operationId") or f"{method}-{path}"
    summary = op.get("summary", "")
    out: list[str] = [f"### `{method.upper()} {path}` <a id=\"op-{op_id.lower()}\"></a>", ""]
    if summary:
        out += [f"**{esc(summary)}**", ""]
    desc = op.get("description")
    if desc:
        out += [esc(desc), ""]
    tags = op.get("tags") or []
    if tags:
        out += [f"Tags: {', '.join(f'`{t}`' for t in tags)}", ""]
    params = op.get("parameters") or []
    if params:
        out += ["**Parameters**", "",
                "| in | name | type | required | description |",
                "|---|---|---|---|---|"]
        for p in params:
            out.append(f"| {p.get('in','')} | `{p.get('name','')}` | {type_str(p.get('schema',{}))} | {'yes' if p.get('required') else ''} | {esc(p.get('description',''))} |")
        out.append("")
    body = op.get("requestBody")
    if body:
        content = body.get("content", {})
        for mtype, mspec in content.items():
            schema = mspec.get("schema", {})
            out += [f"**Request body** (`{mtype}`): {type_str(schema)}", ""]
    responses = op.get("responses") or {}
    if responses:
        out += ["**Responses**", "",
                "| status | description | body |",
                "|---|---|---|"]
        for status, r in responses.items():
            body_type = ""
            for mtype, mspec in (r.get("content") or {}).items():
                body_type = f"`{mtype}` -> {type_str(mspec.get('schema',{}))}"
                break
            out.append(f"| `{status}` | {esc(r.get('description',''))} | {body_type} |")
        out.append("")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8077/openapi.json")
    ap.add_argument("--out", default="docs/API_REFERENCE.md")
    args = ap.parse_args()

    try:
        spec = load_spec(args.url)
    except Exception as e:
        print(f"failed to load {args.url}: {e}", file=sys.stderr)
        return 1

    info = spec.get("info", {})
    title = info.get("title", "sdcpp-restapi")
    version = info.get("version", "")
    lines: list[str] = [
        f"# {title} - API Reference",
        "",
        f"> **Auto-generated** from `/openapi.json` by `scripts/generate-api-docs.py`. Do not edit by hand.",
        f"> Version: `{version}`",
        "",
        "This document is the canonical field-level reference. Hand-written guides",
        "(`API.md`, `LLM_GUIDE.md`, `MCP.md`) explain concepts and workflows and",
        "link here for the current field list.",
        "",
        "## Endpoints", "",
    ]

    grouped: dict[str, list[tuple[str, str, dict[str, Any]]]] = {}
    for path, methods in sorted(spec.get("paths", {}).items()):
        for method, op in methods.items():
            if method.lower() not in {"get", "post", "put", "delete", "patch"}:
                continue
            tag = (op.get("tags") or ["Other"])[0]
            grouped.setdefault(tag, []).append((method, path, op))

    for tag in sorted(grouped):
        lines += [f"### {tag}", ""]
        for method, path, op in grouped[tag]:
            op_id = op.get("operationId") or f"{method}-{path}"
            summary = op.get("summary", "")
            lines.append(f"- [`{method.upper()} {path}`](#op-{op_id.lower()}){' - ' + esc(summary) if summary else ''}")
        lines.append("")

    lines += ["## Endpoint details", ""]
    for tag in sorted(grouped):
        lines += [f"#### {tag}", ""]
        for method, path, op in grouped[tag]:
            lines += render_operation(method, path, op)

    lines += ["## Schemas", ""]
    schemas = spec.get("components", {}).get("schemas", {})
    for name in sorted(schemas):
        lines += render_schema(name, schemas[name])

    Path(args.out).write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out} ({len(lines)} lines, {len(spec.get('paths', {}))} paths, {len(schemas)} schemas)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
