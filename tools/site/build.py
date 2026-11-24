#!/usr/bin/env python3
"""Build sdk.impossibleos.co and check that it still says what the code does.

    python tools/site/build.py --check        builds in memory and fails on any drift
    python tools/site/build.py --out _site    the same, and writes the site
    python tools/site/build.py --sync         writes the generated parts of README.md

Run tools/build.py first: sizes and pictures come from the programs it built.
--prove also runs the tests that the page of what works names, and refuses one
that did not run. --no-run is for a machine that cannot start Windows programs.
"""
import argparse
import html
import json
import os
import posixpath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from html.parser import HTMLParser
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / "tools/vendor"), str(ROOT / "tools"), str(HERE)]
import bootstrap  # noqa: E402
import headers  # noqa: E402
from markdown_it import MarkdownIt  # noqa: E402

FIRST = ["base", "text", "file", "window", "theme", "draw", "button", "image"]  # the order to read them in; any other header follows
COMMANDS = ["bootstrap", "build", "check_imports", "check_parts", "new_app", "app_build", "prove", "contribute"]
GROUPS = ["Start here", "Guide", "Reference", "Impossible UI", "Project"]
DIRECTIVE = re.compile(r"<!-- page: (.*?) -->\n")


class Drift(Exception):
    pass


def clean(text):
    return html.escape(text, quote=False)


class Marks(HTMLParser):
    """The names and the links of a page, read as a browser reads them."""

    def __init__(self, text):
        super().__init__()
        self.ids, self.hrefs = [], []
        self.feed(text)

    def handle_starttag(self, tag, attributes):
        for name, value in attributes:
            if name == "id":
                self.ids.append(value or "")
            elif name in ("href", "src"):
                self.hrefs.append(value or "")


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", re.sub(r"<[^>]+>", "", text).lower()).strip("-")


# Colours in code: one pass, so nothing is coloured twice.
WORDS = (r"return|if|else|for|while|switch|case|break|static|const|extern|inline|typedef|struct|enum|bool|int|void|char|float|double|"
         r"size_t|u?int(?:8|16|32|64)_t|wchar_t|long|short|unsigned|true|false|NULL|project|include|LANGUAGES")
TOKENS = re.compile(r"""(?P<c>/\*.*?\*/|//[^\n]*|(?<=\s)\#\s[^\n]*|^\#\s[^\n]*)|(?P<s>L?"(?:\\.|[^"\\\n])*"|<[a-z_/]+\.h>)|(?P<p>^\#\w+)"""
                    r"""|(?P<f>\b(?:im|impossible)_\w+)|(?P<n>\bIM_\w+|\bTIER\b|\bWINDOW\b|\bCONSOLE\b|\bSOURCES\b)|(?P<k>\b(?:""" + WORDS + r""")\b)""",
                    re.S | re.M)


def coloured(code, language):
    if language in ("text", "json", ""):
        return clean(code)
    if language == "sh":
        return re.sub(r"(\s{2,}#.*)$", r'<i class="c">\1</i>', clean(code), flags=re.M)
    out, at = [], 0
    for match in TOKENS.finditer(code):
        out += [clean(code[at:match.start()]), f'<i class="{match.lastgroup}">{clean(match[0])}</i>']
        at = match.end()
    return "".join(out) + clean(code[at:])


def code_block(code, language, name=""):
    return (f'<div class="code"><div class="code-bar"><span>{clean(name)}</span><button class="copy" type="button">Copy</button></div>'
            f'<pre tabindex="0"><code>{coloured(code.rstrip(), language)}</code></pre></div>\n')


def png(width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(b"\0" + row for row in rows), 9)) + chunk(b"IEND", b""))


class Page:
    def __init__(self, name, title, group, order, lead, body, sources, kind="Generated", edit=""):
        self.name, self.title, self.group, self.order, self.lead, self.body = name, title, group, order, lead, body
        self.sources, self.kind, self.edit = sources, kind, edit
        self.heads, self.sections = [], []


