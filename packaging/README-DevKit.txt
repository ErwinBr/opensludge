SLUDGE Dev Kit
==============

With the SLUDGE Dev Kit you make SLUDGE adventure games. It manages and
compiles projects, runs them in the engine, and edits floors, z-buffers,
sprite banks and translations. Open a project (.slp), for example one of
the examples in the Examples folder, or one of those files, to start.

The Dev Kit has the compiler and the engine with it, and uses them when
you compile and run a game. It edits the scripts of a project in the text
editor of your choice: choose it in Edit > Preferences.

The help, with the scripting language and every built-in function, is in
Help > SLUDGE Dev Kit Help.

Projects from the old Windows versions of SLUDGE work too. When their
scripts aren't UTF-8 text, or name files in another case than they have
on disk, Project > Check Files finds that and fixes it.

We keep the preferences, the recent files and the layout of the window in
the settings folder of the Dev Kit: on Windows, %APPDATA%\OpenSLUDGE\Dev Kit,
and on macOS, ~/Library/Application Support/OpenSLUDGE/Dev Kit.

If Windows SmartScreen says that it protected your PC, click "More info",
then "Run anyway". If macOS says that the app can't be opened, Control-click
it, choose Open, then choose Open again, or click "Open Anyway" in System
Settings > Privacy & Security.

SLUDGE is open source, under the GNU LGPL 2.1, and the Dev Kit uses Dear
ImGui (MIT licence) and SDL 3 (zlib licence):
https://github.com/opensludge/opensludge
