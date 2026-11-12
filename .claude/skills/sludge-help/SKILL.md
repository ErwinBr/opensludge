---
name: sludge-help
description: Write, update, check and improve the SLUDGE Dev Kit help, the HTML pages in doc/SLUDGEDevKitHelp and doc/SLUDGELinuxHelp, with their contents (.hhc), index (.hhk) and web menu (HtmlMenu.html). Use it for any change to the help - a page for a new built-in function or Dev Kit editor, a page that is out of date after a code change, screenshots, wording, broken links, a page missing from the menu - and whenever a change to the engine's built-ins (functionlist.h) or to a Dev Kit editor means the help should change too, even if the user doesn't say "help" or "docs". It also covers ScriptCatalogData.inc, which the script editor builds from these pages.
---

# The SLUDGE Dev Kit help

The help is about 230 hand-written HTML 4 pages in `doc/SLUDGEDevKitHelp/`. People read it in three ways, so a new page has to be listed in three places, and each of them has its own file:

| How it is read | File that lists the pages |
|---|---|
| Windows HTML Help (CHM) contents | `doc/Table of Contents.hhc` |
| Windows HTML Help index | `doc/SLUDGE Dev Kit Help.hhk` |
| In a browser, from the Dev Kit's Help menu | `doc/SLUDGEDevKitHelp/HtmlMenu.html` (the menu frame of `doc/SLUDGE_Help.html`) |

The script editor uses the help too. `tests/devkit/make_script_catalog.cpp` reads the Syntax, Purpose and Return value of every function page, and the Built-in Constants page, and writes `source/DevKit/Core/ScriptCatalogData.inc`. The editor shows these in its tooltips, completion and Help buttons, and opens pages by their file names (`Workspace::openHelpPage`). That's why the headings of function pages have to stay exactly as they are, and why renaming a page breaks the editor's links.

## Write like the original authors

Take the style from the original pages, by Hungry Software and the later maintainers (trumgottist and Tobias Hansen in the history). Good examples: `Introduction_to_SLUDGE_Concepts.html`, `addOverlay.html`, and the older parts of the pages for the tools. Don't copy the style of pages written or rewritten since November 2026. To see whether a page is still original, run `git log --format='%an %ad' -- <page>`: a page whose history has only trumgottist, tobihan, Tobias Hansen or the first import is original. `references/style.md` has the details and examples. In short:

- **Talk to the reader as "you", in a friendly, relaxed voice, with a little humour.** "Confused? This is how one of the floors of the verb coin example looks:" Use contractions. Don't be stiff, and don't overdo the jokes either: one aside per page is plenty.
- **Say why, not only what.** The original pages explain why a rule exists ("otherwise the compiler will try and turn the name into code and get very confused").
- **Use British spelling** (colour, centre, initialise). Where the engine also accepts the American name, give both: "CENTRE (or CENTER)", "setPasteColour (setPasteColor)".
- **Show it in SLUDGE code.** Examples should be short and complete, and should compile: a `sub` or an `objectType` that someone could paste into a game.
- **Name UI items as the program shows them, in bold:** "choose **New** from the **File** menu". Read the Dev Kit source (`source/DevKit/App/`) for the exact labels rather than guessing them. If a page is about an older tool whose labels have changed, update the label and keep the rest of the text.
- **Headings are `<H3>` and end with a colon** ("Creating a floor:", "Syntax:").

## Kinds of pages

Start from the templates in `references/templates.md`:

- **Function page** (`<name>.html`, one per built-in function, or one for a pair such as `loadCustomData_and_saveCustomData.html`): the headings Syntax:, Purpose:, Return value:, then optionally Example: and See also:. The catalog tool depends on these headings and their order.
- **Tool page** (`About_the_SLUDGE_<Tool>.html`): what the tool is for, then tasks in the order someone does them, with a screenshot.
- **Topic page** (concepts, how-tos): plain prose with headings, linking to the function pages.

Every page has the same header (rule, `images/sludge300.png` logo, `<h2>` title, rule) and the same copyright footer. Copy them from a template. On a new page, end the footer's years with the current year. Leave the footers of other pages alone.

File names: words joined with `_`. In page names that come from titles, a `:` becomes `__` (`Overview__Creating_a_Game.html`), as in the existing pages.

**Pair pages.** Two functions that are each other's opposite or variant, and that someone would look up together, share one page named `a_and_b.html` (`deleteFromStack_and_deleteAllFromStack.html`, `loadCustomData_and_saveCustomData.html`). Give each function its own `<pre class="syntax">` under Syntax:. Unrelated functions get a page each. A pair is listed like this:
- contents and web menu: one entry, named "a and b";
- index: one entry for each function, each with the keywords `a`, `a (built-in function)` and `a and b (built-in function)`;
- alphabetical list: one link for each function.

**Line endings.** Nearly all pages, the `.hhc`, the `.hhk` and the alphabetical list use CRLF line endings, while `HtmlMenu.html` uses LF. Keep each file's own line endings, and write new pages with CRLF. Check with `grep -c $'\r' <file>` after editing, because edit tools can mix the two.

## Adding a page: where it goes

Missing one of these places is the usual mistake:

