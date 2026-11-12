#!/usr/bin/env python3
"""Check the SLUDGE Dev Kit help for the mistakes that are easy to make.

    check_help.py [--all] [repo root]

The repo root defaults to the git checkout (or worktree) of the current
folder. It reports, without changing anything:
  - built-in functions of the engine (source/CommonCode/functionlist.h)
    that no help page documents, and function pages that aren't on the
    Alphabetical List of Built-in Functions;
  - pages that the table of contents (doc/Table of Contents.hhc), the index
    (doc/SLUDGE Dev Kit Help.hhk) or the web menu (HtmlMenu.html) doesn't list;
  - entries in those three that point at pages which don't exist;
  - links and images on the pages that point at nothing.

The help had gaps long before this script. So it runs every check on the
committed help (HEAD) too, and a problem that HEAD already had is listed
under "Older problems" and doesn't count. Only new problems make the exit
status 1. With --all, every problem counts, old or new.
"""

import os
import re
import subprocess
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
check_all = "--all" in sys.argv


def git(*command, cwd=None):
    return subprocess.run(["git", *command], cwd=cwd, capture_output=True, text=True)


root = args[0] if args else (git("rev-parse", "--show-toplevel").stdout.strip() or os.getcwd())

HELP = "doc/SLUDGEDevKitHelp"
FRAME_PAGES = {"HtmlMenu.html"}     # The frame of the web help, not a topic.


class WorkingTree:
    def read(self, path):
        try:
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def exists(self, path):
        return os.path.exists(os.path.join(root, path))

    def pages(self):
        return sorted(p for p in os.listdir(os.path.join(root, HELP)) if p.endswith(".html"))


class Committed:
    """The same files as they are at HEAD."""

    def __init__(self):
        out = git("ls-tree", "-r", "--name-only", "HEAD", "--", "doc", "source/CommonCode", cwd=root).stdout
        self.files = set(out.splitlines())

    def read(self, path):
        if path not in self.files:
            return ""
        return git("show", f"HEAD:{path}", cwd=root).stdout

    def exists(self, path):
        path = os.path.normpath(path)
        return path in self.files or any(f.startswith(path + "/") for f in self.files)

    def pages(self):
        return sorted(os.path.basename(f) for f in self.files
                      if os.path.dirname(f) == HELP and f.endswith(".html"))


def locals_in(text):
    """The pages an .hhc or .hhk points at, as paths from doc/."""
    return {p.replace("\\", "/") for p in re.findall(r'name="Local" value="([^"#]+)', text, re.I)}


def problems_of(src):
    """{title: set of problems} for one version of the help."""
    found = {}

    def add(title, items):
        found.setdefault(title, set()).update(items)

    pages = src.pages()

    # Built-in functions. Those taken out of the engine keep their slot as
    # _rem_name, so that old games still compile; their old pages may stay.
    all_funcs = re.findall(r"^FUNC\s*\(\s*\w+\s*,\s*(\w+)\s*\)",
                           src.read("source/CommonCode/functionlist.h"), re.M)
    removed = {f[len("_rem_"):] for f in all_funcs if f.startswith("_rem_")}
    funcs = [f for f in all_funcs if not f.startswith("_rem_")]

    documented = {}   # function name -> page
    for page in pages:
        m = re.search(r"Syntax:\s*</H3>(.*?)<H3>", src.read(f"{HELP}/{page}"), re.S | re.I)
        if not m:
            continue
        documented.setdefault(page[:-5], page)
        for name in re.findall(r"(\w+)\s*\(", re.sub(r"<[^>]+>", "", m.group(1))):
            documented.setdefault(name, page)

    add("Built-in functions without a help page", [f for f in funcs if f not in documented])
    alpha = set(re.findall(r'href="([^"#]+)', src.read(f"{HELP}/Alphabetical_List_of_Built-in_Functions.html"), re.I))
    add("Function pages missing from Alphabetical_List_of_Built-in_Functions.html",
        [documented[f] for f in funcs if f in documented and documented[f] not in alpha])

    # Contents, index and web menu.
    toc = locals_in(src.read("doc/Table of Contents.hhc"))
    index = locals_in(src.read("doc/SLUDGE Dev Kit Help.hhk"))
    menu = {"SLUDGEDevKitHelp/" + p for p in
            re.findall(r'href="([^"#:]+\.html)', src.read(f"{HELP}/HtmlMenu.html"), re.I)}
    topics = [p for p in pages if p not in FRAME_PAGES and p[:-5] not in removed]
    add("Pages not in the table of contents (doc/Table of Contents.hhc)",
        [p for p in topics if "SLUDGEDevKitHelp/" + p not in toc])
    add("Pages not in the index (doc/SLUDGE Dev Kit Help.hhk)",
        [p for p in topics if "SLUDGEDevKitHelp/" + p not in index])
    add("Pages not in the web menu (SLUDGEDevKitHelp/HtmlMenu.html)",
        [p for p in topics if "SLUDGEDevKitHelp/" + p not in menu and p != "index.html"])
    for name, entries in (("Table of Contents.hhc", toc), ("SLUDGE Dev Kit Help.hhk", index), ("HtmlMenu.html", menu)):
        add("Contents, index or menu entries for pages that don't exist",
            [f"{name}: {p}" for p in entries if not src.exists(os.path.normpath("doc/" + p))])

    # Links and images.
    for page in pages:
        for target in re.findall(r'(?:href|src)="([^"]+)"', src.read(f"{HELP}/{page}"), re.I):
            if re.match(r"[a-z]+:", target, re.I) or target.startswith("#"):
                continue
            path = target.split("#")[0]
            if path and not src.exists(os.path.normpath(f"{HELP}/{path}")):
                add("Links or images that point at nothing", [f"{page} -> {target}"])
    return found


now = problems_of(WorkingTree())
before = {} if check_all else problems_of(Committed())

new_count = 0
older = []
for title, items in now.items():
    fresh = sorted(items - before.get(title, set()))
    old = sorted(items & before.get(title, set()))
    if fresh:
        new_count += len(fresh)
        print(f"\n{title} ({len(fresh)}):")
        for item in fresh:
            print(f"  {item}")
    if old:
        older.append((title, old))

if new_count == 0:
    print("No new problems.")
if older:
    print("\nOlder problems, which HEAD has too (fix them only if asked):")
    for title, items in older:
        print(f"  {title}: {', '.join(items)}")
sys.exit(1 if new_count else 0)
