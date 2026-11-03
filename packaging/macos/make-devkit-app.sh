#!/bin/bash
# Make the macOS application bundle of the SLUDGE Dev Kit, with the compiler
# and the engine, which the Dev Kit runs, next to it in Contents/MacOS.
#
# Usage: make-devkit-app.sh <sludge-devkit> <sludge-compiler> <sludge-engine>
#                           <output .app> <minimum macOS version>
#
# The app opens the files of SLUDGE projects from the Finder: projects,
# floors, z-buffers, sprite banks and translations. Its help is inside it.

set -euo pipefail

devkit=$1 compiler=$2 engine=$3 app=$4 min_macos=$5

here=$(cd "$(dirname "$0")" && pwd)
images="$here/../../images"
# The version in Info.plist must be numbers and dots, so we leave out a
# suffix such as -beta.1.
version=$("$here/../version.sh")
version=${version%%-*}
copyright=${APP_COPYRIGHT:-"SLUDGE Dev Kit © Tim Furnish, Rikard Peterson, Tobias Hansen and contributors"}

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

cp "$devkit" "$app/Contents/MacOS/sludge-devkit"
cp "$compiler" "$app/Contents/MacOS/sludge-compiler"
cp "$engine" "$app/Contents/MacOS/sludge-engine"
chmod 755 "$app"/Contents/MacOS/*
cp "$images/ProjIcon.icns" "$app/Contents/Resources/AppIcon.icns"
# The Dev Kit opens Help/SLUDGE_Help.html in Resources, the folder of the
# program for SDL.
"$here/../copy-devkit-docs.sh" help "$app/Contents/Resources/Help"

sed -e "s|@BUNDLE_ID@|org.opensludge.devkit|g" \
    -e "s|@APP_NAME@|SLUDGE Dev Kit|g" \
    -e "s|@VERSION@|$version|g" \
    -e "s|@MIN_MACOS@|$min_macos|g" \
    -e "s|@COPYRIGHT@|$copyright|g" \
    -e "s|@EXECUTABLE@|sludge-devkit|g" \
    -e "s|@CATEGORY@|public.app-category.developer-tools|g" \
    "$here/Info.plist.in" > "$app/Contents/Info.plist"
printf 'APPL????' > "$app/Contents/PkgInfo"

# The kinds of file that the Dev Kit edits: the extension, the name, the
# type identifier and the icon of each.
plist() { /usr/libexec/PlistBuddy -c "$1" "$app/Contents/Info.plist" > /dev/null; }
plist "Add :UTExportedTypeDeclarations array"
plist "Add :CFBundleDocumentTypes array"
i=0
while read -r extension icon uti name; do
	cp "$images/$icon" "$app/Contents/Resources/$icon"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeIdentifier string $uti"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeDescription string $name"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeIconFile string $icon"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeConformsTo array"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeConformsTo:0 string public.data"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeTagSpecification:public.filename-extension array"
	plist "Add :UTExportedTypeDeclarations:$i:UTTypeTagSpecification:public.filename-extension:0 string $extension"
	plist "Add :CFBundleDocumentTypes:$i:CFBundleTypeName string $name"
	plist "Add :CFBundleDocumentTypes:$i:CFBundleTypeRole string Editor"
	plist "Add :CFBundleDocumentTypes:$i:CFBundleTypeIconFile string $icon"
	plist "Add :CFBundleDocumentTypes:$i:LSHandlerRank string Owner"
	plist "Add :CFBundleDocumentTypes:$i:LSItemContentTypes array"
	plist "Add :CFBundleDocumentTypes:$i:LSItemContentTypes:0 string $uti"
	i=$((i + 1))
done <<'TYPES'
slp ProjIcon.icns org.opensludge.project SLUDGE Project
flo floorIcon.icns org.opensludge.floor SLUDGE Floor
zbu zIcon.icns org.opensludge.zbuffer SLUDGE Z-Buffer
duc spriteIcon.icns org.opensludge.spritebank SLUDGE Sprite Bank
tra flags.icns org.opensludge.translation SLUDGE Translation
TYPES

echo "Created $app"
