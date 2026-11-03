#!/bin/sh
# Copy the help of the Dev Kit, or its example projects, into a folder of a
# package, as CMake installs them with -DSLUDGE_DOC=ON (doc/CMakeLists.txt).
#
# Usage: copy-devkit-docs.sh help|examples <folder>
#
# The Dev Kit opens Help/SLUDGE_Help.html next to it (on macOS, in the
# Resources of the app). We leave out the help index of the help viewer of
# Mac OS X, and the .mod music of the examples, which play the .xm versions.

set -eu

what=$1 folder=$2
doc=$(cd "$(dirname "$0")/../doc" && pwd)

mkdir -p "$folder"
case "$what" in
	help)
		cp "$doc/SLUDGE_Help.html" "$doc/Credits.html" "$folder/"
		cp -R "$doc/SLUDGEDevKitHelp" "$folder/"
		find "$folder" -name '*.helpindex' -delete
		;;
	examples)
		cp -R "$doc/ExampleProjects/." "$folder/"
		find "$folder" -iname '*.mod' -delete
		;;
	*)
		echo "usage: copy-devkit-docs.sh help|examples <folder>" >&2
		exit 1
		;;
esac
