#ifndef SLUDGE_DEVKIT_DOCUMENT_MANAGER_H
#define SLUDGE_DEVKIT_DOCUMENT_MANAGER_H

/*
 * The documents that are open, and opening, saving and closing them, with
 * the questions that come with it, through the Platform.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Document.h"
#include "RecentFiles.h"

namespace devkit {

class DocumentManager {
public:
	DocumentManager (Platform & platform, RecentFiles & recent);

	void registerType (const DocumentType & type);
	const std::vector<DocumentType> & types () const { return documentTypes; }
	const DocumentType * typeOf (const std::string & path) const;
	// The filters for the open dialog: every type, then each one.
	std::vector<FileFilter> openFilters () const;

	const std::vector<Document *> & documents () const { return list; }
	Document * active () const { return activeDocument; }
	void setActive (Document * document) { activeDocument = document; }
	Document * find (const std::string & path) const;
	bool anyModified () const;

	// Open a file, or make it active if it is open already. Returns the
	// document, or NULL after telling the user why not.
	Document * open (const std::string & path);
	// Ask for files to open, and open them.
	void openWithDialog ();
	// Make a new, untitled document.
	Document * create (const DocumentType & type);

	// Save a document, in its file or, when it has none yet, in one that
	// we ask for. done gets whether it was saved.
	typedef std::function<void (bool)> Done;
	void save (Document * document, Done done = Done ());
	void saveAs (Document * document, Done done = Done ());
	void saveAll (Done done = Done ());

	// Close a document, asking whether to save its changes first. done
	// gets whether it was closed.
	void close (Document * document, Done done = Done ());
	// Close them all, and stop at the first one that the user keeps.
	void closeAll (Done done = Done ());

	// Called when a document opens and before it closes, so that the
	// program can make or forget its views.
	std::function<void (Document *)> opened, closing;

	// The folder where file dialogs start.
	std::string lastFolder;

private:
	void add (Document * document);
	void remove (Document * document);
	void saveIn (Document * document, const std::string & path, Done done);
	void saveFrom (std::vector<Document *> documents, size_t index, Done done);

	Platform & platform;
	RecentFiles & recentFiles;
	std::vector<DocumentType> documentTypes;
	std::vector<Document *> list;
	std::vector<std::unique_ptr<Document> > owned;
	Document * activeDocument;
};

} // namespace devkit

#endif
