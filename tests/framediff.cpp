// Compare the frames of two capture runs of the engine (see
// source/Engine/capture.h).
//
// Usage: framediff [options] <expected> <actual>
//
// <expected> and <actual> are two PNG files or two capture folders. For
// folders, we compare every PNG file of the expected folder with the file of
// the same name in the actual folder, and with --max-diff=0 also the CRCs
// of all the frames in frames.tsv.
//
// Options:
//   --max-diff=<n>    Largest difference allowed in a colour channel (default 0)
//   --max-bad=<f>     Fraction of pixels allowed to differ by more (default 0)
//   --out=<folder>    For each image that differs, write <name>-actual.png
//                     and <name>-difference.png there
//
// The exit status is 0 when everything matches, 1 when something differs and
// 2 for errors.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include <png.h>

namespace {

int maxDiff = 0;
double maxBad = 0.0;
std::string outFolder;

struct Image {
	png_uint_32 width, height;
	std::vector<unsigned char> rgba;
};

bool readImage (const std::string & filename, Image & image) {
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	if (! png_image_begin_read_from_file (&png, filename.c_str ())) {
		fprintf (stderr, "Can't read %s: %s\n", filename.c_str (), png.message);
		return false;
	}
	png.format = PNG_FORMAT_RGBA;
	image.width = png.width;
	image.height = png.height;
	image.rgba.resize (PNG_IMAGE_SIZE (png));
	if (! png_image_finish_read (&png, NULL, image.rgba.data (), 0, NULL)) {
		fprintf (stderr, "Can't read %s: %s\n", filename.c_str (), png.message);
		png_image_free (&png);
		return false;
	}
	return true;
}

bool writeImage (const std::string & filename, const Image & image) {
	png_image png;
	memset (&png, 0, sizeof (png));
	png.version = PNG_IMAGE_VERSION;
	png.width = image.width;
	png.height = image.height;
	png.format = PNG_FORMAT_RGBA;
	if (! png_image_write_to_file (&png, filename.c_str (), 0, image.rgba.data (), 0, NULL)) {
		fprintf (stderr, "Can't write %s: %s\n", filename.c_str (), png.message);
		return false;
	}
	return true;
}

bool isFolder (const std::string & path) {
	struct stat st;
	return stat (path.c_str (), &st) == 0 && S_ISDIR (st.st_mode);
}

std::string baseName (const std::string & path) {
	size_t slash = path.find_last_of ("/\\");
	std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
	if (name.size () > 4 && name.compare (name.size () - 4, 4, ".png") == 0) name.resize (name.size () - 4);
	return name;
}

// Returns 0 when the images match, 1 when they differ and 2 for errors.
int compareImages (const std::string & expectedFile, const std::string & actualFile) {
	Image expected, actual;
	if (! readImage (expectedFile, expected) || ! readImage (actualFile, actual)) return 2;
	if (expected.width != actual.width || expected.height != actual.height) {
		printf ("%s: size %ux%u, expected %ux%u\n", actualFile.c_str (),
			actual.width, actual.height, expected.width, expected.height);
		return 1;
	}

	Image difference = expected;
	size_t pixels = (size_t) expected.width * expected.height;
	size_t bad = 0;
	int largest = 0;
	for (size_t i = 0; i < pixels; i ++) {
		int d = 0;
		for (int c = 0; c < 4; c ++) {
			d = std::max (d, abs ((int) expected.rgba[i * 4 + c] - (int) actual.rgba[i * 4 + c]));
		}
		largest = std::max (largest, d);
		unsigned char * out = &difference.rgba[i * 4];
		if (d > maxDiff) {
			bad ++;
			out[0] = 255; out[1] = 0; out[2] = 0; out[3] = 255;
		} else {
			// The expected image, faded, so that the differences stand out.
			out[0] = (unsigned char) (out[0] / 4 + 160);
			out[1] = (unsigned char) (out[1] / 4 + 160);
			out[2] = (unsigned char) (out[2] / 4 + 160);
			out[3] = 255;
		}
	}

	if (bad <= (size_t) (maxBad * pixels)) {
		if (largest) printf ("%s: largest difference %d, within the tolerance\n", actualFile.c_str (), largest);
		return 0;
	}

	printf ("%s: %zu of %zu pixels differ by more than %d (largest difference %d)\n",
		actualFile.c_str (), bad, pixels, maxDiff, largest);
	if (! outFolder.empty ()) {
		std::string name = outFolder + "/" + baseName (actualFile);
		writeImage (name + "-actual.png", actual);
		writeImage (name + "-difference.png", difference);
	}
	return 1;
}

bool readLines (const std::string & filename, std::vector<std::string> & lines) {
	FILE * f = fopen (filename.c_str (), "r");
	if (! f) return false;
	char line[256];
	while (fgets (line, sizeof (line), f)) lines.push_back (line);
	fclose (f);
	return true;
}

int compareFrameCRCs (const std::string & expectedFolder, const std::string & actualFolder) {
	std::vector<std::string> expected, actual;
	if (! readLines (expectedFolder + "/frames.tsv", expected)) return 0;
	if (! readLines (actualFolder + "/frames.tsv", actual)) {
		printf ("%s/frames.tsv is missing\n", actualFolder.c_str ());
		return 1;
	}
	for (size_t i = 0; i < expected.size () && i < actual.size (); i ++) {
		if (expected[i] != actual[i]) {
			printf ("Frame CRCs differ first at line %zu:\n  expected %s  actual   %s", i + 1,
				expected[i].c_str (), actual[i].c_str ());
			return 1;
		}
	}
	if (expected.size () != actual.size ()) {
		printf ("%zu frames, expected %zu\n", actual.size (), expected.size ());
		return 1;
	}
	return 0;
}

int compareFolders (const std::string & expectedFolder, const std::string & actualFolder) {
	DIR * dir = opendir (expectedFolder.c_str ());
	if (! dir) {
		fprintf (stderr, "Can't open %s\n", expectedFolder.c_str ());
		return 2;
	}
	std::vector<std::string> names;
	while (struct dirent * entry = readdir (dir)) {
		std::string name = entry->d_name;
		if (name.size () > 4 && name.compare (name.size () - 4, 4, ".png") == 0) names.push_back (name);
	}
	closedir (dir);
	std::sort (names.begin (), names.end ());

	int result = 0;
	for (size_t i = 0; i < names.size (); i ++) {
		std::string actualFile = actualFolder + "/" + names[i];
		struct stat st;
		if (stat (actualFile.c_str (), &st) != 0) {
			printf ("%s is missing\n", actualFile.c_str ());
			result = std::max (result, 1);
			continue;
		}
		result = std::max (result, compareImages (expectedFolder + "/" + names[i], actualFile));
	}
	if (maxDiff == 0 && maxBad == 0.0) result = std::max (result, compareFrameCRCs (expectedFolder, actualFolder));
	printf ("Compared %zu images: %s\n", names.size (), result == 0 ? "they match" : "they differ");
	return result;
}

} // namespace

int main (int argc, char ** argv) {
	std::vector<std::string> paths;
	for (int i = 1; i < argc; i ++) {
		std::string a = argv[i];
		if (a.compare (0, 11, "--max-diff=") == 0) maxDiff = atoi (a.c_str () + 11);
		else if (a.compare (0, 10, "--max-bad=") == 0) maxBad = atof (a.c_str () + 10);
		else if (a.compare (0, 6, "--out=") == 0) outFolder = a.substr (6);
		else if (a.compare (0, 2, "--") == 0) {
			fprintf (stderr, "Unknown option %s\n", a.c_str ());
			return 2;
		} else paths.push_back (a);
	}
	if (paths.size () != 2) {
		fprintf (stderr, "Usage: framediff [--max-diff=<n>] [--max-bad=<f>] [--out=<folder>] <expected> <actual>\n");
		return 2;
	}
	if (! outFolder.empty ()) {
#if defined(_WIN32)
		_mkdir (outFolder.c_str ());
#else
		mkdir (outFolder.c_str (), 0777);
#endif
	}
	if (isFolder (paths[0]) && isFolder (paths[1])) return compareFolders (paths[0], paths[1]);
	return compareImages (paths[0], paths[1]);
}
