/*
 * The SLUDGE Dev Kit: SDL 3 for the window and input, Dear ImGui for the
 * interface, drawn with the renderers of the engine.
 *
 * Usage: sludge-devkit [--renderer=auto|gpu|gl] [--gpu-driver=<name>]
 *                      [--smoke=<picture.png>] [--smoke-compile=<project>]
 *                      [--smoke-check=<project>] [--smoke-input=<steps>]
 *                      [file ...]
 *
 * With --smoke, we draw a few frames in a window of a fixed size, save the
 * last one as a PNG, and quit, for the tests. With --smoke-compile, we
 * open a project and compile it first, and the exit code says whether it
 * compiled. With --smoke-check, we open a project and check its files. With
 * --smoke-input, we take the mouse and keys from a file (SmokeInput.h)
 * instead of the user, and the exit code says whether its steps worked.
 * With SLUDGE_DEVKIT_DEBUG set in the environment, we write the log to
 * stderr, with the details of the renderer.
 */

#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <png.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_gfx.h"

#include "renderer.h"
#include "renderer_window.h"

#include "draw2d.h"
#include "draw2d_gfx.h"

#include "Icons.h"
#include "InterfacePlatform.h"
#include "Log.h"
#include "PlatformSDL.h"
#include "SmokeInput.h"
#include "Workspace.h"

using namespace devkit;

// The size of the window in a smoke test, in pixels.
static const int SMOKE_WIDTH = 1280, SMOKE_HEIGHT = 800;
// How many frames a smoke test draws: enough for the layout to settle.
static const int SMOKE_FRAMES = 8;

// After an event, we draw a few more frames, so that ImGui can finish what
// it started, and then we wait for the next event.
static const int FRAMES_AFTER_EVENT = 3;
// While we wait, we still draw now and then, for the blinking cursor.
static const Sint32 IDLE_WAIT_MS = 500;

static bool startsWith (const char * s, const char * prefix) {
	return strncmp (s, prefix, strlen (prefix)) == 0;
}

static void usage () {
	fprintf (stderr, "usage: sludge-devkit [--renderer=auto|gpu|gl] [--gpu-driver=<name>] [--smoke=<picture.png>]\n"
		"                     [--smoke-compile=<project>] [--smoke-check=<project>] [--smoke-input=<steps>]\n"
		"                     [file ...]\n");
}

// Save the frame in the window, which we read bottom row first.
static bool saveFrame (gfx::Renderer * renderer, int width, int height, const char * filename) {
	std::vector<unsigned char> pixels ((size_t) width * height * 4);
	renderer->setTarget (NULL);
	renderer->readPixels (0, 0, width, height, pixels.data ());
	for (int y = 0; y < height / 2; y ++) {
		unsigned char * a = &pixels[(size_t) y * width * 4];
		unsigned char * b = &pixels[(size_t) (height - 1 - y) * width * 4];
		for (int i = 0; i < width * 4; i ++) {
			unsigned char t = a[i];
			a[i] = b[i];
			b[i] = t;
		}
	}
	// The window has no alpha of its own.
	for (size_t i = 3; i < pixels.size (); i += 4) pixels[i] = 255;

	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = width;
	png.height = height;
	png.format = PNG_FORMAT_RGBA;
	if (! png_image_write_to_file (&png, filename, 0, pixels.data (), 0, NULL)) {
		fprintf (stderr, "Can't save %s: %s\n", filename, png.message);
		return false;
	}
	return true;
}

