#include "ProjectSearch.h"
#include "ScriptDocument.h"

namespace devkit {

std::vector<SearchHit> searchFiles (const std::vector<std::string> & files, OpenText openText, const std::string & wanted,
		const editing::FindOptions & options, size_t limit, bool * more, std::vector<std::string> * unread) {
	std::vector<SearchHit> hits;
	*more = false;
	if (wanted.empty ()) return hits;
	for (size_t f = 0; f < files.size (); f ++) {
		const TextBuffer * text = openText ? openText (files[f]) : NULL;
		TextBuffer fromDisk;
		if (! text) {
			std::string fileText, error;
			if (! ScriptDocument::readScript (files[f], &fileText, &error)) {
				if (unread) unread->push_back (files[f]);
				continue;
			}
			fromDisk.setText (fileText);
			text = &fromDisk;
		}
		std::vector<TextSelection> found = editing::findAll (*text, wanted, options);
		for (size_t i = 0; i < found.size (); i ++) {
			if (hits.size () == limit) {
				*more = true;
				return hits;
			}
			SearchHit hit;
			hit.file = files[f];
			hit.at = found[i].start ();
			hit.length = found[i].end ().column - found[i].start ().column;
			hit.text = text->line (hit.at.line);
			hits.push_back (hit);
		}
	}
	return hits;
}

} // namespace devkit
