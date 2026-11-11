#!/bin/sh
# Download the data file of Out Of Order from the Ubuntu archive, check it,
# and unpack the package into a folder. The game is freeware, and anyone may
# give it away together with its readme and licence.
#
# Usage: get-out-of-order.sh <folder>
#
# In the folder, the data file is usr/share/out-of-order/gamedata, the
# manual and licence are in usr/share/doc/out-of-order, and the icon is
# usr/share/pixmaps/out-of-order.xpm.

set -eu

# The package of Ubuntu, from its archive or, when the archive can't be
# reached (as when the DNS of a CI runner fails), from a mirror that has the
# same file. The checksum checks it, from wherever it came. (Debian's
# package is another build, with another checksum.)
package=pool/multiverse/o/out-of-order/out-of-order_1.0-3_all.deb
mirrors="http://archive.ubuntu.com/ubuntu https://mirrors.edge.kernel.org/ubuntu"
sha256=fcf4e47f947d767ce55b4be718b0c10e591fdf1a8bc99e884f7e3e988e757a4f

folder=$1
mkdir -p "$folder"
cd "$folder"
got=
for mirror in $mirrors; do
	if curl -fsSL --retry 3 --retry-delay 5 -o out-of-order.deb "$mirror/$package"; then
		got=yes
		break
	fi
	echo "Can't download Out Of Order from $mirror" >&2
done
if [ -z "$got" ]; then
	echo "Can't download Out Of Order from any mirror" >&2
	exit 1
fi
if command -v sha256sum > /dev/null; then
	echo "$sha256  out-of-order.deb" | sha256sum -c -
else
	echo "$sha256  out-of-order.deb" | shasum -a 256 -c -
fi
# A Debian package is an ar archive, with the files in data.tar.xz.
ar x out-of-order.deb data.tar.xz
tar -xf data.tar.xz
rm out-of-order.deb data.tar.xz
