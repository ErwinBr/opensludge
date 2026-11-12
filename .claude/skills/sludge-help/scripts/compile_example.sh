#!/bin/sh
# Compile the example code of a help page, to see that it is valid SLUDGE.
#
#   compile_example.sh <example.slu> [sludge-compiler] [work folder]
#
# The example is added to a copy of doc/ExampleProjects/Welcome, so it may use
# what any game has. Welcome has a sub init () of its own, so the script
# renames the example's to sub test (). The example needs any object types it
# uses, such as objectType ego ("Ego") {}.
#
# Examples name files that the Welcome project doesn't have, such as
# 'ego/walk.clp' or 'head.duc'. The script puts a stand-in of the same kind
# in their place (a sprite bank, sound, rig or clip of the repository), so
# that the compiler gets past them; it doesn't check what is in them.
#
# The compiler finds mistakes of syntax and unknown names; it doesn't check
# how many parameters a built-in gets, so compare those with
# source/CommonCode/functionparams.h.
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
# The sprite banks that the stand-in rig and clip use.
cp "$root"/tests/devkit/fixtures/*.duc "$work"/
sed 's/sub init (/sub test (/' "$example" > "$work/example.slu"
printf 'example.slu\r\n' >> "$work/Welcome.slp"

fixtures=$root/tests/devkit/fixtures
grep -o "'[^']*'" "$example" | tr -d "'" | sort -u | while read -r file; do
	[ -e "$work/$file" ] && continue
	mkdir -p "$work/$(dirname "$file")"
	case $file in
		*.duc) cp "$root/doc/ExampleProjects/VerbCoin/outside/duck.duc" "$work/$file" ;;
		*.wav|*.ogg|*.mp3|*.mod|*.xm|*.it|*.s3m) cp "$root/doc/ExampleProjects/Welcome/tada.wav" "$work/$file" ;;
		*.rig) cp "$fixtures/rig.rig" "$work/$file" ;;
		*.clp) cp "$fixtures/clip.clp" "$work/$file" ;;
		*) cp "$root/doc/ExampleProjects/Welcome/image.tga" "$work/$file" ;;
	esac
done

"$compiler" "$work/Welcome.slp"
