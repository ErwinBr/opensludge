#include <stdio.h>
#include <stdlib.h>

#include "CompileJob.h"
#include "Document.h"
#include "Log.h"

namespace devkit {

CompileJob::CompileJob (Platform & p)
	: started (false), finished (false), succeeded (false), cancelled (false),
	  functions (0), objectTypes (0), globals (0), strings (0), resources (0), errorCount (0), warningCount (0),
	  platform (p), process (NULL), ended (false), exitCode (0), sawDone (false) {
	progress[0] = progress[1] = 0.0;
}

CompileJob::~CompileJob () {
	if (process && ! finished) process->kill ();
	if (reader.joinable ()) reader.join ();
	delete process;
}

std::vector<std::string> CompileJob::arguments (const std::string & compiler, const std::string & project, const CompileOptions & options) {
	std::vector<std::string> args;
	args.push_back (compiler);
	args.push_back ("--machine");
	if (options.keepImages) args.push_back ("--keep-slx");
	if (options.writeStrings) args.push_back ("--string-file");
	if (! options.verbose) args.push_back ("--strip-debug-info");
	args.push_back (project);
	return args;
}

bool CompileJob::start (const std::string & compiler, const std::string & project, const CompileOptions & options) {
	started = true;
	Log::get ().add (LogLine::LOG_INFO, "Compiling %s\n", project.c_str ());
	std::string error;
	process = platform.runProgram (arguments (compiler, project, options), folderOf (project), &error);
	if (! process) {
		addMessage (BuildMessage::SYSTEM, "", 0, "Can't start the compiler " + compiler + ": " + error, "");
		finished = true;
		return false;
	}
	reader = std::thread (&CompileJob::readOutput, this);
	return true;
}

void CompileJob::cancel () {
	if (! running ()) return;
	cancelled = true;
	process->kill ();
}

// On the reader thread.
void CompileJob::readOutput () {
	char buffer[4096];
	size_t n;
	while ((n = process->read (buffer, sizeof (buffer))) > 0) {
		std::lock_guard<std::mutex> lock (mutex);
		pending.append (buffer, n);
	}
	int code = process->wait ();
	std::lock_guard<std::mutex> lock (mutex);
	ended = true;
	exitCode = code;
}

bool CompileJob::update () {
	if (! running ()) return false;
	std::string text;
	bool hasEnded;
	int code;
	{
		std::lock_guard<std::mutex> lock (mutex);
		// Only whole lines, until the end.
		size_t end = ended ? pending.size () : pending.rfind ('\n');
		if (end == std::string::npos) end = 0;
		else if (! ended) end ++;
		text = pending.substr (0, end);
		pending.erase (0, end);
		hasEnded = ended;
		code = exitCode;
	}
	size_t start = 0;
	while (start < text.size ()) {
		size_t end = text.find ('\n', start);
		if (end == std::string::npos) end = text.size ();
		std::string line = text.substr (start, end - start);
		if (! line.empty () && line[line.size () - 1] == '\r') line.erase (line.size () - 1);
		if (! line.empty ()) handleLine (line);
		start = end + 1;
	}
	if (hasEnded) {
		if (reader.joinable ()) reader.join ();
		finished = true;
		if (cancelled) {
			succeeded = false;
			Log::get ().add (LogLine::LOG_INFO, "The compile was cancelled.\n");
		} else if (! sawDone) {
			// It ended without saying so, for example in a crash.
			char why[64];
			snprintf (why, sizeof (why), "The compiler stopped (exit code %d).", code);
			addMessage (BuildMessage::INTERNAL, "", 0, why, "");
			succeeded = false;
		}
		Log::get ().add (LogLine::LOG_INFO, "Compiling %s: %d errors, %d warnings\n",
			succeeded ? "succeeded" : "failed", errorCount, warningCount);
	}
	return ! text.empty () || hasEnded;
}

std::vector<std::string> CompileJob::splitFields (const std::string & line) {
	std::vector<std::string> fields;
	std::string field;
	for (size_t i = 0; i < line.size (); i ++) {
		char c = line[i];
		if (c == '\t') {
			fields.push_back (field);
			field.clear ();
		} else if (c == '\\' && i + 1 < line.size ()) {
			char next = line[++ i];
			field += next == 't' ? '\t' : next == 'n' ? '\n' : next;
		} else {
			field += c;
		}
	}
	fields.push_back (field);
	return fields;
}

void CompileJob::addMessage (BuildMessage::Type type, const std::string & f, int line, const std::string & text, const std::string & fullText) {
	BuildMessage message;
	message.type = type;
	message.file = f;
	message.line = line;
	message.text = text;
	message.fullText = fullText.empty () ? text : fullText;
	messages.push_back (message);
	if (type == BuildMessage::WARNING) warningCount ++;
	else if (type != BuildMessage::OUTPUT) errorCount ++;
}

void CompileJob::handleLine (const std::string & line) {
	std::vector<std::string> f = splitFields (line);
	const std::string & kind = f[0];
	if (kind == "progress" && f.size () >= 3) {
		int bar = atoi (f[1].c_str ());
		if (bar == 1 || bar == 2) progress[bar - 1] = atof (f[2].c_str ());
	} else if (kind == "task" && f.size () >= 2) {
		task = f[1];
	} else if (kind == "file" && f.size () >= 2) {
		file = f[1];
	} else if (kind == "item" && f.size () >= 2) {
		item = f[1];
	} else if (kind == "stats" && f.size () >= 6) {
		functions = atoi (f[1].c_str ());
		objectTypes = atoi (f[2].c_str ());
		globals = atoi (f[3].c_str ());
		strings = atoi (f[4].c_str ());
		resources = atoi (f[5].c_str ());
	} else if (kind == "message" && f.size () >= 6) {
		BuildMessage::Type type = f[1] == "warning" ? BuildMessage::WARNING
			: f[1] == "error" ? BuildMessage::ERROR
			: f[1] == "system" ? BuildMessage::SYSTEM : BuildMessage::INTERNAL;
		addMessage (type, f[2], atoi (f[3].c_str ()), f[4], f[5]);
	} else if (kind == "done" && f.size () >= 2) {
		sawDone = true;
		succeeded = f[1] == "1";
		if (f.size () >= 3) gameFile = f[2];
	} else {
		// Anything else the compiler wrote, such as to stderr.
		addMessage (BuildMessage::OUTPUT, "", 0, line, line);
	}
}

} // namespace devkit
