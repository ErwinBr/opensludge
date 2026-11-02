#include <ctype.h>
#include <dirent.h>
#include <stdio.h>

#include <algorithm>

#include "Launch.h"
#include "ProjectCheck.h"
#include "ProjectDocument.h"

namespace devkit {

// The characters of the bytes 0x80 to 0xFF. Where a code page has none, we
// take the control character of the same value, as Windows does.
static const unsigned short windows1252[128] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

static const unsigned short windows1250[128] = {
	0x20AC, 0x0081, 0x201A, 0x0083, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0088, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0098, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

const char * codePageName (CodePage page) {
	return page == CODEPAGE_WINDOWS_1250 ? "Windows-1250 (Central European)" : "Windows-1252 (Western European)";
}

long invalidUTF8Offset (const std::string & text) {
	size_t i = 0, n = text.size ();
	while (i < n) {
		unsigned char c = text[i];
		size_t length = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
		// No lead byte, an overlong lead, or too short.
		if (! length || c == 0xC0 || c == 0xC1 || i + length > n) return (long) i;
		for (size_t k = 1; k < length; k ++) {
			if ((text[i + k] & 0xC0) != 0x80) return (long) i;
		}
		i += length;
	}
	return -1;
}

static void appendUTF8 (std::string * out, unsigned int c) {
	if (c < 0x80) {
		*out += (char) c;
	} else if (c < 0x800) {
		*out += (char) (0xC0 | (c >> 6));
		*out += (char) (0x80 | (c & 0x3F));
	} else {
		*out += (char) (0xE0 | (c >> 12));
		*out += (char) (0x80 | ((c >> 6) & 0x3F));
		*out += (char) (0x80 | (c & 0x3F));
	}
}

std::string toUTF8 (const std::string & text, CodePage page) {
	if (invalidUTF8Offset (text) < 0) return text;
	const unsigned short * table = page == CODEPAGE_WINDOWS_1250 ? windows1250 : windows1252;
	std::string out;
	for (size_t i = 0; i < text.size (); i ++) {
		unsigned char c = text[i];
		if (c < 0x80) out += (char) c;
		else appendUTF8 (&out, table[c - 0x80]);
	}
	return out;
}

static bool sameIgnoringCase (const std::string & a, const char * b) {
	size_t i = 0;
	for (; i < a.size () && b[i]; i ++) {
		if (tolower ((unsigned char) a[i]) != tolower ((unsigned char) b[i])) return false;
	}
	return i == a.size () && ! b[i];
}

// As fileNameOnDisk in the compiler (Common/helpers.cpp), in a folder.
std::string nameOnDisk (const std::string & folder, const std::string & name, bool * found, bool * caseDiffers) {
	*found = true;
	*caseDiffers = false;
	std::string result, at = folder;
	size_t start = 0;
	while (start <= name.size ()) {
		size_t end = name.find_first_of ("/\\", start);
		if (end == std::string::npos) end = name.size ();
		std::string wanted = name.substr (start, end - start);
		std::string part = wanted;
		if (! wanted.empty () && wanted != "." && wanted != ".." && *found) {
			bool exact = false, other = false;
			std::string otherName;
			if (DIR * dir = opendir (at.empty () ? "." : at.c_str ())) {
				while (struct dirent * entry = readdir (dir)) {
					if (wanted == entry->d_name) {
						exact = true;
						break;
					}
					if (! other && sameIgnoringCase (wanted, entry->d_name)) {
						other = true;
						otherName = entry->d_name;
					}
				}
				closedir (dir);
			}
			if (! exact && other) {
				part = otherName;
				*caseDiffers = true;
			} else if (! exact) {
				*found = false;
			}
		}
		if (! wanted.empty ()) {
			if (! result.empty ()) result += "/";
			result += part;
			at = folder + result + "/";
		}
		start = end + 1;
	}
	return result;
}

static bool readFile (const std::string & path, std::string * text) {
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return false;
	char buffer[8192];
	size_t n;
	text->clear ();
	while ((n = fread (buffer, 1, sizeof (buffer), fp)) > 0) text->append (buffer, n);
	fclose (fp);
	return true;
}

static bool writeFile (const std::string & path, const std::string & text) {
	FILE * fp = fopen (path.c_str (), "wb");
	if (! fp) return false;
	bool ok = fwrite (text.data (), 1, text.size (), fp) == text.size ();
	ok = fclose (fp) == 0 && ok;
	return ok;
}

static int lineAt (const std::string & text, size_t offset) {
	return 1 + (int) std::count (text.begin (), text.begin () + std::min (offset, text.size ()), '\n');
}

static std::string lineText (const std::string & text, size_t offset) {
	size_t start = text.rfind ('\n', offset);
	start = start == std::string::npos ? 0 : start + 1;
	size_t end = text.find_first_of ("\r\n", offset);
	if (end == std::string::npos) end = text.size ();
	return text.substr (start, end - start);
}

// The names in single quotes in a script, outside comments and strings,
// with their lines, as the compiler finds them.
static void namesInScript (const std::string & text, std::vector<std::pair<std::string, int> > * names) {
	enum { NORMAL, QUOTE, COMMENT, NAME } mode = NORMAL;
	std::string name;
	int line = 1, nameLine = 1;
	char last = ' ';
	for (size_t i = 0; i < text.size (); i ++) {
		char c = text[i];
		if (c == '\n') line ++;
		switch (mode) {
			case NORMAL:
				if (c == '\'') {
					mode = NAME;
					name.clear ();
					nameLine = line;
				} else if (c == '"') mode = QUOTE;
				else if (c == '#') mode = COMMENT;
				break;
			case COMMENT:
				if (c == '\n') mode = NORMAL;
				break;
			case QUOTE:
				if (c == '"' && last != '\\') mode = NORMAL;
				break;
			case NAME:
				if (c == '\'' && last != '\\') {
					mode = NORMAL;
					names->push_back (std::make_pair (name, nameLine));
				} else {
					name += c;
				}
				break;
		}
		last = c;
	}
}

std::vector<FileProblem> checkProject (const ProjectDocument & project) {
	std::vector<FileProblem> problems;
	std::string folder = project.folder ();
	const std::vector<std::string> & files = project.files ();
	for (size_t f = 0; f < files.size (); f ++) {
		FileProblem p;
		p.file = files[f];
		p.line = 0;
		bool found, caseDiffers;
		std::string onDisk = nameOnDisk (folder, files[f], &found, &caseDiffers);
		if (! found) {
			p.kind = FileProblem::MISSING;
			p.file = fileNameOf (project.path ());
			p.name = files[f];
			p.inProject = true;
			problems.push_back (p);
			continue;
		}
		if (caseDiffers) {
			p.kind = FileProblem::WRONG_CASE;
			p.file = fileNameOf (project.path ());
			p.name = files[f];
			p.onDisk = onDisk;
			p.inProject = true;
			problems.push_back (p);
		}
		FileKind kind = fileKind (onDisk);
		if (kind != FILE_SCRIPT && kind != FILE_TRANSLATION) continue;
		std::string text;
		if (! readFile (folder + onDisk, &text)) continue;
		long bad = invalidUTF8Offset (text);
		if (bad >= 0) {
			FileProblem e;
			e.kind = FileProblem::NOT_UTF8;
			e.file = onDisk;
			e.line = lineAt (text, bad);
			e.sample = lineText (text, bad);
			problems.push_back (e);
		}
		if (extensionOf (onDisk) != "slu") continue;
		std::vector<std::pair<std::string, int> > names;
		namesInScript (text, &names);
		for (size_t n = 0; n < names.size (); n ++) {
			if (names[n].first.empty ()) continue;
			bool nameFound, nameCase;
			std::string nameDisk = nameOnDisk (folder, names[n].first, &nameFound, &nameCase);
			if (nameFound && ! nameCase) continue;
			FileProblem r;
			r.kind = nameFound ? FileProblem::WRONG_CASE : FileProblem::MISSING;
			r.file = onDisk;
			r.line = names[n].second;
			r.name = names[n].first;
			r.onDisk = nameDisk;
			problems.push_back (r);
		}
	}
	return problems;
}

bool convertFileToUTF8 (const std::string & path, CodePage page, std::string * error) {
	std::string text;
	if (! readFile (path, &text)) {
		*error = "The file can't be read.";
		return false;
	}
	if (invalidUTF8Offset (text) < 0) return true;
	if (! writeFile (path + ".orig", text)) {
		*error = "The copy of the old file, " + fileNameOf (path) + ".orig, can't be written.";
		return false;
	}
	if (! writeFile (path, toUTF8 (text, page))) {
		*error = "The file can't be written.";
		return false;
	}
	return true;
}

bool fixNamesInScript (const std::string & path, const std::vector<FileProblem> & problems, std::string * error) {
	std::string text;
	if (! readFile (path, &text)) {
		*error = "The file can't be read.";
		return false;
	}
	std::string fixed = text;
	for (size_t i = 0; i < problems.size (); i ++) {
		if (problems[i].kind != FileProblem::WRONG_CASE) continue;
		// The name in quotes, and in the same way of writing folders.
		std::string from = "'" + problems[i].name + "'";
		std::string onDisk = problems[i].onDisk;
		if (problems[i].name.find ('\\') != std::string::npos) std::replace (onDisk.begin (), onDisk.end (), '/', '\\');
		std::string to = "'" + onDisk + "'";
		size_t at = 0;
		while ((at = fixed.find (from, at)) != std::string::npos) {
			fixed.replace (at, from.size (), to);
			at += to.size ();
		}
	}
	if (fixed == text) return true;
	if (! writeFile (path, fixed)) {
		*error = "The file can't be written.";
		return false;
	}
	return true;
}

} // namespace devkit
