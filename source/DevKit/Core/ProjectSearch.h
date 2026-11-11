#ifndef SLUDGE_DEVKIT_PROJECT_SEARCH_H
#define SLUDGE_DEVKIT_PROJECT_SEARCH_H

/*
 * Find in Project: a text in every script of a project, in those that are
 * open as they are typed, and in the others as they are on disk.
 */

#include <functional>
#include <string>
#include <vector>

#include "TextBuffer.h"
#include "TextEditing.h"

namespace devkit {

struct SearchHit {
	std::string file;		// The full path
	TextPos at;
	int length;
	std::string text;		// Of the line
	SearchHit () : length (0) {}
};

// The text of an open script, or NULL to read it from disk.
typedef std::function<const TextBuffer * (const std::string & path)> OpenText;

// The places of a text in files, in their order, at most limit of them;
// *more says whether there were more. Files that can't be read go into
// *unread.
std::vector<SearchHit> searchFiles (const std::vector<std::string> & files, OpenText openText, const std::string & wanted,
	const editing::FindOptions & options, size_t limit, bool * more, std::vector<std::string> * unread);

} // namespace devkit

#endif
