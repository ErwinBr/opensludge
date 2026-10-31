#!/bin/sh
# Make the movie of the RenderTest project with ffmpeg: two seconds of a
# moving test pattern at 10 frames per second, with a tone, as VP8 and
# Vorbis in WebM, the format of SLUDGE movies. We keep the movie in the
# repository, and this script only to make it again.
#
# Usage: make-movie.sh <folder>

set -e
ffmpeg -y -hide_banner -loglevel error \
	-f lavfi -i "testsrc2=size=320x200:rate=10:duration=2" \
	-f lavfi -i "sine=frequency=440:sample_rate=22050:duration=2" \
	-c:v libvpx -b:v 120k -auto-alt-ref 0 \
	-c:a vorbis -strict experimental -ac 2 \
	-fflags +bitexact -flags:v +bitexact -flags:a +bitexact \
	"$1/movie.webm"
