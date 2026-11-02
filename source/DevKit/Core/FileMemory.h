#ifndef SLUDGE_DEVKIT_FILE_MEMORY_H
#define SLUDGE_DEVKIT_FILE_MEMORY_H

/*
 * What the Dev Kit remembers of files that isn't in them, such as the
 * backdrop that the user drew a floor on. We keep it in a file in the
 * settings folder, a line for each value: the file, the key and the value,
 * separated by tabs. We remember the files that changed last.
 */

#include <string>
#include <utility>
#include <vector>

namespace devkit {

class FileMemory {
public:
	static const size_t MAX_FILES = 200;

	// An empty file name keeps the values in memory only.
	explicit FileMemory (const std::string & file);

	// The value, or empty when we have none.
	std::string get (const std::string & path, const std::string & key) const;
	// Remember a value, and write the file. An empty value forgets it.
	void set (const std::string & path, const std::string & key, const std::string & value);

private:
	struct Entry {
		std::string path;
		std::vector<std::pair<std::string, std::string> > values;
	};
	void write () const;

	std::string file;
	std::vector<Entry> entries;		// The last changed first
};

} // namespace devkit

#endif