// The font of the interface of the system, or NULL when we find none.
static ImFont * addSystemFont (ImGuiIO & io) {
	std::vector<std::string> fonts;
#if defined(__APPLE__)
	fonts.push_back ("/System/Library/Fonts/SFNS.ttf");			// macOS 10.15 and later
	fonts.push_back ("/System/Library/Fonts/SFNSText.ttf");		// macOS 10.11 to 10.14
	fonts.push_back ("/System/Library/Fonts/Helvetica.ttc");
#elif defined(_WIN32)
	const char * windows = SDL_getenv ("WINDIR");
	std::string folder = std::string (windows ? windows : "C:\\Windows") + "\\Fonts\\";
	fonts.push_back (folder + "segoeui.ttf");
	fonts.push_back (folder + "tahoma.ttf");
#else
	const char * folders[] = { "/usr/share/fonts/truetype/", "/usr/share/fonts/TTF/", "/usr/share/fonts/" };
	const char * names[] = { "dejavu/DejaVuSans.ttf", "DejaVuSans.ttf", "noto/NotoSans-Regular.ttf",
		"google-noto/NotoSans-Regular.ttf", "liberation/LiberationSans-Regular.ttf", "LiberationSans-Regular.ttf" };
	for (size_t n = 0; n < sizeof (names) / sizeof (names[0]); n ++) {
		for (size_t f = 0; f < sizeof (folders) / sizeof (folders[0]); f ++) fonts.push_back (std::string (folders[f]) + names[n]);
	}
#endif
	for (size_t i = 0; i < fonts.size (); i ++) {
		SDL_PathInfo info;
		if (! SDL_GetPathInfo (fonts[i].c_str (), &info) || info.type != SDL_PATHTYPE_FILE) continue;
		ImFont * font = io.Fonts->AddFontFromFileTTF (fonts[i].c_str ());
		if (font) {
			Log::get ().add (LogLine::LOG_DEBUG, "The font is %s\n", fonts[i].c_str ());
			return font;
		}
	}
	return NULL;
}

