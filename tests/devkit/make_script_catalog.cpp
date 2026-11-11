/*
 * Write source/DevKit/Core/ScriptCatalogData.inc, the docs of the built-ins
 * that the script editor shows, from the help of the Dev Kit: for each
 * function, the Syntax, Purpose and Return value of its page; for each
 * built-in constant, the heading and the sentence above it on the page of
 * the constants.
 *
 *   make_script_catalog <doc/SLUDGEDevKitHelp> <output file>
 *
 * The file is in the repository, so that no build has to run this; the test
 * script_catalog_current checks that it is what the help says now. After a
 * change of the help, run this to write it again.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

static std::string readFile (const std::string & path) {
	std::string text;
	FILE * fp = fopen (path.c_str (), "rb");
	if (! fp) return text;
	char buffer[16384];
	size_t n;
	while ((n = fread (buffer, 1, sizeof (buffer), fp)) > 0) text.append (buffer, n);
	fclose (fp);
	// Without the \r of a checkout on Windows.
	text.erase (std::remove (text.begin (), text.end (), '\r'), text.end ());
	return text;
}

static std::string lower (std::string text) {
	for (size_t i = 0; i < text.size (); i ++) {
		if (text[i] >= 'A' && text[i] <= 'Z') text[i] = (char) (text[i] - 'A' + 'a');
	}
	return text;
}

// Where a piece of text starts, in any case, from a place; or npos.
static size_t findNoCase (const std::string & text, const std::string & wanted, size_t from = 0) {
	return lower (text).find (lower (wanted), from);
}

// The HTML between a heading such as "Syntax:" and the next heading.
static std::string section (const std::string & page, const std::string & heading) {
	size_t at = findNoCase (page, "<h3>" + heading + "</h3>");
	if (at == std::string::npos) return std::string ();
	at = findNoCase (page, "</h3>", at) + 5;
	size_t end = findNoCase (page, "<h3>", at);
	return page.substr (at, end == std::string::npos ? std::string::npos : end - at);
}

static void appendUTF8 (std::string & out, unsigned long c) {
	if (c < 0x80) out += (char) c;
	else if (c < 0x800) {
		out += (char) (0xC0 | (c >> 6));
		out += (char) (0x80 | (c & 0x3F));
	} else {
		out += (char) (0xE0 | (c >> 12));
		out += (char) (0x80 | ((c >> 6) & 0x3F));
		out += (char) (0x80 | (c & 0x3F));
	}
}

// The text of some HTML: without its tags and entities, and with one space
// for each run of white space.
static std::string plainText (const std::string & html) {
	std::string text;
	for (size_t i = 0; i < html.size (); i ++) {
		char c = html[i];
		if (c == '<') {
			size_t end = html.find ('>', i);
			if (end == std::string::npos) break;
			std::string tag = lower (html.substr (i + 1, end - i - 1));
			if (tag == "br" || tag == "br/" || tag == "br /") text += ' ';
			i = end;
		} else if (c == '&') {
			size_t end = html.find (';', i);
			std::string name = end == std::string::npos ? std::string () : html.substr (i + 1, end - i - 1);
			if (name == "quot") text += '"';
			else if (name == "amp") text += '&';
			else if (name == "lt") text += '<';
			else if (name == "gt") text += '>';
			else if (name == "nbsp") text += ' ';
			else if (name == "apos") text += '\'';
			else if (! name.empty () && name[0] == '#') appendUTF8 (text, strtoul (name.c_str () + (name[1] == 'x' ? 2 : 1), NULL, name[1] == 'x' ? 16 : 10));
			else {
				text += c;
				continue;
			}
			i = end;
		} else if (c == '\n' || c == '\t' || c == ' ') {
			if (! text.empty () && text[text.size () - 1] != ' ') text += ' ';
		} else {
			text += c;
		}
	}
	size_t start = text.find_first_not_of (' ');
	if (start == std::string::npos) return std::string ();
	return text.substr (start, text.find_last_not_of (' ') - start + 1);
}

// The paragraphs of some HTML, as text.
static std::vector<std::string> paragraphs (const std::string & html) {
	std::vector<std::string> list;
	size_t at = 0;
	while ((at = findNoCase (html, "<p", at)) != std::string::npos) {
		// <p> or <P class=...>, not <pre>.
		char next = at + 2 < html.size () ? html[at + 2] : 0;
		if (next != '>' && next != ' ') {
			at += 2;
			continue;
		}
		size_t start = html.find ('>', at) + 1;
		size_t end = findNoCase (html, "</p>", start);
		std::string text = plainText (html.substr (start, end == std::string::npos ? std::string::npos : end - start));
		if (! text.empty ()) list.push_back (text);
		if (end == std::string::npos) break;
		at = end + 4;
	}
	return list;
}

// A C string literal of text.
static std::string literal (const std::string & text) {
	std::string out = "\"";
	for (size_t i = 0; i < text.size (); i ++) {
		char c = text[i];
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\n') {
			out += "\\n";
		} else {
			out += c;
		}
	}
	return out + "\"";
}

struct FunctionDoc {
	std::string page;
	std::vector<std::string> syntax;	// "name (params)" for each form
	std::string purpose, returns;
};

int main (int argc, char ** argv) {
	if (argc != 3) {
		fprintf (stderr, "usage: make_script_catalog <doc/SLUDGEDevKitHelp> <output file>\n");
		return 2;
	}
	std::string folder = std::string (argv[1]) + "/";
	std::vector<std::string> pages;
	DIR * dir = opendir (argv[1]);
	if (! dir) {
		fprintf (stderr, "Can't read %s\n", argv[1]);
		return 1;
	}
	while (struct dirent * entry = readdir (dir)) {
		std::string name = entry->d_name;
		if (name.size () > 5 && name.compare (name.size () - 5, 5, ".html") == 0) pages.push_back (name);
	}
	closedir (dir);
	std::sort (pages.begin (), pages.end ());

	// The functions, by name; a page may have several, such as
	// deleteFromStack and deleteAllFromStack.
	std::map<std::string, FunctionDoc> functions;
	for (size_t p = 0; p < pages.size (); p ++) {
		std::string page = readFile (folder + pages[p]);
		std::string syntax = section (page, "Syntax:");
		if (syntax.empty ()) continue;
		std::vector<std::pair<std::string, std::string> > forms;		// name, the whole form
		size_t at = 0;
		while ((at = findNoCase (syntax, "<pre", at)) != std::string::npos) {
			size_t start = syntax.find ('>', at) + 1;
			size_t end = findNoCase (syntax, "</pre>", start);
			std::string text = syntax.substr (start, end - start);
			// Each line of it is a form, without "result = " before it.
			size_t line = 0;
			while (line <= text.size ()) {
				size_t lineEnd = text.find ('\n', line);
				if (lineEnd == std::string::npos) lineEnd = text.size ();
				std::string form = plainText (text.substr (line, lineEnd - line));
				line = lineEnd + 1;
				size_t open = form.find ('(');
				if (open == std::string::npos) continue;
				size_t equals = form.find ('=');
				size_t nameStart = equals != std::string::npos && equals < open ? equals + 1 : 0;
				std::string name = form.substr (nameStart, open - nameStart);
				name.erase (std::remove (name.begin (), name.end (), ' '), name.end ());
				if (name.empty ()) continue;
				std::string call = form.substr (nameStart);
				while (! call.empty () && (call[0] == ' ')) call.erase (0, 1);
				while (! call.empty () && (call[call.size () - 1] == ';' || call[call.size () - 1] == ' ')) call.erase (call.size () - 1);
				forms.push_back (std::make_pair (name, call));
			}
			at = end + 6;
		}
		std::vector<std::string> purpose = paragraphs (section (page, "Purpose:"));
		std::vector<std::string> returns = paragraphs (section (page, "Return value:"));
		std::string purposeText, returnText;
		for (size_t i = 0; i < purpose.size (); i ++) purposeText += (i ? "\n\n" : "") + purpose[i];
		for (size_t i = 0; i < returns.size (); i ++) returnText += (i ? "\n\n" : "") + returns[i];
		for (size_t f = 0; f < forms.size (); f ++) {
			FunctionDoc & doc = functions[forms[f].first];
			if (doc.page.empty ()) {
				doc.page = pages[p];
				doc.purpose = purposeText;
				doc.returns = returnText;
			}
			if (doc.page == pages[p]) doc.syntax.push_back (forms[f].second);
		}
	}

	// The constants: "<b>NAME</b> = value" under a heading, after the
	// sentence that says what they are for.
	std::map<std::string, std::pair<std::string, std::string> > constants;	// name: heading, sentence
	{
		std::string page = readFile (folder + "Built-in_Constants.html");
		std::string heading, sentence;
		size_t at = 0;
		while (at < page.size ()) {
			size_t h3 = findNoCase (page, "<h3>", at), p = findNoCase (page, "<p", at);
			if (h3 == std::string::npos && p == std::string::npos) break;
			if (h3 != std::string::npos && (p == std::string::npos || h3 < p)) {
				size_t end = findNoCase (page, "</h3>", h3);
				heading = plainText (page.substr (h3 + 4, end - h3 - 4));
				if (! heading.empty () && heading[heading.size () - 1] == ':') heading.erase (heading.size () - 1);
				sentence.clear ();
				at = end + 5;
				continue;
			}
			char next = p + 2 < page.size () ? page[p + 2] : 0;
			if (next != '>' && next != ' ') {
				at = p + 2;
				continue;
			}
			size_t start = page.find ('>', p) + 1;
			size_t end = findNoCase (page, "</p>", start);
			std::string html = page.substr (start, end - start);
			at = end + 4;
			// A heading in a paragraph, as that of the directions.
			size_t inner = findNoCase (html, "<h3>");
			if (inner != std::string::npos) {
				size_t innerEnd = findNoCase (html, "</h3>", inner);
				heading = plainText (html.substr (inner + 4, innerEnd - inner - 4));
				if (! heading.empty () && heading[heading.size () - 1] == ':') heading.erase (heading.size () - 1);
				sentence.clear ();
				continue;
			}
			size_t bold = findNoCase (html, "<b>");
			std::string text = plainText (html);
			if (bold != std::string::npos && text.find (" = ") != std::string::npos) {
				// One or more: <b>NAME</b> = value, ...
				while (bold != std::string::npos) {
					size_t boldEnd = findNoCase (html, "</b>", bold);
					std::string name = plainText (html.substr (bold + 3, boldEnd - bold - 3));
					if (! name.empty () && ! constants.count (name)) constants[name] = std::make_pair (heading, sentence);
					bold = findNoCase (html, "<b>", boldEnd);
				}
			} else if (! text.empty ()) {
				sentence = text;
			}
		}
	}

	FILE * out = fopen (argv[2], "wb");
	if (! out) {
		fprintf (stderr, "Can't write %s\n", argv[2]);
		return 1;
	}
	fprintf (out, "// The docs of the built-ins, from doc/SLUDGEDevKitHelp, which\n"
		"// tests/devkit/make_script_catalog.cpp wrote: don't change this file,\n"
		"// but the help, and run that again.\n"
		"//\n"
		"// FUNCTION_DOC (name, page, syntax (forms between \\n), purpose, return value)\n"
		"// CONSTANT_DOC (name, heading, what it is for)\n\n");
	for (std::map<std::string, FunctionDoc>::const_iterator f = functions.begin (); f != functions.end (); ++ f) {
		std::string syntax;
		for (size_t i = 0; i < f->second.syntax.size (); i ++) syntax += (i ? "\n" : "") + f->second.syntax[i];
		fprintf (out, "FUNCTION_DOC (%s, %s,\n\t%s,\n\t%s,\n\t%s)\n", literal (f->first).c_str (), literal (f->second.page).c_str (),
			literal (syntax).c_str (), literal (f->second.purpose).c_str (), literal (f->second.returns).c_str ());
	}
	fprintf (out, "\n");
	for (std::map<std::string, std::pair<std::string, std::string> >::const_iterator c = constants.begin (); c != constants.end (); ++ c) {
		fprintf (out, "CONSTANT_DOC (%s, %s, %s)\n", literal (c->first).c_str (), literal (c->second.first).c_str (), literal (c->second.second).c_str ());
	}
	fclose (out);
	printf ("%d functions and %d constants\n", (int) functions.size (), (int) constants.size ());
	return 0;
}
