#ifndef SLUDGE_DEVKIT_ZBUFFER_DOCUMENT_H
#define SLUDGE_DEVKIT_ZBUFFER_DOCUMENT_H

/*
 * A z-buffer (.zbu): the layers of a room that hide the characters behind
 * them. We make one from a TGA picture, in which each colour is a layer
 * (GraphicalTools/zbuffer.cpp). Each layer has a y: a character whose
 * feet are above it, further away, is behind the layer. The y of the
 * layers is what the user edits, as commands that undo; a new picture
 * can't be undone.
 *
 * The layers are the sprites 1 to layers () of the bank; sprite 0 says
 * which layer each pixel is in.
 */

#include <string>
#include <vector>

#include "Document.h"
#include "sprites.h"

namespace devkit {

class ZBufferDocument : public Document {
public:
	static DocumentType type ();
	ZBufferDocument ();
	~ZBufferDocument ();

	const char * kind () const { return "Z-Buffer"; }
	// A z-buffer made from a picture is named after it.
	std::string untitledName () const;
	std::string startFolder () const { return folderOf (picture); }

	// The bank, for the views to draw.
	const spriteBank & bank () const { return buffers; }
	bool empty () const { return buffers.total < 2; }
	int layers () const { return buffers.total > 1 ? buffers.total - 1 : 0; }
	int width () const { return buffers.total ? buffers.sprites[0].width : 0; }
	int height () const { return buffers.total ? - buffers.sprites[0].height : 0; }

	// The y of a layer, from 1 to layers (), which goes from 0 to maxY ():
	// below the picture too, for characters that walk out at the bottom.
	int layerY (int layer) const;
	int maxY () const { return height () * 2; }
	// Commands with the same mergeKey (other than 0) merge, as in a drag.
	void setLayerY (int layer, int y, int mergeKey = 0);
	// The layer of a pixel, or 0 when it is in none.
	int layerAt (int x, int y) const;

	// Make the layers from the colours of a PNG or TGA image, as the GTK
	// Z-Buffer Maker did. Its errors go to errorBox (interface.h).
	bool makeFromImage (const std::string & path);

protected:
	bool read (const std::string & path, std::string * error);
	bool write (const std::string & path, std::string * error);

private:
	void setAllY (const std::vector<int> & ys);
	// Take a bank that we loaded, instead of ours.
	void take (spriteBank & loaded);

	spriteBank buffers;
	std::string picture;	// The TGA that we made it from
};

} // namespace devkit

#endif