1. **The page** in `doc/SLUDGEDevKitHelp/`. Put its images in `images/`.
2. **Contents**: add an `<LI> <OBJECT type="text/sitemap">` entry to `doc/Table of Contents.hhc` in the right section. Functions go in alphabetical order under Alphabetical List of Built-in Functions, tools under The Programs, and topics under SLUDGE Language Reference Manual or the topic they belong to. The path uses a backslash: `SLUDGEDevKitHelp\name.html`.
3. **Index**: add an entry to `doc/SLUDGE Dev Kit Help.hhk`, in alphabetical order among the entries around it (the file isn't fully sorted, and HTML Help sorts the index itself). Give it several `Name` keywords: for a function, `name` and `name (built-in function)`. For a tool, its name, the words people might search for (such as "Rig editor") and its file extension (`.RIG file extension`).
4. **Web menu**: add an `<li><a href="name.html" target="Frame">...</a></li>` to `HtmlMenu.html` in the same section and order as in the contents.
5. **For a function**: add it to `Alphabetical_List_of_Built-in_Functions.html`, and to the page of its subject if there is one (such as the stack page for a stack function). Adding to an original page is fine: add a paragraph or section in the page's own voice, and change its existing text only where the new function makes it wrong. The same goes for related function pages that the new function makes incomplete, such as `stackSize` when a new kind of list also works with it.
6. **For a tool**: add it to `The_Programs.html`. If it uses a new file type, add that to `File_Formats_Used_by_SLUDGE.html` too.
7. **Links both ways**: add the new page to the See also: of the pages it links to, where that helps the reader.

Two of the files look like part of this but aren't: the `[FILES]` list in `doc/SLUDGE Dev Kit Help.hhp` (HTML Help Workshop finds the pages through the contents, and the list hasn't been kept up to date for years), and `SLUDGEDevKitHelp.helpindex`, the old Mac Help Viewer index. Leave both alone unless the user asks.

`doc/CMakeLists.txt` installs whole folders, so a new page or image needs no change there.

## After changing a function page: the script catalog

If you changed the Syntax, Purpose or Return value of a function page, the Built-in Constants page, or added or renamed a function page, write the catalog again:

```bash
cmake --build build/rel --target make_script_catalog
build/rel/source/DevKit/make_script_catalog doc/SLUDGEDevKitHelp source/DevKit/Core/ScriptCatalogData.inc
```

Then run `ctest --test-dir build/rel -R script_catalog_current`. Never edit `ScriptCatalogData.inc` by hand: the test would fail on the next run. If `build/rel` doesn't exist, use any configured build folder that has the Dev Kit (`ls build`). In a worktree without a build folder, compile the tool on its own, which has no dependencies, and say that ctest wasn't run:

```bash
c++ -std=c++17 tests/devkit/make_script_catalog.cpp -o "$TMPDIR/make_script_catalog"
"$TMPDIR/make_script_catalog" doc/SLUDGEDevKitHelp source/DevKit/Core/ScriptCatalogData.inc
```

The catalog takes every paragraph of Purpose:, and the script editor shows them in a tooltip. So make the first paragraph a short, complete answer to "what does it do?", and put the longer explanation in the next paragraphs, or in a topic page if it's long.

## Check it against the code

The help must match what the code does, so read the code instead of trusting the old text:

- **Built-in functions**: `source/CommonCode/functionlist.h` has the names. Functions renamed `_rem_<name>` were removed, so don't document them as current. `source/CommonCode/functionparams.h` has how many parameters each one takes, and `source/Engine/builtin.cpp` has what it does and returns.
- **Dev Kit editors**: `source/DevKit/App/` (menus, buttons, dialogs) and `source/DevKit/Core/`.
- **The Linux pages** (`doc/SLUDGELinuxHelp/`, five pages, linked from the contents as `..\SLUDGELinuxHelp\...`) cover building and running on Linux. When a change affects building, running or the Dev Kit on Linux, check them too.

## Check examples by compiling them

Put the example in a `.slu` file and compile it:

```bash
.claude/skills/sludge-help/scripts/compile_example.sh <example.slu>
```

The script adds the file to a copy of the Welcome example project and runs the newest `sludge-compiler` it finds under `build/`. Welcome already has `sub init ()`, so rename that sub in the test file (for example to `sub test ()`), and declare the object types the example uses (`objectType ego ("ego") {}`). The page itself keeps the example as the reader should see it. A warning that the sub isn't used is fine. The compiler finds syntax mistakes and unknown names, but not a wrong number of parameters: compare those with `functionparams.h`.

## Run the check before you finish

```bash
python3 .claude/skills/sludge-help/scripts/check_help.py
```

It reports functions without a page, function pages missing from the alphabetical list, pages missing from the contents, index or web menu, entries that point at missing pages, and broken links and images. The help already had gaps, so the script also checks the committed help (HEAD). It lists the problems HEAD has too as "Older problems", and only new ones make it fail. Fix every new problem. Tell the user about older ones, and fix them only if they ask. With `--all`, every problem counts: use it when the user asks you to tidy up the help as a whole.

The skill's scripts are in the main checkout. If you work in a worktree that doesn't have them, run them by their full path from the main checkout. Run them from inside the worktree, because they check the git checkout they're run in.

## Screenshots

Dev Kit screenshots are `images/devkit-<tool>.png`. Take them with the new Dev Kit in its fixed 1280x800 window: `sludge-devkit --smoke=<png> --smoke-input=<steps> <document>` (see `source/DevKit/App/SmokeInput.h` for the steps). Give each one an `alt` text. The user's ego sprites (the man in the suit) may be used for screenshots, from a project outside the repository. Commit only the PNG, never the sprites or a rig or clip made from them.

## Before you finish

Open the changed pages in a browser (the built-in browser pane works with `file://` paths), through `doc/SLUDGE_Help.html` so that you see the menu too. Then tell the user which pages you added or changed, which listings you updated, whether the catalog was written again, and what the check script still reports.
