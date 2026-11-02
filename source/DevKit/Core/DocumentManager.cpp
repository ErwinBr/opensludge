#include <algorithm>

#include "DocumentManager.h"
#include "Log.h"

namespace devkit {

DocumentManager::DocumentManager (Platform & p, RecentFiles & recent)
	: platform (p), recentFiles (recent), activeDocument (NULL) {}

void DocumentManager::registerType (const DocumentType & type) {
	documentTypes.push_back (type);
}

const DocumentType * DocumentManager::typeOf (const std::string & path) const {
	for (size_t i = 0; i < documentTypes.size (); i ++) {
		if (documentTypes[i].opens (path)) return &documentTypes[i];
	}
	return NULL;
}

std::vector<FileFilter> DocumentManager::openFilters () const {
	std::vector<FileFilter> filters;
	FileFilter all;
	all.name = "SLUDGE files";
	for (size_t i = 0; i < documentTypes.size (); i ++) {
		FileFilter f = documentTypes[i].filter ();
		if (! all.pattern.empty ()) all.pattern += ";";
		all.pattern += f.pattern;
		filters.push_back (f);
	}
	if (documentTypes.size () > 1) filters.insert (filters.begin (), all);
	return filters;
}

Document * DocumentManager::find (const std::string & path) const {
	for (size_t i = 0; i < list.size (); i ++) {
		if (list[i]->path () == path) return list[i];
	}
	return NULL;
}

bool DocumentManager::anyModified () const {
	for (size_t i = 0; i < list.size (); i ++) {
		if (list[i]->isModified ()) return true;
	}
	return false;
}

void DocumentManager::add (Document * document) {
	owned.push_back (std::unique_ptr<Document> (document));
	list.push_back (document);
	activeDocument = document;
	if (opened) opened (document);
}

void DocumentManager::remove (Document * document) {
	if (closing) closing (document);
	list.erase (std::remove (list.begin (), list.end (), document), list.end ());
	if (activeDocument == document) activeDocument = list.empty () ? NULL : list.back ();
	for (size_t i = 0; i < owned.size (); i ++) {
		if (owned[i].get () == document) {
			owned.erase (owned.begin () + i);
			break;
		}
	}
}

Document * DocumentManager::open (const std::string & path) {
	Document * document = find (path);
	if (document) {
		activeDocument = document;
		return document;
	}
	const DocumentType * type = typeOf (path);
	if (! type) {
		platform.showMessage (Platform::MESSAGE_ERROR, "Can't open " + fileNameOf (path),
			"The Dev Kit doesn't know this kind of file yet.");
		return NULL;
	}
	document = type->create ();
	std::string error;
	if (! document->load (path, &error)) {
		delete document;
		if (! error.empty ()) platform.showMessage (Platform::MESSAGE_ERROR, "Can't open " + fileNameOf (path), error);
		recentFiles.remove (path);
		return NULL;
	}
	Log::get ().add (LogLine::LOG_INFO, "Opened %s\n", path.c_str ());
	recentFiles.add (path);
	lastFolder = folderOf (path);
	add (document);
	return document;
}

void DocumentManager::openWithDialog () {
	platform.openFileDialog ("Open", openFilters (), lastFolder, true, [this] (const std::vector<std::string> & files) {
		for (size_t i = 0; i < files.size (); i ++) open (files[i]);
	});
}

Document * DocumentManager::create (const DocumentType & type) {
	Document * document = type.create ();
	add (document);
	return document;
}

void DocumentManager::saveIn (Document * document, const std::string & path, Done done) {
	std::string error;
	bool saved = document->save (path, &error);
	if (saved) {
		Log::get ().add (LogLine::LOG_INFO, "Saved %s\n", path.c_str ());
		recentFiles.add (path);
		lastFolder = folderOf (path);
	} else {
		platform.showMessage (Platform::MESSAGE_ERROR, "Can't save " + fileNameOf (path),
			error.empty () ? std::string ("The file can't be written.") : error);
	}
	if (done) done (saved);
}

void DocumentManager::save (Document * document, Done done) {
	if (document->path ().empty ()) saveAs (document, done);
	else saveIn (document, document->path (), done);
}

void DocumentManager::saveAs (Document * document, Done done) {
	const DocumentType * type = typeOf (document->untitledName ());
	std::vector<FileFilter> filters;
	if (type) filters.push_back (type->filter ());
	std::string start = document->path ().empty () ? lastFolder + document->untitledName () : document->path ();
	platform.saveFileDialog ("Save " + document->displayName (), filters, start,
			[this, document, done] (const std::vector<std::string> & files) {
		// The document may have closed while the dialog was open.
		if (files.empty () || std::find (list.begin (), list.end (), document) == list.end ()) {
			if (done) done (false);
			return;
		}
		saveIn (document, files[0], done);
	});
}

void DocumentManager::saveAll (Done done) {
	std::vector<Document *> modified;
	for (size_t i = 0; i < list.size (); i ++) {
		if (list[i]->isModified ()) modified.push_back (list[i]);
	}
	saveFrom (modified, 0, done);
}

// Save one after another, since saving an untitled one asks for a file.
void DocumentManager::saveFrom (std::vector<Document *> documents, size_t index, Done done) {
	if (index == documents.size ()) {
		if (done) done (true);
		return;
	}
	save (documents[index], [this, documents, index, done] (bool saved) {
		if (saved) saveFrom (documents, index + 1, done);
		else if (done) done (false);
	});
}

void DocumentManager::close (Document * document, Done done) {
	if (! document->isModified ()) {
		remove (document);
		if (done) done (true);
		return;
	}
	std::vector<std::string> buttons;
	buttons.push_back ("Save");
	buttons.push_back ("Don't Save");
	buttons.push_back ("Cancel");
	int answer = platform.ask ("Save changes?",
		"Save the changes to " + document->displayName () + " before closing?", buttons);
	if (answer == 1) {
		remove (document);
		if (done) done (true);
	} else if (answer == 0) {
		save (document, [this, document, done] (bool saved) {
			// A save that was cancelled or failed keeps the document open.
			if (saved) remove (document);
			if (done) done (saved);
		});
	} else {
		if (done) done (false);
	}
}

// Close the last one, until none is left or the user keeps one.
void DocumentManager::closeAll (Done done) {
	if (list.empty ()) {
		if (done) done (true);
		return;
	}
	close (list.back (), [this, done] (bool closed) {
		if (closed) closeAll (done);
		else if (done) done (false);
	});
}

} // namespace devkit
