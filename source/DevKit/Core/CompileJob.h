#ifndef SLUDGE_DEVKIT_COMPILE_JOB_H
#define SLUDGE_DEVKIT_COMPILE_JOB_H

/*
 * Compiling a project: we run sludge-compiler --machine as a program of its
 * own, read what it writes on a thread, and turn it into the state of the
 * build on the main thread, which the Build panel shows. The compiler keeps
 * its global state and its changes of folder to itself, and a crash of the
 * compiler leaves the Dev Kit running.
 */

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Platform.h"

namespace devkit {

struct BuildMessage {
	enum Type { WARNING, ERROR, SYSTEM, INTERNAL, OUTPUT };
	Type type;
	std::string file;		// Relative to the project, or empty
	int line;				// 0 when we don't know
	std::string text;
	std::string fullText;
};

struct CompileOptions {
	bool keepImages;
	bool writeStrings;
	bool verbose;
	CompileOptions () : keepImages (true), writeStrings (false), verbose (true) {}
};

class CompileJob {
public:
	explicit CompileJob (Platform & platform);
	// Kills the compiler if it still runs.
	~CompileJob ();

	// Start the compiler on a project. Returns false, with the reason in a
	// message, when it can't start.
	bool start (const std::string & compiler, const std::string & project, const CompileOptions & options);
	void cancel ();

	// Take in what the compiler wrote since the last time. Returns true when
	// the state changed. Call this on the main thread.
	bool update ();
	// Take in one line of the compiler's output, as update does.
	void handleLine (const std::string & line);

	bool running () const { return started && ! finished; }

	// The state of the build.
	bool started, finished, succeeded, cancelled;
	double progress[2];
	std::string task, file, item;
	int functions, objectTypes, globals, strings, resources;
	std::vector<BuildMessage> messages;
	std::string gameFile;		// When it succeeded
	int errorCount, warningCount;

	// The arguments for a compile, which the tests check.
	static std::vector<std::string> arguments (const std::string & compiler, const std::string & project, const CompileOptions & options);
	// Split a line of the compiler into its fields, as it escaped them.
	static std::vector<std::string> splitFields (const std::string & line);

private:
	void readOutput ();
	void addMessage (BuildMessage::Type type, const std::string & file, int line, const std::string & text, const std::string & fullText);

	Platform & platform;
	Platform::Process * process;
	std::thread reader;

	// What the reader thread passes to the main thread.
	std::mutex mutex;
	std::string pending;
	bool ended;
	int exitCode;
	bool sawDone;
};

} // namespace devkit

#endif