class Site:
    def __init__(self, root, built, run=True):
        self.root, self.built, self.run = root, built, run
        self.project = json.loads(self.read("project.json"))
        self.tiers = json.loads(self.read("abi/tiers.json"))["tiers"]
        self.status = json.loads(self.read("site/status.json"))
        self.files, self.pages, self.faults, self.fences = {}, [], [], []
        self.sizes, self.outward = {}, set()
        if len(self.project["tiers"]) != len(self.tiers):
            raise Drift("project.json: one line of `tiers` for each tier of abi/tiers.json")
        self.commit = self.git("rev-parse", "HEAD")
        for row in self.status:
            if set(row) not in ({"area", "get", "tests"}, {"area", "get", "todo"}) or not (row.get("tests") or row.get("todo")):
                raise Drift(f"site/status.json: {row.get('area')}: a row has an area, what you get, and either tests or work lists")

    # Reading

    def read(self, path):
        return (self.root / path).read_text(encoding="utf-8")

    def git(self, *command):
        done = subprocess.run(["git", *command], cwd=self.root, capture_output=True, text=True, encoding="utf-8", timeout=120)
        if done.returncode:
            raise Drift(f"git {' '.join(command[:2])}: {done.stderr.strip()}")
        return done.stdout.strip()

    def tool(self, *command):
        env = {**os.environ, "COLUMNS": "100", "NO_COLOR": "1", "PYTHONDONTWRITEBYTECODE": "1", "PYTHONIOENCODING": "utf-8"}
        return subprocess.run([sys.executable, *command], cwd=self.root, capture_output=True, text=True, encoding="utf-8", check=True,
                              timeout=120, env=env).stdout

    def fault(self, text):
        self.faults.append(text)

    # What the code says

    def functions(self, upto=None):
        return sum(len(names) for tier in self.tiers if upto is None or tier["tier"] <= upto for names in tier["functions"].values())

    def plan(self):
        if not hasattr(self, "_plan"):
            self._plan = json.loads(self.tool("scripts/todo-graph.py", "query", "plan"))
        return self._plan

    def state(self, row):
        if "tests" in row:
            return "Works"
        sections = [s for s in self.plan()["sections"] if s["path"] in row["todo"]]
        missing = set(row["todo"]) - {s["path"] for s in sections}
        if missing or not sections:
            raise Drift(f"site/status.json: {row['area']}: no such work list: {', '.join(sorted(missing))}")
        done = sum(s["status"] == "verified" for s in sections)
        return "Works" if done == len(sections) else f"{done} of {len(sections)}" if done else "Planned"

    def repository_link(self, target, where):
        """A path of this repository as a link to GitHub; the path has to be there."""
        path, _, part = target.partition("#")
        path = posixpath.normpath(path)
        if path.startswith("..") or not (self.root / path).exists():
            self.fault(f"{where}: no such file: {target}")
        kind = "tree" if (self.root / path).is_dir() else "blob"
        return f"{self.project['repo_url']}/{kind}/main/{path}" + (f"#{part}" if part else "")

    def inline(self, text, where):
        def link(match):
            target = self.target(html.unescape(match[2]), where, "")
            return f'<a href="{html.escape(target)}">{match[1]}</a>'
        text = re.sub(r"`([^`]+)`", r"<code>\1</code>", html.escape(text))
        return re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)

    # Written pages

    def markdown(self, text, where, folder="docs/guide"):
        out, tokens = [], MarkdownIt("commonmark", {"html": False}).enable("table")
        parsed = tokens.parse(text)
        for index, token in enumerate(parsed):
            if token.type == "code_block":
                self.fault(f"{where}: indented code is not checked, use a fence: {token.content.strip().splitlines()[0]}")
            if token.type == "fence":
                token.type, token.content = "html_block", self.fence(token, where)
                token.tag, token.children = "", None
            elif token.type == "heading_open":
                title = parsed[index + 1].content
                token.attrSet("id", slug(title))
            elif token.type == "table_open":
                token.type, token.content = "html_block", '<div class="tw"><table>\n'
            elif token.type == "table_close":
                token.type, token.content = "html_block", "</table></div>\n"
            elif token.type == "inline":
                for child in token.children:
                    if child.type == "link_open":
                        child.attrSet("href", self.target(child.attrGet("href"), where, folder))
        html_out = tokens.renderer.render(parsed, tokens.options, {})
        out.append(re.sub(r"<blockquote>\s*<p>\[!(NOTE|TIP|IMPORTANT|WARNING)\]\s*", lambda m: f'<blockquote><p><b>{m[1].capitalize()}</b> ', html_out))
        return "".join(out)

    def target(self, href, where, folder):
        own = f"{self.project['repo_url']}/"
        for kind in ("blob/main/", "tree/main/"):
            if href.startswith(own + kind):
                return self.repository_link(href.removeprefix(own + kind), where)
        if ":" in href.split("/")[0] and not href.startswith(("https://", "http://", "mailto:", "site:")):
            self.fault(f"{where}: no link a page may have: {href}")
            return "#"
        if "://" in href or href.startswith(("#", "mailto:")):
            return href
        path, _, part = href.partition("#")
        full = posixpath.normpath(posixpath.join(folder, path))
        if full.startswith("docs/guide/") and full.endswith(".md") and (self.root / full).exists():
            return posixpath.basename(full)[:-3] + ".html" + (f"#{part}" if part else "")
        if href.startswith("site:"):  # a page of this site, such as site:api-file.html
            return href[5:]
        return self.repository_link(full + (f"#{part}" if part else ""), where)

    def fence(self, token, where):
        words = token.info.split()
        language, options = (words[0] if words else ""), dict(word.partition("=")[::2] for word in words[1:])
        code = token.content
        name = options.get("name", {"sh": "Terminal"}.get(language, ""))
        if language in ("c", "cmake"):
            if bad := [v for k in ("program", "project") if k in options and not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", v := options[k])]:
                self.fault(f"{where}: not a name for an example: {bad[0]}")
                return ""
            if "from" in options:
                name = options["from"]
                if name not in getattr(self, "describes", []):
                    self.fault(f"{where}: quotes {name} and does not name it in sources")
                source = self.read(name) if (self.root / name).is_file() else ""
                if code.strip("\n") not in source:
                    self.fault(f"{where}: this part is not in {name}: {code.strip().splitlines()[0]}")
            elif "program" in options or "project" in options:
                self.fences.append((where, language, options, code))
                name = name or ("src/main.c" if language == "c" else "CMakeLists.txt")
            else:
                self.fault(f"{where}: say program=, project= or from= for this code: {code.strip().splitlines()[0]}")
        elif language == "sh":
            for tool, rest in re.findall(r"\b((?:tools|scripts)/[\w/.-]+\.py)([^\n#]*)", code):
                if not (self.root / tool).is_file():
                    self.fault(f"{where}: no such command: {tool}")
                    continue
                source = self.read(tool)
                for option in re.findall(r"(?<!\S)--[a-z-]+", rest):
                    if f'"{option}"' not in source:
                        self.fault(f"{where}: {tool} has no {option}")
        elif language == "json":
            try:
                json.loads(code)
            except ValueError as error:
                self.fault(f"{where}: not JSON: {error}")
        elif language != "text":
            self.fault(f"{where}: code of no kind this page knows: {language or 'none'}")
        return code_block(code, language, name)

    def written(self):
        for path in sorted((self.root / "docs/guide").glob("*.md")):
            where, text = f"docs/guide/{path.name}", path.read_text(encoding="utf-8")
            match = DIRECTIVE.match(text)
            if not match:
                raise Drift(f"{where}: starts with no <!-- page: ... --> line")
            fields = dict(re.findall(r'(\w+)="?((?<=")[^"]*|[^\s"]+)"?', match[1]))
            if set(fields) != {"group", "order", "sources", "reviewed"} or fields["group"] not in GROUPS:
                raise Drift(f"{where}: the first line gives group, order, sources and reviewed, and a known group")
            sources = self.describes = fields["sources"].split(",")
            for source in sources:
                if not (self.root / source).exists():
                    self.fault(f"{where}: describes a file that is not there: {source}")
            rest = text[match.end():].strip()
            title, _, rest = rest.partition("\n")
            lead, _, rest = rest.strip().partition("\n\n")
            if not title.startswith("# ") or lead.startswith("#"):
                raise Drift(f"{where}: starts with a title and one paragraph")
            page = Page(path.stem, title[2:].strip(), fields["group"], int(fields["order"]), self.markdown(lead, where)[3:-5],
                        self.markdown(rest, where), sources, self.freshness(where, sources), where)
            self.pages.append(page)

    def freshness(self, page, sources):
        """Whether the files a page describes changed after the page did."""
        if self.git("rev-parse", "--is-shallow-repository") == "true":
            raise Drift("this copy of the history is cut short: fetch all of it")
        there = self.git("log", "-1", "--format=%H", "--", page)
        if not there or self.git("status", "--porcelain", "--", page):
            return "Up to date"
        changed = [s for s in sources if (self.root / s).exists() and self.git("diff", "--name-only", there, "--", s)]
        return "Review needed" if changed else "Up to date"

    # Generated pages

    def generated(self):
        add = lambda *args, **kw: self.pages.append(Page(*args, **kw))  # noqa: E731
        add("tiers", "Tiers", "Guide", 1, f"Which Windows functions may a program call? The ones in its tier and the tiers before it, {self.functions()} in all.",
            self.tiers_page(), ["abi/tiers.json", "project.json"])
        part = {p["name"]: p for p in bootstrap.components()}
        clang = bootstrap.home(part["llvm-mingw"]) / ("bin/clang.exe" if os.name == "nt" else "bin/clang")
        found = {p.stem: p.relative_to(self.root).as_posix() for folder in ("include", "ui", "parts") for p in sorted((self.root / folder).rglob("*.h"))
                 if "Do not edit." not in p.read_text(encoding="utf-8")[:600]}  # what is generated has its own page
        part_manifest = self.root / "parts.json"
        part_dirs = [p["folder"] for p in json.loads(part_manifest.read_text(encoding="utf-8"))["parts"]] if part_manifest.is_file() else []
        header_includes = ["include", "ui", "parts", *[d for d in part_dirs if d not in ("include", "ui", "parts")]]
        for order, name in enumerate([*FIRST, *sorted(set(found) - set(FIRST))]):
            path = found.get(name)
            if not path:
                raise Drift(f"tools/site/build.py: no header is called {name}.h any more")
            try:
                header = headers.read(clang, self.root, path, header_includes)
            except headers.Undocumented as error:
                self.fault(str(error))
                continue
            title = path.removeprefix("include/")
            body = ""
            tag = re.search(r"Tier \d", header.title)
            for entry in header.entries:
                anchors = "".join(f'<span id="{n}"></span>' for n in entry.names[1:])
                body += (f'<section class="fn"><h2 id="{entry.names[0]}">{" ".join(f"<code>{clean(n)}</code>" for n in entry.names)}</h2>{anchors}'
                         f'{"".join(f"<p>{(f"<code>{n}</code>: " if n else "")}{self.inline(x, path)}</p>" for n, x in entry.text)}{code_block(entry.source, "c", f"{title}, line {entry.line}")}</section>\n')
            lead = re.sub(r"^Impossible (SDK|UI): ", "", header.title)
            lead = lead[0].upper() + lead[1:]
            add(f"api-{name}", title, "Reference", order, clean(lead.replace(tag[0] + ".", "").strip() if tag else lead),
                (f'<p><span class="tag">{tag[0]}</span></p>' if tag else "") + body, [path], kind="Generated")
        add("commands", "Commands", "Reference", 50, "Every tool of the SDK and what it takes, as the tool itself says it.",
            "".join(f'<h2 id="{t.replace("_", "-")}">{t}.py</h2>' + code_block(self.tool(f"tools/{t}.py", "--help"), "text", f"python tools/{t}.py --help")
                    for t in COMMANDS), [f"tools/{t}.py" for t in COMMANDS])
        add("toolchain", "Toolchain", "Reference", 51, "The programs every build uses, pinned by version and hash.", self.toolchain_page(), ["toolchain.json"])
        add("values", "Theme values", "Impossible UI", 20, "Every number WinUI's templates name, in the three themes.", self.values_page(), ["ui/theme_data.c", "tools/winui/pin.json"])
        add("controls", "Controls", "Impossible UI", 30, "Which controls exist, and which are planned.", self.controls_page(), ["todo/05-controls", "todo/01-interface"])
        for order, (path, title) in enumerate([("CONTRIBUTING.md", "Contributing"), ("SECURITY.md", "Security"), ("CHANGELOG.md", "Changelog")]):
            text = self.read(path).split("\n", 1)[1].strip()
            lead, _, rest = text.partition("\n\n")
            rest = re.sub(r"^```$\n(?=python )", "```sh\n", rest, flags=re.M)
            add(slug(title), title, "Project", 20 + order, self.markdown(lead, path, "")[3:-5], self.markdown(rest, path, ""), [path], edit=path)

    def tiers_page(self):
        out = ('<p>A program names its tier in <code>CMakeLists.txt</code>, and the build reads the finished program. A function outside the tier, an import by '
               'ordinal, a delay-loaded DLL, a bound import or thread-local storage fails the build.</p>')
        for tier, adds in zip(self.tiers, self.project["tiers"]):
            out += (f'<h2 id="tier-{tier["tier"]}">Tier {tier["tier"]}: {clean(tier["name"])}</h2><p>{clean(adds)}. {self.functions(tier["tier"])} functions with the '
                    'tiers before it.</p><div class="tw"><table><thead><tr><th>DLL</th><th>Functions</th></tr></thead><tbody>')
            for dll, names in tier["functions"].items():
                out += f'<tr><td><code>{dll}</code></td><td class="fnl">{" ".join(f"<code>{n}</code>" for n in names)}</td></tr>'
            out += "</tbody></table></div>"
        return out

    def toolchain_page(self):
        out, names = "", {"windows": "On Windows", "linux": "In WSL and on Linux"}
        for host, parts in ((k, v) for k, v in json.loads(self.read("toolchain.json")).items() if k in names):
            out += f'<h2 id="{host}">{names[host]}</h2><div class="tw"><table><thead><tr><th>Program</th><th>Version</th><th>SHA-256 of the archive</th></tr></thead><tbody>'
            for part in parts:
                out += f'<tr><td><a href="{part["url"]}">{part["name"]}</a></td><td>{part["version"]}</td><td><code class="hash">{part["sha256"]}</code></td></tr>'
            out += "</tbody></table></div>"
        return out

    def values_page(self):
        source = self.read("ui/theme_data.c")
        table = source[source.index("im_theme_values[3]"):]
        themes = [dict((name, [int(n) for n in numbers.split(",")]) for numbers, name in re.findall(r"\{([-\d, ]+)\}, /\* (\w+) \*/", part))
                  for part in re.split(r"\{ /\* (?:dark|light|contrast) \*/", table)[1:]]
        if len(themes) != 3 or not themes[0] or set(themes[0]) != set(themes[1]):
            raise Drift("ui/theme_data.c: the theme values are not laid out as this page reads them")

        def show(numbers):
            return ", ".join(f"{n / 65536:g}" for n in numbers)
        pin = json.loads(self.read("tools/winui/pin.json"))
        rows = "".join(f"<tr><td><code>{name}</code></td><td>{show(dark)}</td>"
                       + (f"<td>{show(themes[1][name])}</td><td>{show(themes[2].get(name, dark))}</td>") + "</tr>" for name, dark in sorted(themes[0].items()))
        return (f'<p>Read one with <a href="api-theme.html#im_value"><code>im_value</code></a> as <code>IM_VALUE_</code> and the name. Four numbers are the left, top, right and '
                f'bottom of a thickness, or the four corners of a radius. They come from <a href="https://github.com/microsoft/microsoft-ui-xaml/tree/{pin["commit"]}">WinUI at '
                f'<code>{pin["commit"][:8]}</code></a> (MIT).</p><h2 id="values">{len(themes[0])} values</h2><div class="tw"><table><thead><tr><th>Name</th><th>Dark</th><th>Light</th><th>Contrast</th>'
                f"</tr></thead><tbody>{rows}</tbody></table></div>")

    def controls_page(self):
        out = ('<p>A control is here when its section is built, reviewed and stamped. The button of <a href="api-button.html">button.h</a> draws its ground and border today, '
               "and becomes the control of the first section below.</p>")
        files = ["todo/01-interface/TODO-01-first-controls.md", "todo/05-controls/TODO-01-controls.md", "todo/05-controls/TODO-02-more-controls.md", "todo/05-controls/TODO-03-dialogs.md"]
        for path in files:
            title = re.search(r"^# TODO-\d+ -- (.*)$", self.read(path), re.M)[1]
            sections = sorted((s for s in self.plan()["sections"] if s["path"] == path), key=lambda s: int(s["ref"].rsplit("§", 1)[1]))
            out += f'<h2 id="{slug(title)}">{clean(title)}</h2><div class="tw"><table><thead><tr><th>Part</th><th>State</th></tr></thead><tbody>'
            for s in sections:
                number = s["ref"].rsplit("§", 1)[1]
                done = s["status"] == "verified"
                out += (f'<tr><td><a href="{self.project["repo_url"]}/blob/main/{path}#{number}-{slug_github(s["title"])}">{clean(s["title"])}</a></td>'
                        f'<td><span class="st {"works" if done else "planned"}">{"Works" if done else "Planned"}</span></td></tr>')
            out += "</tbody></table></div>"
        return out

    def status_table(self):
        rows = ""
        for row in self.status:
            state = self.state(row)
            kind = "works" if state == "Works" else "planned" if state == "Planned" else "part"
            rows += f'<tr><td>{clean(row["area"])}</td><td><span class="st {kind}">{state}</span></td><td>{self.inline(row["get"], "site/status.json")}</td></tr>'
        return f'<div class="tw"><table><thead><tr><th>Area</th><th>State</th><th>What you get</th></tr></thead><tbody>{rows}</tbody></table></div>'

    def status_page(self):
        plan = self.plan()
        phases = [(plan["phases"][str(n)], [s for s in plan["sections"] if s["phase"] == n]) for n in sorted({s["phase"] for s in plan["sections"]}) if n != 99]
        most = max(len(s) for _, s in phases)
        bars = "".join(f'<li><span>{clean(name)}</span><span class="bar"><i style="width:{len(s) * 100 / most:.1f}%"><s style="width:{sum(x["status"] == "verified" for x in s) * 100 / len(s):.1f}%"></s></i></span>'
                       f'<b>{sum(x["status"] == "verified" for x in s)} / {len(s)}</b></li>' for name, s in phases)
        done = sum(s["status"] == "verified" for s in plan["sections"])
        kinds = {"Generated": "gen", "Up to date": "ok", "Review needed": "warn"}
        fresh = "".join(f'<tr><td><a href="{p.name}.html">{clean(p.title)}</a></td><td>{" ".join(f"<code>{s}</code>" for s in p.sources)}</td>'
                        f'<td><span class="st {kinds[p.kind]}">{p.kind}</span></td></tr>' for p in self.ordered())
        tests = "".join(f'<li>{clean(r["area"])}: {", ".join(f"<code>{t}</code>" for t in r["tests"])}</li>' for r in self.status if "tests" in r)
        return (f'<h2 id="what-can-you-use">What can you use?</h2>{self.status_table()}'
                f'<p>Each row that works names the tests that prove it. They run, and have to pass, before this page is published:</p><ul>{tests}</ul>'
                f'<h2 id="how-much-is-planned">How much is planned?</h2><p>{len(plan["sections"])} sections, of which {done} are built, reviewed and stamped. What works above was built before the '
                f'<a href="{self.repository_link("todo/implementation-plan.md", "status")}">plan</a>.</p><ul class="ph">{bars}</ul>'
                '<h2 id="are-the-pages-still-true">Are the pages still true?</h2><p>A generated page is made from the files it names on every push. A written page names the files it describes: when one of them '
                f'changes and the page does not, it is marked for review here.</p><div class="tw"><table><thead><tr><th>Page</th><th>Made from, or describes</th><th>State</th></tr></thead><tbody>{fresh}</tbody></table></div>')

    def ordered(self):
        return sorted(self.pages, key=lambda p: (GROUPS.index(p.group), p.order, p.title))

    # Examples, sizes and pictures

    def examples(self):
        """Builds every program of the pages with the pinned toolchain, and measures them."""
        import build
        with tempfile.TemporaryDirectory(prefix="impossible-docs-") as folder:
            loose, projects = [], {}
            for where, language, options, code in self.fences:
                if "project" in options:
                    if language in projects.setdefault(options["project"], {}):
                        self.fault(f"{where}: a second {language} block of the project {options['project']}")
                    projects[options["project"]][language] = (where, code)
                else:
                    if options["program"] in [o["program"] for _, o, _ in loose]:
                        self.fault(f"{where}: a second program called {options['program']}")
                    loose.append((where, options, code))
            for name, parts in projects.items():
                if set(parts) != {"c", "cmake"}:
                    self.fault(f"{parts[next(iter(parts))][0]}: the project {name} needs one c and one cmake block")
                    continue
                home = Path(folder, name)
                (home / "src").mkdir(parents=True)
                (home / "src/main.c").write_text(parts["c"][1], encoding="utf-8")
                (home / "CMakeLists.txt").write_text(parts["cmake"][1], encoding="utf-8")
                self.compile(build, home, parts["c"][0])
            if loose:
                home = Path(folder, "loose")
                home.mkdir()
                text = f'cmake_minimum_required(VERSION 4.0)\nproject(Pages LANGUAGES C)\ninclude("{self.root.as_posix()}/cmake/ImpossibleSDK.cmake")\n'
                for where, options, code in loose:
                    (home / f"{options['program']}.c").write_text(code, encoding="utf-8")
                    text += f"impossible_add_program({options['program']} {'WINDOW' if 'window' in options else 'CONSOLE'} TIER {options['tier']} SOURCES {options['program']}.c)\n"
                (home / "CMakeLists.txt").write_text(text, encoding="utf-8")
                self.compile(build, home, ", ".join(sorted({where for where, _, _ in loose})))

    def compile(self, build, home, where):
        out = home / "out"
        keep = os.dup(1)
        log = tempfile.TemporaryFile()
        sys.stdout.flush()
        os.dup2(log.fileno(), 1)
        try:
            build.build(home, out, test=False, definitions={"IMPOSSIBLE_SDK_DIR": self.root.as_posix()})
        except subprocess.CalledProcessError:
            log.seek(0)
            lines = log.read().decode("utf-8", "replace").strip().splitlines()
            self.fault(f"{where}: an example does not build:\n    " + "\n    ".join(lines[-12:]))
        finally:
            os.dup2(keep, 1)
            os.close(keep)
        for program in out.glob("*.exe"):
            self.sizes[program.stem] = program.stat().st_size

    def picture(self):
        """Impossible UI and real WinUI, from the same cases the tests compare."""
        manifest = json.loads(self.read("tests/reference/cases.json"))
        wide, high = 160, 64
        legacy_ids = [
            f"{kind}-{state}-{theme}"
            for theme in ("Dark", "Light")
            for kind in ("button", "accent")
            for state in ("Normal", "PointerOver", "Pressed", "Disabled")
        ]
        by_id = {c.get("id", f"{c.get('control')}-{c.get('state')}-{c.get('theme')}"): c for c in manifest["cases"]}
        cases = [by_id[cid] for cid in legacy_ids if cid in by_id]
        frame = wide * high * 4
        real = [zlib.decompress((self.root / "tests/reference" / f"{c['file']}.z").read_bytes()) for c in cases]
        if self.run:
            accent_keys = ("Accent", "AccentLight1", "AccentLight2", "AccentLight3", "AccentDark1", "AccentDark2", "AccentDark3")
            accent_args = [manifest["accents"][k] for k in accent_keys]
            shots = subprocess.run([str(self.built / "control-shots.exe"), *accent_args], capture_output=True, timeout=60, check=True).stdout
            ours = [shots[i * frame:(i + 1) * frame] for i in range(len(cases))]
        else:
            ours = real
        for name, frames in (("ours", ours), ("winui", real)):
            if len(frames) != 16 or any(len(f) != frame for f in frames):
                raise Drift("the drawn cases are not the sixteen the picture shows")
            rows = []
            for line in range(4):
                for y in range(high):
                    row = b""
                    for f in frames[line * 4:line * 4 + 4]:
                        pixels = f[y * wide * 4:(y + 1) * wide * 4]
                        row += bytes(v for x in range(wide) for v in (pixels[x * 4 + 2], pixels[x * 4 + 1], pixels[x * 4]))
                    rows.append(row)
            self.files[f"img/{name}.png"] = png(wide * 4, high * 4, rows)

    # Putting it together

    def fill(self, name, **values):
        text = self.read(f"site/{name}")
        for key, value in {**{k: html.escape(v) for k, v in self.project.items() if isinstance(v, str)}, "commit": self.commit[:7], "total": str(self.functions()), **values}.items():
            text = text.replace("{{" + key + "}}", value)
        left = re.search(r"\{\{\w+\}\}", text)
        if left:
            raise Drift(f"site/{name}: nothing fills {left[0]}")
        return text

    def document(self, page, side):
        body = page.body
        heads = re.findall(r'<h([23]) id="([^"]+)">(.*?)</h\1>', body, re.S)
        page.heads = heads
        toc = "".join(f'<li><a href="#{i}">{re.sub(r"</?a[^>]*>", "", t)}</a></li>' for level, i, t in heads if level == "2")
        edit = f'<a href="{self.project["repo_url"]}/edit/main/{page.edit}">Edit this page</a>' if page.edit else ""
        names = [f'<a href="{self.repository_link(s, page.name)}"><code>{s}</code></a>' for s in page.sources]
        truth = f'<b class="{"warn" if page.kind == "Review needed" else ""}">{page.kind}</b><span>{"Made from" if page.kind == "Generated" else "Describes"} {", ".join(names)}</span>'
        plain = html.unescape(re.sub(r"<[^>]+>", "", page.lead))
        return self.fill("page.html", title=clean(page.title), group=page.group, lead=page.lead, body=body, side=side, toc=f'<p>On this page</p><ul>{toc}</ul>' if toc else "",
                         truth=truth + edit, description=html.escape(plain[:200]), path=f"docs/{page.name}.html", header=self.header("../", page.name))

    def header(self, base, current=""):
        on = {"first-window": "docs", "tiers": "tiers", "status": "status"}.get(current, "api" if current.startswith("api-") else "docs" if current else "")
        links = [("docs", "docs/", "Docs"), ("api", "docs/api-base.html", "API"), ("tiers", "docs/tiers.html", "Tiers"), ("status", "docs/status.html", "Status")]
        nav = "".join(f'<a href="{base}{href}"{" aria-current=true" if key == on else ""}>{label}</a>' for key, href, label in links)
        return self.fill("header.html", base=base, nav=nav)

    def side(self, current):
        out = ""
        for group in GROUPS:
            out += f"<p>{group}</p><ul>" + "".join(
                f'<li><a href="{p.name}.html"{" aria-current=page" if p is current else ""}>{clean(p.title)}</a></li>' for p in self.ordered() if p.group == group) + "</ul>"
        return out

    def assemble(self, build_examples=True):
        self.written()
        self.generated()
        status = Page("status", "What works today", "Project", 1, "What is built, what is planned, and whether these pages still describe the code.", "", ["site/status.json", "todo"])
        self.pages.append(status)
        status.body = self.status_page()
        home = self.home()
        if build_examples:
            self.examples()
        self.picture()
        sample = (self.built / "hello-window.exe").stat().st_size
        hello = self.sizes.get("Hello", 0)
        if build_examples and not hello and not self.faults:
            raise Drift("the program of the first page was not built")
        home = home.replace("@@HELLO@@", self.size_row(hello, "this program")).replace("@@WINDOW@@", self.size_row(sample, "the window sample"))
        self.files["index.html"] = home
        names = [p.name for p in self.pages]
        if len(set(names)) != len(names):
            raise Drift("two pages have one name")
        for page in self.pages:
            self.files[f"docs/{page.name}.html"] = self.document(page, self.side(page))
        self.files["docs/index.html"] = self.contents()
        self.files["404.html"] = self.fill("404.html", header=self.header("/"))
        self.files["docs/search.json"] = json.dumps(self.index(), ensure_ascii=False, separators=(",", ":"))
        for name in ("style.css", "app.js"):
            self.files[name] = self.read(f"site/{name}")
        for name, source in (("icon.svg", "brand/icon/icon.svg"), ("favicon.ico", "brand/icon.ico"), ("icon.png", "brand/png/icon-256.png"), ("social.png", "brand/social-preview.png"), ("os.svg", "site/os.svg")):
            self.files[name] = (self.root / source).read_bytes()
        self.files["CNAME"] = self.project["site_url"].split("//")[1] + "\n"
        self.files["robots.txt"] = f"User-agent: *\nAllow: /\nSitemap: {self.project['site_url']}/sitemap.xml\n"
        urls = "".join(f"<url><loc>{self.project['site_url']}/{'' if f == 'index.html' else f.removesuffix('index.html')}</loc></url>" for f in sorted(self.files) if f.endswith(".html") and f != "404.html")
        self.files["sitemap.xml"] = f'<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">{urls}</urlset>'
        self.readme()
        self.links()

    def size_row(self, size, what):
        return (f'<div><span class="secs" aria-hidden="true">{"<i></i>" * (-(-size // 512))}</span><span><b>{size:,} bytes</b> {what}</span></div>')

    def home(self):
        quick = self.part("docs/guide/install.md", "sh")
        hello, cmake = self.part("docs/guide/own-program.md", "c"), self.part("docs/guide/own-program.md", "cmake")
        drawers = "".join(f'<button type="button" class="drawer" aria-pressed="{"true" if t["tier"] == 1 else "false"}"><span class="num">{t["tier"]}</span><span class="dn">{clean(t["name"])}<small>{clean(a)}</small></span>'
                          f'<span class="cnt">{sum(map(len, t["functions"].values()))}</span></button>' for t, a in zip(self.tiers, self.project["tiers"]))
        data = json.dumps([{"n": t["name"], "f": t["functions"]} for t in self.tiers], separators=(",", ":"))
        return self.fill("home.html", header=self.header(""), hello=coloured(hello.rstrip(), "c"), cmake=coloured(cmake.rstrip(), "cmake"), quick=code_block(quick, "sh", "Terminal"),
                         drawers=drawers, tiers_json=data.replace("</", "<\\/"), status=self.status_table(), tier_count=str(len(self.tiers)))

    def part(self, path, language):
        match = re.search(rf"^```{language}\b[^\n]*\n(.*?)^```", self.read(path), re.S | re.M)
        if not match:
            raise Drift(f"{path}: the first page of the site shows its first {language} block, and there is none")
        return match[1]

    def contents(self):
        body = ""
        for group in GROUPS:
            body += f'<h2 id="{slug(group)}">{group}</h2><ul class="toc-list">' + "".join(
                f'<li><a href="{p.name}.html">{clean(p.title)}</a><span>{p.lead}</span></li>' for p in self.ordered() if p.group == group) + "</ul>"
        page = Page("index", "Documentation", "Documentation", 0, "How to write, build, test and publish programs with Impossible SDK.", body, ["docs/guide"])
        return self.document(page, self.side(None)).replace("docs/index.html", "docs/")

    def index(self):
        entries = []
        for page in self.ordered():
            entries.append({"t": page.title, "h": "", "u": f"{page.name}.html", "x": html.unescape(re.sub(r"<[^>]+>", "", page.lead))[:160]})
            parts = re.split(r'(<h[23] id="[^"]+">.*?</h[23]>)', page.body, flags=re.S)
            for head, text in zip(parts[1::2], parts[2::2]):
                anchor, title = re.match(r'<h[23] id="([^"]+)">(.*?)</h', head, re.S).groups()
                plain = html.unescape(re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", text))).strip()
                entries.append({"t": page.title, "h": html.unescape(re.sub(r"<[^>]+>", " ", title)).strip(), "u": f"{page.name}.html#{anchor}", "x": plain[:240]})
        return entries

    # What two places say has to agree

    def regions(self):
        mark = {"Works": "✅", "Planned": "📋"}
        tiers = "| Tier | Name | Adds |\n| :-: | --- | --- |\n" + "".join(f"| {t['tier']} | {t['name']} | {a} |\n" for t, a in zip(self.tiers, self.project["tiers"]))
        status = "| Area | Status | What you get |\n| --- | :---: | --- |\n" + "".join(f"| {r['area']} | {mark.get(self.state(r), '🚧')} | {r['get']} |\n" for r in self.status)
        size = {n: -(-(self.built / f"hello-{n}.exe").stat().st_size // 1024) for n in ("console", "window")}
        small = f"- **Small.** The console sample is {size['console']} KB and the window sample {size['window']} KB, with nothing beside them to install.\n"
        block = lambda path, language: f"```{language if language != 'sh' else ''}\n{self.part(path, language)}```\n"  # noqa: E731
        return {"tiers": tiers, "status": status, "small": small, "start": block("docs/guide/install.md", "sh"),
                "hello": block("docs/guide/own-program.md", "c"), "cmake": block("docs/guide/own-program.md", "cmake")}

    def readme(self, write=False):
        text = old = self.read("README.md")
        for name, want in self.regions().items():
            pattern = re.compile(rf"<!-- site:{name} -->\n\n(.*?)\n<!-- /site:{name} -->", re.S)
            match = pattern.search(text)
            if not match:
                raise Drift(f"README.md: no <!-- site:{name} --> part")
            if match[1] != want:
                if not write:
                    self.fault(f"README.md: the {name} part is not what the code says: run python tools/site/build.py --sync")
                text = text[:match.start(1)] + want + text[match.end(1):]
        if self.project["site_url"] not in text:
            self.fault("README.md: does not link to the site")
        if write and text != old:
            temporary = self.root / "README.md.tmp"
            temporary.write_text(text, encoding="utf-8", newline="\n")
            os.replace(temporary, self.root / "README.md")

    def links(self):
        pages = {name: Marks(data) for name, data in self.files.items() if name.endswith(".html")}
        ids = {name: set(page.ids) for name, page in pages.items()}
        own = self.project["site_url"] + "/"
        for name, page in pages.items():
            for twice in sorted({i for i in page.ids if page.ids.count(i) > 1}):
                self.fault(f"{name}: two things are called #{twice}")
            for href in page.hrefs:
                if href == own[:-1] or href.startswith(own):
                    href = "/" + href[len(own):]
                if "://" in href or href.startswith("mailto:"):
                    self.outward.add(href.split("#")[0])
                    continue
                path, _, part = href.partition("#")
                full = path.lstrip("/") if path.startswith("/") else posixpath.normpath(posixpath.join(posixpath.dirname(name), path)) if path else name
                if path.endswith("/") or full in ("", "."):
                    full = posixpath.join("" if full == "." else full, "index.html").lstrip("/")
                if full not in self.files:
                    self.fault(f"{name}: leads nowhere: {href}")
                elif part and full in ids and part not in ids[full]:
                    self.fault(f"{name}: no such part: {href}")

    def prove(self):
        """Runs the tests that the rows that work name; one that is skipped proves nothing."""
        names = [t for row in self.status for t in row.get("tests", [])]
        os.environ["IMPOSSIBLE_BUILD"] = str(self.built)
        sys.path.insert(0, str(self.root))
        result = unittest.TextTestRunner(stream=sys.stderr, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromNames(names))
        if not result.wasSuccessful() or result.skipped or result.testsRun < len(names):
            self.fault("site/status.json: a test that a row names failed or did not run")

    def named_tests(self):
        for row in self.status:
            for name in row.get("tests", []):
                module, case, test = name.rsplit(".", 2)
                source = self.root / (module.replace(".", "/") + ".py")
                if not source.is_file() or not re.search(rf"class {case}\(.*?def {test}\(", source.read_text(encoding="utf-8"), re.S):
                    self.fault(f"site/status.json: no such test: {name}")


def slug_github(title):
    return re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--prove", action="store_true")
    parser.add_argument("--no-run", action="store_true")
    parser.add_argument("--no-examples", action="store_true", help="for the tests of this tool")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--outward", type=Path, help="write the links that leave the site here")
    parser.add_argument("--root", type=Path, default=ROOT, help="for the tests of this tool")
    args = parser.parse_args()
    if not (args.check or args.sync or args.out):
        parser.error("say --check, --sync or --out")
    root = args.root.resolve()
    if args.out and (args.no_run or args.no_examples) and root == ROOT:
        parser.error("--no-run and --no-examples leave parts out, so there is no site to write")
    built = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / bootstrap.host())).resolve()
    if not (built / "hello-window.exe").is_file():
        raise Drift(f"nothing is built in {built}: run python tools/build.py first")
    site = Site(root, built, run=not args.no_run)
    if args.sync:
        site.readme(write=True)
        return 0
    site.assemble(build_examples=not args.no_examples)
    site.named_tests()
    if args.prove:
        site.prove()
    if site.faults:
        raise Drift("\n".join(site.faults))
    if args.out:
        made = args.out / "CNAME"
        if args.out.exists() and any(args.out.iterdir()) and not ((args.out / "docs/search.json").is_file() and made.is_file() and made.read_text(encoding="utf-8") == site.files["CNAME"]):
            raise Drift(f"{args.out} is not empty and holds no site that this made")
        shutil.rmtree(args.out, ignore_errors=True)
        for name, data in site.files.items():
            path = args.out / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
    if args.outward:
        args.outward.write_text("\n".join(sorted(site.outward)) + "\n", encoding="utf-8", newline="\n")
    print(f"{len(site.pages) + 3} pages, {len(site.files)} files, {len(site.fences)} examples: " + ("nothing has drifted" if not (args.no_run or args.no_examples) else "checked in part, without " + " and ".join(
        [*(["starting programs"] if args.no_run else []), *(["building examples"] if args.no_examples else [])])))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Drift as error:
        print(f"site: {error}", file=sys.stderr)
        raise SystemExit(1)
