#!/usr/bin/env python3
"""Find catalog files that would be downloaded twice.

Two kinds of duplicate in data/model_architectures.json downloads[]:

  1. The same file (identical SHA-256, read from the Hugging Face response
     headers) listed under different destinations, i.e. a different
     subfolder or file name. Downloading both cards stores it twice.
  2. The same file name (ignoring case and - _ . separators, so
     "Qwen3VL-8B" equals "Qwen3-VL-8B") from different sources that land in
     the same folder. The files are different builds and sit side by side,
     and the second download is not recognised as already present.

Usage:
  scripts/check_catalog_duplicates.py [--file PATH] [--jobs N]

Only single-file Hugging Face entries are checked. Exit code 1 when any
duplicate is found. Needs network access; no token is required (gated files
simply have no hash and are only checked by name).
"""

import argparse
import collections
import json
import os
import re
import sys
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

DEFAULT_FILE = Path(__file__).resolve().parent.parent / "data" / "model_architectures.json"


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    # The hash is in the first response, before the CDN redirect.
    def redirect_request(self, *args, **kwargs):
        return None


_opener = urllib.request.build_opener(_NoRedirect)


def file_sha(entry):
    """SHA-256 (LFS etag) of a Hugging Face file, or None."""
    url = "https://huggingface.co/{}/resolve/{}/{}".format(
        entry["repo_id"], entry.get("revision", "main"), entry["filename"]
    )
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "sdcpp-restapi-dupcheck/1.0"})
    try:
        headers = _opener.open(req, timeout=30).headers
    except urllib.error.HTTPError as e:
        headers = e.headers
    except OSError:
        return None
    return (headers.get("x-linked-etag") or "").strip('"') or None


def destination(entry):
    return (entry["target_type"], entry.get("subfolder") or "", os.path.basename(entry["filename"]))


def normalized(name):
    return re.sub(r"[-_. ]", "", name.lower())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", default=str(DEFAULT_FILE))
    ap.add_argument("--jobs", type=int, default=16)
    args = ap.parse_args()

    arches = json.load(open(args.file))["architectures"]
    entries = []
    for arch, preset in arches.items():
        for e in preset.get("downloads", []):
            if e.get("source") == "huggingface" and e.get("filename") and e.get("bundle") != "directory":
                entries.append((arch, e))

    with ThreadPoolExecutor(args.jobs) as pool:
        shas = list(pool.map(lambda ae: file_sha(ae[1]), entries))

    problems = 0

    by_sha = collections.defaultdict(list)
    for (arch, e), sha in zip(entries, shas):
        if sha:
            by_sha[sha].append((arch, e))
    for items in by_sha.values():
        dests = {destination(e) for _, e in items}
        if len(dests) > 1:
            problems += 1
            print("SAME CONTENT, several destinations:")
            for dest in sorted(dests):
                who = sorted({a for a, e in items if destination(e) == dest})
                print("  {}/{}/{}  <- {}".format(dest[0], dest[1], dest[2], ", ".join(who)))

    by_name = collections.defaultdict(set)
    for _, e in entries:
        t, sub, base = destination(e)
        by_name[(t, sub, normalized(base))].add((e["repo_id"], base))
    for (t, sub, _), sources in by_name.items():
        if len({base for _, base in sources}) > 1:
            problems += 1
            print("SAME NAME, different spelling, in {}/{}:".format(t, sub))
            for repo, base in sorted(sources):
                print("  {} | {}".format(repo, base))

    print("checked {} files, {} duplicate group(s)".format(len(entries), problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
