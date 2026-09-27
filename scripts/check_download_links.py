#!/usr/bin/env python3
"""Check that every download link in data/model_architectures.json is alive.

Builds the same URLs the server's DownloadManager uses:
  huggingface file    -> HEAD https://huggingface.co/<repo>/resolve/<revision>/<filename>
  huggingface bundle  -> GET  https://huggingface.co/api/models/<repo>/tree/<revision>
  url                 -> HEAD <url>
  civitai             -> GET  https://civitai.com/api/v1/models/<id> (or model-versions/<id>)

Usage:
  scripts/check_download_links.py [--file PATH] [--arch NAME ...] [--jobs N] [--verbose]

Tokens (optional) are read from the environment: HF_TOKEN, CIVITAI_API_KEY.
Use --anonymous to ignore them and see what a user without a token gets.
Gated/auth-required links (401/403) are reported separately and do not fail
the run unless --strict is given. Exit code 1 when any link is dead.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

USER_AGENT = "sdcpp-restapi-linkcheck/1.0"
ANONYMOUS = False
DEFAULT_FILE = Path(__file__).resolve().parent.parent / "data" / "model_architectures.json"


def build_request(entry):
    """Return (method, url, headers) for a catalog entry, or raise ValueError."""
    source = entry.get("source", "huggingface")
    headers = {"User-Agent": USER_AGENT}
    revision = entry.get("revision", "main")

    if source == "huggingface":
        repo = entry.get("repo_id")
        if not repo:
            raise ValueError("missing repo_id")
        if os.environ.get("HF_TOKEN") and not ANONYMOUS:
            headers["Authorization"] = "Bearer " + os.environ["HF_TOKEN"]
        if entry.get("bundle") == "directory":
            return "GET", f"https://huggingface.co/api/models/{repo}/tree/{revision}", headers
        filename = entry.get("filename")
        if not filename:
            raise ValueError("missing filename")
        return "HEAD", f"https://huggingface.co/{repo}/resolve/{revision}/{filename}", headers

    if source == "url":
        url = entry.get("url")
        if not url:
            raise ValueError("missing url")
        return "HEAD", url, headers

    if source == "civitai":
        model_id = str(entry.get("model_id", ""))
        if not model_id:
            raise ValueError("missing model_id")
        if os.environ.get("CIVITAI_API_KEY") and not ANONYMOUS:
            headers["Authorization"] = "Bearer " + os.environ["CIVITAI_API_KEY"]
        if ":" in model_id:
            model_id = model_id.split(":", 1)[0]
        return "GET", f"https://civitai.com/api/v1/models/{model_id}", headers

    raise ValueError(f"unknown source '{source}'")


class NoRedirectAuth(urllib.request.HTTPRedirectHandler):
    """Drop the Authorization header when a redirect leaves the origin host
    (HF resolves to a CDN; forwarding the token there is both wrong and rejected)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlparse(newurl).netloc != urllib.parse.urlparse(req.full_url).netloc:
            new.remove_header("Authorization")
        return new


OPENER = urllib.request.build_opener(NoRedirectAuth)


def probe(method, url, headers, timeout):
    """Return (status_code, size_bytes_or_None, error_text)."""
    req = urllib.request.Request(url, method=method, headers=headers)
    try:
        with OPENER.open(req, timeout=timeout) as resp:
            size = resp.headers.get("X-Linked-Size") or resp.headers.get("Content-Length")
            if method == "GET":
                resp.read(1 << 20)  # enough to confirm the API answered
            return resp.status, int(size) if size and size.isdigit() and method == "HEAD" else None, ""
    except urllib.error.HTTPError as e:
        return e.code, None, e.reason or ""
    except Exception as e:  # DNS, timeout, TLS...
        return 0, None, str(e)


def classify(code):
    if 200 <= code < 300:
        return "OK"
    if code in (401, 403):
        return "GATED"
    return "DEAD"


def fmt_size(n):
    if n is None:
        return "-"
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", default=str(DEFAULT_FILE), help="architectures JSON (default: data/model_architectures.json)")
    ap.add_argument("--arch", action="append", help="only check this architecture (repeatable)")
    ap.add_argument("--jobs", type=int, default=8, help="parallel requests (default 8)")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-request timeout in seconds")
    ap.add_argument("--strict", action="store_true", help="treat gated (401/403) links as failures")
    ap.add_argument("--verbose", "-v", action="store_true", help="also print OK rows")
    ap.add_argument("--anonymous", action="store_true", help="ignore HF_TOKEN / CIVITAI_API_KEY")
    args = ap.parse_args()
    global ANONYMOUS
    ANONYMOUS = args.anonymous

    with open(args.file, encoding="utf-8") as f:
        archs = json.load(f).get("architectures", {})

    jobs = []
    missing = []
    for name, arch in archs.items():
        if args.arch and name not in args.arch:
            continue
        downloads = arch.get("downloads") or []
        if not downloads:
            missing.append(name)
        for entry in downloads:
            jobs.append((name, entry))

    def run(job):
        name, entry = job
        try:
            method, url, headers = build_request(entry)
        except ValueError as e:
            return name, entry, "", "DEAD", 0, None, str(e)
        code, size, err = probe(method, url, headers, args.timeout)
        return name, entry, url, classify(code), code, size, err

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(run, jobs))

    counts = {"OK": 0, "GATED": 0, "DEAD": 0}
    for name, entry, url, verdict, code, size, err in results:
        counts[verdict] += 1
        if verdict == "OK" and not args.verbose:
            continue
        label = f"{name}/{entry.get('id', '?')}"
        detail = f" ({err})" if err and verdict != "OK" else ""
        print(f"{verdict:5} {code:3}  {fmt_size(size):>9}  {label:45}  {url}{detail}")

    print()
    print(f"checked {len(results)} links: {counts['OK']} ok, {counts['GATED']} gated, {counts['DEAD']} dead")
    if missing:
        print(f"{len(missing)} architectures without downloads: {', '.join(missing)}")

    failed = counts["DEAD"] + (counts["GATED"] if args.strict else 0)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
