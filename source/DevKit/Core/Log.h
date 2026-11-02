#ifndef SLUDGE_DEVKIT_LOG_H
#define SLUDGE_DEVKIT_LOG_H

/*
 * The messages of the Dev Kit and of the renderers, which the Log panel
 * shows. Any thread may add one.
 */

#include <stdarg.h>

#include <string>
#include <vector>

namespace devkit {

struct LogLine {
	enum Level { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR };
	Level level;
	std::string text;
};

class Log {
public:
	static Log & get ();

	void add (LogLine::Level level, const char * format, ...);
	void addV (LogLine::Level level, const char * format, va_list args);
	void clear ();

	// A copy of the lines, and a number that grows with each change, so
	// that a panel knows when to look again.
	std::vector<LogLine> lines () const;
	unsigned int version () const;

	// Also write each line to stderr, with or without the details
	// (LOG_DEBUG).
	void setEcho (bool echo, bool details);

private:
	Log ();
	struct State;
	State * state;
};

// For gfx::setLogFunction: the renderers' details, at LOG_DEBUG.
void logRendererLine (const char * format, va_list args);

} // namespace devkit

#endif
