#ifndef SLUDGE_DEVKIT_DOCUMENT_VIEW_H
#define SLUDGE_DEVKIT_DOCUMENT_VIEW_H

/*
 * The editor of a document: what its tab shows, and what the Inspector
 * shows while it is active. The state is in the document (Core); a view
 * only shows it and turns what the user does into commands.
 */

#include <string>

namespace devkit {

class Document;

class DocumentView {
public:
	virtual ~DocumentView () {}

	virtual Document * document () = 0;
	// The contents of the document's tab.
	virtual void draw () = 0;
	// The contents of the Inspector while the document is active.
	virtual void drawInspector () {}
	// A file that the user dropped on the window while the document was
	// active, such as the backdrop of a floor. Returns whether we took it.
	virtual bool takeFile (const std::string & path) { (void) path; return false; }
	// Whether the view needs another frame soon, such as during a drag.
	virtual bool animating () const { return false; }
	// Edit > Copy and Paste, for the views that have them.
	virtual bool canCopy () const { return false; }
	virtual void copy () {}
	virtual bool canPaste () const { return false; }
	virtual void pasteClipboard () {}
	// More items of the Edit menu, after Paste.
	virtual void drawEditMenu () {}
};

} // namespace devkit

#endif
