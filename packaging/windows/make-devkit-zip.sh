#!/bin/sh
# Make the Windows zip of the SLUDGE Dev Kit: the Dev Kit, the compiler and
# the engine, which it runs, with the help and the example projects.
#
# Usage: make-devkit-zip.sh <sludge-devkit.exe> <sludge-compiler.exe> <sludge-engine.exe>
#                           <version> <label> <output folder>
#
# We put the label, for example x64, in the name of the zip. Set STRIP to
# the strip program for the executables, when it is not strip.

set -eu

devkit=$1 compiler=$2 engine=$3 version=$4 label=$5 output=$6
here=$(cd "$(dirname "$0")" && pwd)
stage=$(mktemp -d)
folder="$stage/SLUDGE Dev Kit"

mkdir -p "$folder" "$output"
cp "$devkit" "$folder/sludge-devkit.exe"
cp "$compiler" "$folder/sludge-compiler.exe"
cp "$engine" "$folder/sludge-engine.exe"
${STRIP:-strip} "$folder"/*.exe
cp "$here/../README-DevKit.txt" "$folder/README.txt"
"$here/../copy-devkit-docs.sh" help "$folder/Help"
"$here/../copy-devkit-docs.sh" examples "$folder/Examples"

output=$(cd "$output" && pwd)
(cd "$stage" && zip -q -r -9 "$output/SLUDGE-Dev-Kit-$version-windows-$label.zip" "SLUDGE Dev Kit")
rm -rf "$stage"
ls -la "$output"
