For macOS and Windows, there are three kinds of downloads. **SLUDGE Dev Kit** is for making games: it manages and compiles projects and edits their floors, z-buffers, sprite banks and translations. **OpenSLUDGE** contains the engine alone, with which you can play any SLUDGE game. **Out Of Order** contains the engine together with Out Of Order, a freeware adventure game made with SLUDGE.

| Download | Platform |
|---|---|
| `SLUDGE-Dev-Kit-…-macOS.zip` | Any Mac with Apple Silicon or Intel, from macOS 10.13 on |
| `SLUDGE-Dev-Kit-…-windows-x64.zip` | 64-bit Windows |
| `OutOfOrder-…-macOS.zip` | Any Mac with Apple Silicon or Intel, from macOS 10.13 on |
| `OutOfOrder-…-windows-x64.zip` | 64-bit Windows |
| `OutOfOrder-…-windows-x86.zip` | 32-bit and 64-bit Windows, from Windows XP on, with OpenGL 2.0 graphics |
| `OpenSLUDGE-…-*.zip` | The same platforms, with the engine alone |
| `sludge-….tar.gz` | The source code of the engine, the Dev Kit and the documentation, to build on Linux (see INSTALL) |

Unzip the download and open the app. If macOS does not open it, Control-click the app and choose Open, or click "Open Anyway" in System Settings, under Privacy & Security. If Windows SmartScreen says that it protected your PC, click "More info" and then "Run anyway".

## What's new

This release brings a new SLUDGE Dev Kit, for Linux, macOS and Windows. The engine is the same as in 2.3.1.

## The Dev Kit

- The Dev Kit is now one program, `sludge-devkit`, instead of five GTK 3 programs, and runs on Linux, macOS and Windows. It uses SDL 3 and Dear ImGui, and draws with the renderers of the engine: Metal, Direct3D 12 or Vulkan, or OpenGL.
- Open a project and its floors, z-buffers, sprite banks and translations open as tabs next to each other. The Project panel shows the files of the project, the Inspector the settings of what you edit, the Build panel the errors and warnings of the compiler, and the Log what happened. The panels dock where you like.
- Everything that you change can be undone and redone, in every editor.
- The compiler runs as a program of its own, so the Dev Kit stays responsive while it compiles and you can cancel it. Double-click an error to open its script at its line in your editor.
- Projects from the old Windows versions of SLUDGE build on every system: the compiler finds files whatever their case, and Project → Check Files converts scripts that aren't UTF-8 and fixes the case of file names.
- The floor editor: draw, move, delete, split lines and split floors as before, with a line to the cursor while you draw and a preview while you move a vertex. A click after a finished polygon starts a new one, without asking. It remembers the backdrop and the colour of each floor, and points on a straight line no longer count as concave.
- The z-buffer editor: drop a TGA picture on it to make the layers, drag to set the y of a layer, and Command-click (Ctrl-click) a layer to select it.
- The sprite bank editor: the sprites as thumbnails, ranges with Shift-click, pictures inserted after the selected sprites, a hotspot for a whole range at once, and Command-click (Ctrl-click) to put a hotspot on a pixel. Sprites are no longer drawn squashed by two rows.
- The translation editor: sort, filter and search the strings, type the translations in the table, and press Enter to go on to the next one.
- The downloads have the compiler and the engine inside, with the help and the example projects.

## Building SLUDGE

- CMake builds the Dev Kit by default on every system (SLUDGE_DEVKIT). The option SLUDGE_DEVKIT_GTK, GTK 3 and libepoxy are gone.
- On Linux, installing the Dev Kit installs one menu entry with the types of the files that it opens, their icons, and the syntax of SLUDGE scripts for GtkSourceView.
- The tests of the Dev Kit edit documents with the mouse and the keys of a script, as a user does, on Linux, macOS and Windows.
