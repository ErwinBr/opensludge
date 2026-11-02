#ifndef SLUDGE_DEVKIT_LAUNCH_H
#define SLUDGE_DEVKIT_LAUNCH_H

/*
 * The command lines of the programs that open the files of a project, from
 * the preferences, such as "code --goto {file}:{line}".
 */

#include <string>
#include <vector>

namespace devkit {

// Split a command into its arguments at spaces, with "double quotes" around
// arguments that have spaces, and put the file in place of {file} and the
// line in place of {line}. Without {file}, the file is the last argument.
// A line of 0 is line 1.
std::vector<std::string> commandLine (const std::string & command, const std::string & file, int line);

// The kinds of files of a project, by their extension, for what opens them.
enum FileKind { FILE_SCRIPT, FILE_TRANSLATION, FILE_IMAGE, FILE_AUDIO, FILE_MODULE, FILE_DOCUMENT, FILE_OTHER };
FileKind fileKind (const std::string & path);

} // namespace devkit

#endif
