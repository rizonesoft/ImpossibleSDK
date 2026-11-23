#!/usr/bin/env python3
"""Compare the published site with a build of it, or follow the links that leave it.

    python tools/site/live.py same _site            every file, byte for byte
    python tools/site/live.py links outward.txt     links to GitHub, Impossible OS and others

`same` waits for a fresh publish with --wait seconds. A link fails when its
server says it is not there; a server that does not answer, or refuses robots,
is only reported.
"""
import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROJECT = json.loads((ROOT / "project.json").read_text(encoding="utf-8"))
SITE, REPO = PROJECT["site_url"], PROJECT["repo_url"]
AGENT = {"User-Agent": "site-check/1.0", "Cache-Control": "no-cache"}


def get(url):
    """(status, body); status 0 when there was no answer."""
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=30) as reply:
                return reply.status, reply.read()
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504):
                return error.code, b""
            status = error.code
        except (OSError, ValueError):
            status = 0
        time.sleep(3 * (attempt + 1))
    return status, b""


def differ(folder, base, mark):
    names = sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file())
    names = [n for n in names if n not in ("CNAME", "404.html")]  # GitHub keeps the first, and answers with the second

    def look(name):
        status, body = get(f"{base}/{name.removesuffix('index.html')}?at={mark}")
        return None if status == 200 and body == (folder / name).read_bytes() else f"{name}: {status or 'no answer'}{'' if status != 200 else ', other content'}"
    with ThreadPoolExecutor(8) as pool:
        return [wrong for wrong in pool.map(look, names) if wrong], len(names)


def same(args):
    until = time.monotonic() + args.wait
    while True:
        wrong, count = differ(args.what, args.base, int(time.time()))
        if not wrong or time.monotonic() > until:
            break
        time.sleep(15)
    if wrong:
        print(f"the site is not this build: {len(wrong)} of {count} files\n" + "\n".join(wrong[:40]), file=sys.stderr)
        return 1
    print(f"{count} files are live as built")
    return 0


def links(args):
    urls = [u for u in args.what.read_text(encoding="utf-8").split() if not u.startswith(("mailto:", SITE))]
    skip = ("https://fonts.googleapis.com", "https://fonts.gstatic.com")
    # A page to edit needs a signed-in reader, and this commit may not be pushed yet.
    urls = [u for u in urls if not u.startswith(skip) and "/edit/" not in u and "/commit/" not in u]
    # The build already knows that files of this repository are there; those of others are asked for as plain files.
    own = re.compile(re.escape(REPO) + "/(blob|tree)/main/")
    urls = [re.sub(r"^https://github\.com/([^/]+/[^/]+)/blob/", r"https://raw.githubusercontent.com/\1/", u) for u in urls if not own.match(u)]
    with ThreadPoolExecutor(6) as pool:
        answers = list(zip(urls, pool.map(lambda u: get(u)[0], urls)))
    for url, status in answers:
        if status not in (200, 404, 410):
            print(f"not judged ({status or 'no answer'}): {url}")
    gone = [url for url, status in answers if status in (404, 410)]
    if gone:
        print("not there any more:\n" + "\n".join(gone), file=sys.stderr)
        return 1
    print(f"{len(urls)} links lead somewhere")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("job", choices=["same", "links"])
    parser.add_argument("what", type=Path)
    parser.add_argument("--base", default=SITE)
    parser.add_argument("--wait", type=int, default=0)
    args = parser.parse_args()
    return (same if args.job == "same" else links)(args)


if __name__ == "__main__":
    raise SystemExit(main())
