#!/bin/sh
# Compile the example code of a help page, to see that it is valid SLUDGE.
#
#   compile_example.sh <example.slu> [sludge-compiler] [work folder]
#
# The example is added to a copy of doc/ExampleProjects/Welcome, so it may use
# what any game has, but not define init (Welcome has one): rename it, for
# example to sub test (). It also needs any object types it uses, such as
# objectType ego ("ego") {}. The compiler finds mistakes of syntax and unknown
# names; it doesn't check how many parameters a built-in gets, so compare
# those with source/CommonCode/functionparams.h.
#
# Run it from the repository (or a worktree of it). Exit status 0 when the
# example compiles.

set -e
example=$1
if [ -z "$example" ] || [ ! -f "$example" ]; then
	echo "usage: compile_example.sh <example.slu> [sludge-compiler] [work folder]" >&2
	exit 2
fi
root=$(git rev-parse --show-toplevel)
compiler=$2
if [ -z "$compiler" ]; then
	# The newest compiler of any build folder.
	compiler=$(ls -t "$root"/build/*/source/DevKit/sludge-compiler 2>/dev/null | head -1)
fi
if [ -z "$compiler" ] || [ ! -x "$compiler" ]; then
	echo "No sludge-compiler found: build one, or give its path." >&2
	exit 2
fi
work=${3:-$(mktemp -d "${TMPDIR:-/tmp}/sludge-example.XXXXXX")}
mkdir -p "$work"
cp "$root"/doc/ExampleProjects/Welcome/* "$work"/
cp "$example" "$work/example.slu"
printf 'example.slu\r\n' >> "$work/Welcome.slp"
"$compiler" "$work/Welcome.slp"