int main (int argc, char ** argv) {
	const char * rendererChoice = SDL_getenv ("SLUDGE_RENDERER");
	const char * smokeFile = NULL;
	const char * smokeProject = NULL;
	const char * smokeCheck = NULL;
	const char * smokeInput = NULL;
	std::vector<std::string> files;
	for (int i = 1; i < argc; i ++) {
		const char * arg = argv[i];
		if (startsWith (arg, "--renderer=")) {
			rendererChoice = arg + strlen ("--renderer=");
		} else if (startsWith (arg, "--gpu-driver=")) {
			SDL_SetHintWithPriority (SDL_HINT_GPU_DRIVER, arg + strlen ("--gpu-driver="), SDL_HINT_OVERRIDE);
		} else if (startsWith (arg, "--smoke=")) {
			smokeFile = arg + strlen ("--smoke=");
		} else if (startsWith (arg, "--smoke-compile=")) {
			smokeProject = arg + strlen ("--smoke-compile=");
		} else if (startsWith (arg, "--smoke-check=")) {
			smokeCheck = arg + strlen ("--smoke-check=");
		} else if (startsWith (arg, "--smoke-input=")) {
			smokeInput = arg + strlen ("--smoke-input=");
		} else if (! strcmp (arg, "-h") || ! strcmp (arg, "--help")) {
			usage ();
			return 0;
		} else if (startsWith (arg, "-psn_")) {
			// The process serial number that old versions of macOS pass.
		} else if (arg[0] == '-') {
			fprintf (stderr, "Unknown option %s\n", arg);
			usage ();
			return 1;
		} else {
			files.push_back (arg);
		}
	}

	// With SLUDGE_DEVKIT_DEBUG set, we write the log to stderr, with the
	// details. A smoke test writes it without the details.
	bool debug = SDL_getenv ("SLUDGE_DEVKIT_DEBUG") != NULL;
	if ((smokeProject || smokeInput) && ! smokeFile) {
		fprintf (stderr, "--smoke-compile and --smoke-input need --smoke\n");
		return 1;
	}
	SmokeInput input;
	if (smokeInput) {
		std::string error;
		if (! input.read (smokeInput, &error)) {
			fprintf (stderr, "%s\n", error.c_str ());
			return 1;
		}
	}
	Log::get ().setEcho (debug || smokeFile, debug);
	gfx::setLogFunction (logRendererLine);

	// The identifier is the name of the desktop file on Linux, by which the
	// desktop finds the icon of the window. On Windows, the window has the
	// icon of the executable.
	SDL_SetAppMetadata ("SLUDGE Dev Kit", NULL, "io.github.opensludge.devkit");
	SDL_SetHint (SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
	SDL_SetHint (SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
	if (! SDL_Init (SDL_INIT_VIDEO)) {
		fprintf (stderr, "Can't start SDL: %s\n", SDL_GetError ());
		return 1;
	}

	// The window is as large as fits comfortably, scaled as the display.
	float scale = SDL_GetDisplayContentScale (SDL_GetPrimaryDisplay ());
	if (scale <= 0) scale = 1.0f;
	gfx::WindowOptions options;
	options.title = "SLUDGE Dev Kit";
	options.width = (int) (1280 * scale);
	options.height = (int) (800 * scale);
	options.flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
	options.renderer = rendererChoice ? rendererChoice : "auto";
	if (smokeFile) {
		// One pixel for each point, on every display.
		scale = 1.0f;
		options.width = SMOKE_WIDTH;
		options.height = SMOKE_HEIGHT;
		options.flags = SDL_WINDOW_HIDDEN;
		options.immediate = true;
	}
	SDL_Window * window;
	gfx::WindowProblem problem;
	gfx::Renderer * renderer = gfx::createWindowRenderer (options, &window, &problem);
	if (! renderer) {
		SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR, problem.message, problem.detail, NULL);
		SDL_Quit ();
		return problem.exitCode;
	}
	std::string rendererName = gfx::windowRendererName ();
	SDL_SetWindowMinimumSize (window, 640, 400);

	PlatformSDL platform (window);
	if (smokeFile) {
		// A smoke test starts with no settings, and leaves the user's alone.
		std::string folder = platform.cacheFolder () + "smoke-settings/";
		SDL_CreateDirectory (folder.c_str ());
		SDL_RemovePath ((folder + "recent.txt").c_str ());
		SDL_RemovePath ((folder + "SLUDGE.ini").c_str ());
		SDL_RemovePath ((folder + "files.txt").c_str ());
		platform.setSettingsFolder (folder);
		platform.setUnattended (true);
	}
	setInterfacePlatform (&platform);

	IMGUI_CHECKVERSION ();
	ImGui::CreateContext ();
	ImGuiIO & io = ImGui::GetIO ();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
#if defined(__APPLE__)
	io.ConfigMacOSXBehaviors = true;
#endif
	// The places of the panels go in the settings folder, but a smoke
	// test always starts with the default layout.
	std::string iniFile = platform.settingsFolder () + "imgui.ini";
	io.IniFilename = smokeFile ? NULL : iniFile.c_str ();

	ImGui::StyleColorsDark ();
	ImGuiStyle & style = ImGui::GetStyle ();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 3.0f;
	style.TabRounding = 3.0f;
	style.ScaleAllSizes (scale);
	style.FontScaleDpi = scale;
	style.FontSizeBase = 15.0f;
	// The font of the system's interface, which reads better than ImGui's
	// own, with ImGui's for the characters that it lacks. A smoke test keeps
	// ImGui's alone, so that the places of things in its window are the
	// same on every system.
	// (SLUDGE_DEVKIT_SMOKE_FONT=system gives it the system's, as for the
	// pictures of the help.)
	const char * smokeFont = SDL_getenv ("SLUDGE_DEVKIT_SMOKE_FONT");
	bool systemFont = ! smokeFile || (smokeFont && ! strcmp (smokeFont, "system"));
	ImFont * font = systemFont ? addSystemFont (io) : NULL;
	if (font) {
		ImFontConfig fallback;
		fallback.MergeMode = true;
		io.Fonts->AddFontDefaultVector (&fallback);
	} else {
		io.Fonts->AddFontDefaultVector ();
	}

	ImGui_ImplSDL3_InitForOther (window);
	ImGui_ImplGfx_Init (renderer);

	// The pictures of the editors, and the textures of their sprites.
	Draw2D * draw2d = createGfxDraw2D (renderer);
	CanvasGraphics graphics = { renderer, draw2d };
	std::unique_ptr<Workspace> theWorkspace (new Workspace (platform, rendererName, graphics));
	Workspace & workspace = *theWorkspace;
	// Smoke tests stay silent: they log the sounds they would play.
	if (smokeFile) workspace.sounds ().setEnabled (false);
	workspace.openFiles (files);
	if (smokeProject) {
		workspace.openFiles (std::vector<std::string> (1, smokeProject));
		workspace.compile ();
	}
	if (smokeCheck) {
		workspace.openFiles (std::vector<std::string> (1, smokeCheck));
		workspace.checkFiles ();
	}
	std::string title = workspace.title ();
	SDL_SetWindowTitle (window, title.c_str ());
	SDL_ShowWindow (window);
	SDL_RaiseWindow (window);

	int framesToDraw = FRAMES_AFTER_EVENT;
	int framesDrawn = 0;
	int exitCode = 0;
	bool done = false;
	while (! done) {
		SDL_Event event;
		bool haveEvent = framesToDraw > 0 || smokeFile ? SDL_PollEvent (&event) : SDL_WaitEventTimeout (&event, IDLE_WAIT_MS);
		while (haveEvent) {
			// In a smoke test with input, the input is the file's only.
			if (! smokeInput) ImGui_ImplSDL3_ProcessEvent (&event);
			if (event.type == SDL_EVENT_QUIT) workspace.requestQuit ();
			// Only the events of our window: on Windows, OpenGL draws in a
			// hidden window of its own too (dxgipresent.cpp).
			bool ours = event.window.windowID == SDL_GetWindowID (window);
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ours) workspace.requestQuit ();
			if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && ours) gfx::windowResized (event.window.data1, event.window.data2);
			if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) workspace.dropFiles (std::vector<std::string> (1, event.drop.data));
			framesToDraw = FRAMES_AFTER_EVENT;
			haveEvent = SDL_PollEvent (&event);
		}
		platform.deliverAnswers ();
		if (framesToDraw > 0) framesToDraw --;

		if (smokeInput) {
			// The input is the file's, not that of the mouse of the system.
			int w, h;
			SDL_GetWindowSize (window, &w, &h);
			io.DisplaySize = ImVec2 ((float) w, (float) h);
			io.DisplayFramebufferScale = ImVec2 (1, 1);
			io.DeltaTime = 1.0f / 60.0f;
			if (! input.step (workspace)) exitCode = 1;
		} else {
			ImGui_ImplSDL3_NewFrame ();
		}
		ImGui::NewFrame ();
		workspace.draw ();
		ImGui::Render ();

		int width, height;
		SDL_GetWindowSizeInPixels (window, &width, &height);
		renderer->setTarget (NULL);
		renderer->setScissor (0, 0, 0, 0);
		renderer->setViewport (0, 0, width, height);
		renderer->clear (true, true);
		ImGui_ImplGfx_RenderDrawData (ImGui::GetDrawData (), width, height);

		framesDrawn ++;
		// A smoke test waits for its compile, and a few frames after it.
		if (smokeFile && (workspace.compiling () || input.running ())) framesDrawn = 0;
		if (smokeFile && framesDrawn == SMOKE_FRAMES) {
			if (! saveFrame (renderer, width, height, smokeFile)) return 1;
			done = true;
			if (smokeProject && ! workspace.compileSucceeded ()) exitCode = 1;
		}
		renderer->present ();
		workspace.frameDone ();

		if (workspace.title () != title) {
			title = workspace.title ();
			SDL_SetWindowTitle (window, title.c_str ());
		}
		if (workspace.animating ()) framesToDraw = FRAMES_AFTER_EVENT;
		if (workspace.wantsToQuit ()) done = true;
	}

	// The editors have textures, which go before the renderer.
	theWorkspace.reset ();
	delete draw2d;
	devkit::forgetIcons ();
	ImGui_ImplGfx_Shutdown ();
	ImGui_ImplSDL3_Shutdown ();
	ImGui::DestroyContext ();
	gfx::destroyWindowRenderer ();
	SDL_Quit ();
	return exitCode;
}
