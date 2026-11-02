#ifndef SLUDGE_DEVKIT_RECENT_FILES_H
#define SLUDGE_DEVKIT_RECENT_FILES_H

/*
 * The files that the user opened last, newest first, which we keep in a
 * file in the settings folder, one path on each line.
 */

#include <string>
#include <vector>

namespace devkit {

class RecentFiles {
public:
	static const size_t MAX_FILES = 12;

	// An empty file name keeps the list in memory only.
	explicit RecentFiles (const std::string & file);

	const std::vector<std::string> & files () const { return list; }
	void add (const std::string & path);
	void remove (const std::string & path);
	void clear ();

private:
	void write () const;

	std::string file;
	std::vector<std::string> list;
};

} // namespace devkit

#endif
