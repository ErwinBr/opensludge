#include <stdio.h>
#include <string.h>

#include "ZBufferDocument.h"
#include "zbuffer.h"

namespace devkit {

DocumentType ZBufferDocument::type () {
	DocumentType type;
	type.name = "Z-Buffer";
	type.extensions.push_back ("zbu");
	type.create = [] () -> Document * { return new ZBufferDocument; };
	return type;
}

ZBufferDocument::ZBufferDocument () {
	memset (&buffers, 0, sizeof (buffers));
}

ZBufferDocument::~ZBufferDocument () {
	forgetSpriteBank (&buffers);
}

std::string ZBufferDocument::untitledName () const {
	if (picture.empty ()) return "Untitled Z-Buffer.zbu";
	std::string name = fileNameOf (picture);
	size_t dot = name.rfind ('.');
	return (dot == std::string::npos ? name : name.substr (0, dot)) + ".zbu";
}

int ZBufferDocument::layerY (int layer) const {
	if (layer < 1 || layer >= buffers.total) return 0;
	return buffers.sprites[layer].special;
}

void ZBufferDocument::setAllY (const std::vector<int> & ys) {
	for (size_t i = 0; i < ys.size () && (int) i + 1 < buffers.total; i ++) {
		buffers.sprites[i + 1].special = ys[i];
	}
}

void ZBufferDocument::setLayerY (int layer, int y, int mergeKey) {
	if (layer < 1 || layer >= buffers.total) return;
	if (y < 0) y = 0;
	if (y > maxY ()) y = maxY ();
	if (y == layerY (layer)) return;
	std::vector<int> before, after;
	for (int i = 1; i < buffers.total; i ++) before.push_back (buffers.sprites[i].special);
	after = before;
	after[layer - 1] = y;
	char label[64];
	snprintf (label, sizeof (label), "Y of Layer %d", layer);
	commands ().run (new ValueCommand<std::vector<int> > (label, before, after,
		[this] (const std::vector<int> & ys) { setAllY (ys); }, mergeKey));
}

int ZBufferDocument::layerAt (int x, int y) const {
	if (buffers.total < 2 || x < 0 || y < 0 || x >= width () || y >= height ()) return 0;
	return buffers.sprites[0].data[y * width () + x];
}

void ZBufferDocument::take (spriteBank & loaded) {
	forgetSpriteBank (&buffers);
	buffers = loaded;
	// A number of its own, so that the views make their textures again.
	spriteBankChanged (&buffers);
}

bool ZBufferDocument::makeFromTGA (const std::string & path) {
	spriteBank loaded;
	memset (&loaded, 0, sizeof (loaded));
	if (! loadZBufferFromTGA (path.c_str (), &loaded)) {
		forgetSpriteBank (&loaded);
		return false;
	}
	take (loaded);
	picture = path;
	commands ().clearAfterChange ();
	return true;
}

bool ZBufferDocument::read (const std::string & path, std::string * error) {
	spriteBank loaded;
	memset (&loaded, 0, sizeof (loaded));
	if (! loadZBufferFile (path.c_str (), &loaded)) {
		forgetSpriteBank (&loaded);
		*error = "The file can't be opened, or it isn't a z-buffer.";
		return false;
	}
	take (loaded);
	picture.clear ();
	return true;
}

bool ZBufferDocument::write (const std::string & path, std::string * error) {
	if (empty ()) {
		*error = "There are no layers yet. Make them from a TGA picture first.";
		return false;
	}
	if (! saveZBufferFile (path.c_str (), &buffers)) {
		*error = "The file can't be written.";
		return false;
	}
	return true;
}

} // namespace devkit
