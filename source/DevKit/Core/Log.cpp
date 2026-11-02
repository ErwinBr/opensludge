#include <stdio.h>

#include <mutex>

#include "Log.h"

namespace devkit {

// We keep the last lines only.
static const size_t MAX_LINES = 5000;

struct Log::State {
	mutable std::mutex mutex;
	std::vector<LogLine> lines;
	unsigned int version;
	bool echo, echoDetails;
	// A line that the renderers write in pieces, until its newline.
	std::string partial;
};

Log::Log () : state (new State) {
	state->version = 0;
	state->echo = state->echoDetails = false;
}

Log & Log::get () {
	static Log log;
	return log;
}

void Log::add (LogLine::Level level, const char * format, ...) {
	va_list args;
	va_start (args, format);
	addV (level, format, args);
	va_end (args);
}

void Log::addV (LogLine::Level level, const char * format, va_list args) {
	char buffer[2048];
	vsnprintf (buffer, sizeof (buffer), format, args);

	std::lock_guard<std::mutex> lock (state->mutex);
	if (state->echo && (level != LogLine::LOG_DEBUG || state->echoDetails)) fputs (buffer, stderr);
	// Every line of the text becomes a line of the log.
	std::string text = state->partial + buffer;
	state->partial.clear ();
	size_t start = 0;
	for (;;) {
		size_t end = text.find ('\n', start);
		if (end == std::string::npos) {
			state->partial = text.substr (start);
			break;
		}
		if (end > start) {
			LogLine line = { level, text.substr (start, end - start) };
			state->lines.push_back (line);
		}
		start = end + 1;
	}
	if (state->lines.size () > MAX_LINES) {
		state->lines.erase (state->lines.begin (), state->lines.begin () + (state->lines.size () - MAX_LINES));
	}
	state->version ++;
}

void Log::clear () {
	std::lock_guard<std::mutex> lock (state->mutex);
	state->lines.clear ();
	state->version ++;
}

std::vector<LogLine> Log::lines () const {
	std::lock_guard<std::mutex> lock (state->mutex);
	return state->lines;
}

unsigned int Log::version () const {
	std::lock_guard<std::mutex> lock (state->mutex);
	return state->version;
}

void Log::setEcho (bool echo, bool details) {
	std::lock_guard<std::mutex> lock (state->mutex);
	state->echo = echo;
	state->echoDetails = details;
}

void logRendererLine (const char * format, va_list args) {
	Log::get ().addV (LogLine::LOG_DEBUG, format, args);
}

} // namespace devkit
