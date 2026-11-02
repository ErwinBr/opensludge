#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

#include <png.h>
#include <zlib.h>

#include <SDL3/SDL.h>

#include "capture.h"
#include "graphics.h"
#include "language.h"

namespace {

enum CommandType {
	CMD_MOVE, CMD_DOWN, CMD_UP, CMD_KEY, CMD_TEXT, CMD_CAPTURE, CMD_DUMP_BACKDROP, CMD_QUIT
};

struct Command {
	unsigned int frame;
	CommandType type;
	int x, y;
	int button;			// SDL_BUTTON_LEFT or SDL_BUTTON_RIGHT
	std::string text;	// The key name or the typed text
};

// A range of frames from --frames, such as 100-200/10.
struct FrameRange {
	unsigned int first, last, step;
};

bool deterministic = false;
bool forcePOT = false;
const char * rendererName = NULL;
bool recording = false;
unsigned int seed = 1;
long maxFrames = -1;

std::string captureFolder;
std::string userFolder;
FILE * recordFile = NULL;
FILE * framesFile = NULL;

std::vector<Command> commands;
size_t nextCommand = 0;
std::vector<FrameRange> frameRanges;

unsigned int frame = 0;
bool frameDrawn = false;
bool captureThisFrame = false;
bool quitRequested = false;

// The last frame we read, top row first, RGB.
std::vector<unsigned char> lastPixels;
int lastWidth = 0, lastHeight = 0;
long lastFrameNumber = -1;

Uint64 virtualNS = 0;

// While we push the events of the input script, the event filter lets them
// through. It drops the mouse, keyboard and controller events of the
// player.
bool injecting = false;

bool SDLCALL filterPlayerInput (void * userdata, SDL_Event * event) {
	if (injecting) return true;
	switch (event->type) {
		case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
		case SDL_EVENT_MOUSE_WHEEL:
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
		case SDL_EVENT_TEXT_INPUT:
		case SDL_EVENT_TEXT_EDITING:
		case SDL_EVENT_GAMEPAD_ADDED:
		case SDL_EVENT_GAMEPAD_REMOVED:
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			return false;
		default:
			return true;
	}
}

// Make a folder and all the folders above it, and return its absolute path,
// so that it stays valid when the engine changes the current folder.
bool makeAbsoluteFolder (const char * path, std::string & result) {
	if (! SDL_CreateDirectory (path)) {
		fprintf (stderr, "Can't create the folder %s: %s\n", path, SDL_GetError ());
		return false;
	}
#if defined(_WIN32)
	char * full = _fullpath (NULL, path, 0);
#else
	char * full = realpath (path, NULL);
#endif
	if (! full) {
		fprintf (stderr, "Can't find the folder %s\n", path);
		return false;
	}
	result = full;
	free (full);
	return true;
}

bool parseFrames (const char * list) {
	const char * p = list;
	while (*p) {
		FrameRange r;
		char * end;
		r.first = (unsigned int) strtoul (p, &end, 10);
		if (end == p) return false;
		r.last = r.first;
		r.step = 1;
		p = end;
		if (*p == '-') {
			r.last = (unsigned int) strtoul (p + 1, &end, 10);
			if (end == p + 1 || r.last < r.first) return false;
			p = end;
			if (*p == '/') {
				r.step = (unsigned int) strtoul (p + 1, &end, 10);
				if (end == p + 1 || r.step == 0) return false;
				p = end;
			}
		}
		frameRanges.push_back (r);
		if (*p == ',') p ++;
		else if (*p) return false;
	}
	return true;
}

bool frameIsChosen (unsigned int f) {
	for (size_t i = 0; i < frameRanges.size (); i ++) {
		const FrameRange & r = frameRanges[i];
		if (f >= r.first && f <= r.last && (f - r.first) % r.step == 0) return true;
	}
	return false;
}

bool parseButton (const char * word, int & button) {
	if (! strcmp (word, "left")) button = SDL_BUTTON_LEFT;
	else if (! strcmp (word, "right")) button = SDL_BUTTON_RIGHT;
	else return false;
	return true;
}

// Read an input script. We sort the commands by frame, keeping the order
// of commands in the same frame.
bool readInputScript (const char * filename) {
	FILE * f = fopen (filename, "r");
	if (! f) {
		fprintf (stderr, "Can't open the input script %s: %s\n", filename, strerror (errno));
		return false;
	}
	char line[1024];
	int lineNumber = 0;
	bool ok = true;
	while (fgets (line, sizeof (line), f)) {
		lineNumber ++;
		size_t len = strlen (line);
		while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[-- len] = 0;
		char * p = line;
		while (*p == ' ' || *p == '\t') p ++;
		if (! *p || *p == '#') continue;

		Command c;
		c.x = c.y = 0;
		c.button = SDL_BUTTON_LEFT;
		char word[32], arg[32];
		int used = 0;
		bool good = sscanf (p, "%u %31s%n", &c.frame, word, &used) == 2;
		const char * rest = p + used;
		while (*rest == ' ' || *rest == '\t') rest ++;
		if (! good) {
		} else if (! strcmp (word, "move")) {
			c.type = CMD_MOVE;
			good = sscanf (rest, "%d %d", &c.x, &c.y) == 2;
		} else if (! strcmp (word, "down") || ! strcmp (word, "up")) {
			c.type = strcmp (word, "down") ? CMD_UP : CMD_DOWN;
			good = sscanf (rest, "%31s", arg) == 1 && parseButton (arg, c.button);
		} else if (! strcmp (word, "click")) {
			good = sscanf (rest, "%d %d", &c.x, &c.y) == 2;
			if (good) {
				c.type = CMD_MOVE;
				commands.push_back (c);
				c.type = CMD_DOWN;
				commands.push_back (c);
				c.frame ++;
				c.type = CMD_UP;
			}
		} else if (! strcmp (word, "key")) {
			c.type = CMD_KEY;
			c.text = rest;
			good = SDL_GetKeyFromName (rest) != SDLK_UNKNOWN;
		} else if (! strcmp (word, "text")) {
			// The game reads one character in each frame, so we type one
			// character in each frame.
			c.type = CMD_TEXT;
			good = *rest != 0;
			for (const char * t = rest; *t; ) {
				const char * next = t + 1;
				while ((*next & 0xC0) == 0x80) next ++;
				c.text.assign (t, next - t);
				if (*next) {
					commands.push_back (c);
					c.frame ++;
				}
				t = next;
			}
		} else if (! strcmp (word, "capture")) {
			c.type = CMD_CAPTURE;
		} else if (! strcmp (word, "dump")) {
			c.type = CMD_DUMP_BACKDROP;
			good = ! strcmp (rest, "backdrop");
		} else if (! strcmp (word, "quit")) {
			c.type = CMD_QUIT;
		} else {
			good = false;
		}
		if (! good) {
			fprintf (stderr, "%s:%d: can't understand \"%s\"\n", filename, lineNumber, p);
			ok = false;
			continue;
		}
		commands.push_back (c);
	}
	fclose (f);

	// A stable insertion sort, because scripts are nearly sorted already.
	for (size_t i = 1; i < commands.size (); i ++) {
		Command c = commands[i];
		size_t j = i;
		while (j > 0 && commands[j - 1].frame > c.frame) {
			commands[j] = commands[j - 1];
			j --;
		}
		commands[j] = c;
	}
	return ok;
}

bool writePNG (const std::string & filename, int width, int height, int channels, const unsigned char * pixels) {
	FILE * fp = fopen (filename.c_str (), "wb");
	if (! fp) {
		fprintf (stderr, "Can't write %s: %s\n", filename.c_str (), strerror (errno));
		return false;
	}
	png_structp png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info_ptr = png_ptr ? png_create_info_struct (png_ptr) : NULL;
	if (! info_ptr || setjmp (png_jmpbuf (png_ptr))) {
		png_destroy_write_struct (&png_ptr, info_ptr ? &info_ptr : NULL);
		fclose (fp);
		fprintf (stderr, "Can't write %s\n", filename.c_str ());
		return false;
	}
	png_init_io (png_ptr, fp);
	png_set_IHDR (png_ptr, info_ptr, width, height, 8,
		channels == 4 ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info (png_ptr, info_ptr);
	for (int y = 0; y < height; y ++) {
		png_write_row (png_ptr, (png_bytep) (pixels + (size_t) y * width * channels));
	}
	png_write_end (png_ptr, NULL);
	png_destroy_write_struct (&png_ptr, &info_ptr);
	fclose (fp);
	return true;
}

std::string numberedName (const char * prefix, long number) {
	char name[64];
	snprintf (name, sizeof (name), "%s_%06ld.png", prefix, number);
	return captureFolder + "/" + name;
}

void dumpBackdrop () {
	if (captureFolder.empty ()) return;
	int w, h;
	unsigned char * pixels = readBackdropPixels (&w, &h);
	if (! pixels) {
		fprintf (stderr, "Can't read the backdrop in frame %u\n", frame);
		return;
	}
	writePNG (numberedName ("backdrop", frame), w, h, 4, pixels);
	delete [] pixels;
}

void pushEvent (SDL_Event & event) {
	injecting = true;
	SDL_PushEvent (&event);
	injecting = false;
}

void pushMouseButton (const Command & c, bool down, int x, int y) {
	SDL_Event event;
	SDL_zero (event);
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = SDL_GetWindowID (sludgeWindow);
	event.button.button = (Uint8) c.button;
	event.button.down = down;
	event.button.clicks = 1;
	float wx, wy;
	gameToWindowCoords ((float) x, (float) y, &wx, &wy);
	event.button.x = wx;
	event.button.y = wy;
	pushEvent (event);
}

// The last mouse position of the script, in game pixels, for the buttons.
int mouseX = 0, mouseY = 0;

void runCommand (const Command & c) {
	SDL_Event event;
	SDL_zero (event);
	switch (c.type) {
		case CMD_MOVE: {
			mouseX = c.x;
			mouseY = c.y;
			event.type = SDL_EVENT_MOUSE_MOTION;
			event.motion.windowID = SDL_GetWindowID (sludgeWindow);
			float wx, wy;
			gameToWindowCoords ((float) c.x, (float) c.y, &wx, &wy);
			event.motion.x = wx;
			event.motion.y = wy;
			pushEvent (event);
			break;
		}
		case CMD_DOWN:
			pushMouseButton (c, true, mouseX, mouseY);
			break;
		case CMD_UP:
			pushMouseButton (c, false, mouseX, mouseY);
			break;
		case CMD_KEY:
			event.type = SDL_EVENT_KEY_DOWN;
			event.key.windowID = SDL_GetWindowID (sludgeWindow);
			event.key.key = SDL_GetKeyFromName (c.text.c_str ());
			event.key.down = true;
			pushEvent (event);
			event.type = SDL_EVENT_KEY_UP;
			event.key.down = false;
			pushEvent (event);
			break;
		case CMD_TEXT:
			event.type = SDL_EVENT_TEXT_INPUT;
			event.text.windowID = SDL_GetWindowID (sludgeWindow);
			// SDL doesn't copy or free the text. The commands stay in memory
			// for the whole run, so the pointer stays valid.
			event.text.text = c.text.c_str ();
			pushEvent (event);
			break;
		case CMD_CAPTURE:
			captureThisFrame = true;
			break;
		case CMD_DUMP_BACKDROP:
			dumpBackdrop ();
			break;
		case CMD_QUIT:
			quitRequested = true;
			break;
	}
}

void recordLine (const char * format, ...) SDL_PRINTF_VARARG_FUNC (1);
void recordLine (const char * format, ...) {
	if (! recordFile) return;
	fprintf (recordFile, "%u ", frame);
	va_list args;
	va_start (args, format);
	vfprintf (recordFile, format, args);
	va_end (args);
	fputc ('\n', recordFile);
}

} // namespace

void printCaptureUsage () {
	fprintf (stdout, "Options for testing the engine:\n");
	fprintf (stdout, "--capture=<folder>		Run deterministically and write frame CRCs and PNGs there\n");
	fprintf (stdout, "--frames=<list>			Frames to write as PNG, e.g. 0,100-200/10\n");
	fprintf (stdout, "--max-frames=<number>		Stop after this many frames\n");
	fprintf (stdout, "--input=<file>			Play the mouse and keyboard events of an input script\n");
	fprintf (stdout, "--record-input=<file>		Write the player's events as an input script\n");
	fprintf (stdout, "--seed=<number>			Seed for random numbers (default 1)\n");
	fprintf (stdout, "--user-dir=<folder>		Folder for settings and saved games\n");
	fprintf (stdout, "--pot-textures			Use power-of-two textures, as on OpenGL ES 2.0 devices\n");
	fprintf (stdout, "--renderer=<auto|gpu|gl>	Draw with SDL_GPU (Metal, Vulkan, Direct3D 12) or OpenGL;\n");
	fprintf (stdout, "				auto (default) chooses SDL_GPU on macOS and Windows, OpenGL on Linux\n");
	fprintf (stdout, "--gpu-driver=<name>		The driver of SDL_GPU: metal, vulkan or direct3d12\n\n");
}

bool parseCaptureOptions (int & argc, char ** argv) {
	bool ok = true;
	const char * inputFile = NULL;
	const char * recordName = NULL;
	const char * userDir = NULL;
	int out = 1;
	for (int i = 1; i < argc; i ++) {
		const char * a = argv[i];
		const char * value = strchr (a, '=');
		value = value ? value + 1 : "";
		if (! strncmp (a, "--capture=", 10)) {
			ok = makeAbsoluteFolder (value, captureFolder) && ok;
			deterministic = true;
		} else if (! strncmp (a, "--frames=", 9)) {
			if (! parseFrames (value)) {
				fprintf (stderr, "Can't understand --frames=%s\n", value);
				ok = false;
			}
		} else if (! strncmp (a, "--max-frames=", 13)) {
			maxFrames = atol (value);
			deterministic = true;
		} else if (! strncmp (a, "--input=", 8)) {
			inputFile = value;
			deterministic = true;
		} else if (! strncmp (a, "--record-input=", 15)) {
			recordName = value;
		} else if (! strncmp (a, "--seed=", 7)) {
			seed = (unsigned int) strtoul (value, NULL, 10);
		} else if (! strncmp (a, "--user-dir=", 11)) {
			userDir = value;
		} else if (! strcmp (a, "--pot-textures")) {
			forcePOT = true;
		} else if (! strncmp (a, "--renderer=", 11)) {
			rendererName = value;
		} else if (! strncmp (a, "--gpu-driver=", 13)) {
			// With the normal priority, SDL_GPU_DRIVER in the environment
			// would win over the option.
			SDL_SetHintWithPriority (SDL_HINT_GPU_DRIVER, value, SDL_HINT_OVERRIDE);
		} else {
			argv[out ++] = argv[i];
			continue;
		}
	}
	argc = out;
	argv[argc] = NULL;

	if (recordName && deterministic) {
		fprintf (stderr, "--record-input can't be combined with --capture, --input or --max-frames\n");
		ok = false;
	}
	if (inputFile) ok = readInputScript (inputFile) && ok;
	if (recordName) {
		recordFile = fopen (recordName, "w");
		if (recordFile) {
			fprintf (recordFile, "# Recorded by the SLUDGE engine. Positions are in game pixels.\n");
			recording = true;
		} else {
			fprintf (stderr, "Can't write the input script %s: %s\n", recordName, strerror (errno));
			ok = false;
		}
	}
	if (userDir) {
		ok = makeAbsoluteFolder (userDir, userFolder) && ok;
	} else if (! captureFolder.empty ()) {
		std::string user = captureFolder + "/user";
		ok = makeAbsoluteFolder (user.c_str (), userFolder) && ok;
	}
	if (! captureFolder.empty ()) {
		std::string name = captureFolder + "/frames.tsv";
		framesFile = fopen (name.c_str (), "w");
		if (! framesFile) {
			fprintf (stderr, "Can't write %s: %s\n", name.c_str (), strerror (errno));
			ok = false;
		}
	}
	if (deterministic) SDL_SetEventFilter (filterPlayerInput, NULL);
	return ok;
}

bool captureIsDeterministic () {
	return deterministic;
}

bool captureIsActive () {
	return deterministic || recording;
}

unsigned int captureSeed () {
	return seed;
}

bool captureForcePOTTextures () {
	return forcePOT;
}

const char * requestedRenderer () {
	if (rendererName) return rendererName;
	const char * env = SDL_getenv ("SLUDGE_RENDERER");
	if (env && *env) return env;
	return gameSettings.renderer[0] ? gameSettings.renderer : "auto";
}

Uint64 engineTicksNS () {
	return deterministic ? virtualNS : SDL_GetTicksNS ();
}

Uint64 engineTicksMS () {
	return engineTicksNS () / SDL_NS_PER_MS;
}

void engineDelayMS (Uint32 ms) {
	if (deterministic) {
		virtualNS += SDL_MS_TO_NS (ms);
	} else {
		SDL_Delay (ms);
	}
}

void engineDelayNS (Uint64 ns, bool precise) {
	if (deterministic) {
		virtualNS += ns;
	} else if (precise) {
		SDL_DelayPrecise (ns);
	} else {
		SDL_DelayNS (ns);
	}
}

bool captureChangeToUserDir () {
	if (userFolder.empty ()) return false;
	if (chdir (userFolder.c_str ())) {
		fprintf (stderr, "Can't change to the folder %s\n", userFolder.c_str ());
	}
	return true;
}

void captureStartFrame () {
	frameDrawn = false;
	captureThisFrame = frameIsChosen (frame);
	while (nextCommand < commands.size () && commands[nextCommand].frame <= frame) {
		runCommand (commands[nextCommand ++]);
	}
}

void captureRecordEvent (const SDL_Event & event) {
	if (! recordFile) return;
	int x, y;
	switch (event.type) {
		case SDL_EVENT_MOUSE_MOTION:
			windowToGameCoords (event.motion.x, event.motion.y, &x, &y);
			recordLine ("move %d %d", x, y);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (event.button.button != SDL_BUTTON_LEFT && event.button.button != SDL_BUTTON_RIGHT) break;
			windowToGameCoords (event.button.x, event.button.y, &x, &y);
			recordLine ("move %d %d", x, y);
			recordLine ("%s %s", event.button.down ? "down" : "up",
				event.button.button == SDL_BUTTON_LEFT ? "left" : "right");
			break;
		case SDL_EVENT_KEY_DOWN:
			if (event.key.repeat) break;
			recordLine ("key %s", SDL_GetKeyName (event.key.key));
			break;
		case SDL_EVENT_TEXT_INPUT:
			recordLine ("text %s", event.text.text);
			break;
		default:
			break;
	}
}

void captureFrameDrawn () {
	if (! framesFile || frameDrawn) return;
	frameDrawn = true;

	int w, h;
	unsigned char * rgba = readFramePixels (&w, &h);
	if (! rgba) return;

	// We compare what the player sees, so we leave out the alpha channel.
	lastPixels.resize ((size_t) w * h * 3);
	for (size_t i = 0, n = (size_t) w * h; i < n; i ++) {
		lastPixels[i * 3] = rgba[i * 4];
		lastPixels[i * 3 + 1] = rgba[i * 4 + 1];
		lastPixels[i * 3 + 2] = rgba[i * 4 + 2];
	}
	delete [] rgba;
	lastWidth = w;
	lastHeight = h;
	lastFrameNumber = frame;

	uLong crc = crc32 (0L, Z_NULL, 0);
	crc = crc32 (crc, lastPixels.data (), (uInt) lastPixels.size ());
	fprintf (framesFile, "%u\t%dx%d\t%08lx\n", frame, w, h, (unsigned long) crc);

	if (captureThisFrame) writePNG (numberedName ("frame", frame), w, h, 3, lastPixels.data ());
}

bool captureEndFrame () {
	frame ++;
	if (quitRequested) return true;
	if (maxFrames >= 0 && frame >= (unsigned long) maxFrames) return true;
	return false;
}

void captureFinish () {
	if (framesFile) {
		// We always keep the last frame, to see where a run ended.
		if (lastFrameNumber >= 0 && ! frameIsChosen ((unsigned int) lastFrameNumber)) {
			writePNG (numberedName ("frame", lastFrameNumber), lastWidth, lastHeight, 3, lastPixels.data ());
		}
		fclose (framesFile);
		framesFile = NULL;
	}
	if (recordFile) {
		fclose (recordFile);
		recordFile = NULL;
	}
}
