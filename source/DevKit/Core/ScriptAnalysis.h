#ifndef SLUDGE_DEVKIT_SCRIPT_ANALYSIS_H
#define SLUDGE_DEVKIT_SCRIPT_ANALYSIS_H

/*
 * The mistakes of a script that we can see while it is typed, without the
 * compiler: strings with no end, brackets that don't match, names that are
 * nothing (which the compiler reports without a line), calls with the wrong
 * number of arguments, names made twice, and events of objectTypes that
 * aren't there.
 *
 * What we say must be right: a script that compiles has no errors here.
 * So we only speak of the names of the project when the index has all of
 * its scripts, and we leave alone what we don't understand.
 */

#include <string>
#include <vector>

#include "ScriptIndex.h"
#include "TextBuffer.h"

namespace devkit {

struct Diagnostic {
	enum Severity { ERROR, WARNING };
	Severity severity;
	TextPos start, end;
	std::string message;
	Diagnostic () : severity (ERROR) {}
	Diagnostic (Severity s, TextPos a, TextPos b, const std::string & m) : severity (s), start (a), end (b), message (m) {}
};

// The mistakes of a file, in the order of their places. With allNames, the
// index has every script of the file's project, so that a name that it
// doesn't know is nothing.
std::vector<Diagnostic> analyseScript (const TextBuffer & text, const ScriptIndex & index, const std::string & file, bool allNames);

} // namespace devkit

#endif
