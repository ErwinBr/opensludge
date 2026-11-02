#ifndef SLUDGE_DEVKIT_DOCUMENT_VIEW_H
#define SLUDGE_DEVKIT_DOCUMENT_VIEW_H

/*
 * The editor of a document: what its tab shows, and what the Inspector
 * shows while it is active. The state is in the document (Core); a view
 * only shows it and turns what the user does into commands.
 */

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
	// Whether the view needs another frame soon, such as during a drag.
	virtual bool animating () const { return false; }
};

} // namespace devkit

#endif
