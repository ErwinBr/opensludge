/*
 * Tests of the conversion of text from the Windows-1250 code page, in which
 * games made before SLUDGE 2.2 store their text, to UTF-8
 * (source/Engine/cp1250.cpp). The expected characters are those of
 * Microsoft's table, as in Python's cp1250 codec.
 */

#include <stdio.h>
#include <string.h>

#include "cp1250.h"

// The Unicode characters of the bytes 0x80 to 0xFF, or 0 where CP1250 has
// no character. Below 0x80, CP1250 is ASCII.
static const unsigned int upperHalf[128] = {
	0x20AC, 0, 0x201A, 0, 0x201E, 0x2026, 0x2020, 0x2021,
	0, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

// Sentences in CP1250, and in UTF-8.
static const char * const sentences[][2] = {
	// Czech
	{ "P\xf8\xedli\x9a \x9elu\x9dou\xe8k\xfd k\xf9\xf2 \xfap\xecl \xef\xe1" "belsk\xe9 \xf3" "dy",
	  "P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd k\xc5\xaf\xc5\x88 \xc3\xbap\xc4\x9bl \xc4\x8f\xc3\xa1" "belsk\xc3\xa9 \xc3\xb3" "dy" },
	// Polish
	{ "Za\xbf\xf3\xb3\xe6 g\xea\x9cl\xb9 ja\x9f\xf1",
	  "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84" },
	// Quotation marks, a dash and the euro sign
	{ "\x84" "Ahoj\x93 \x96 5 \x80",
	  "\xe2\x80\x9e" "Ahoj\xe2\x80\x9c \xe2\x80\x93 5 \xe2\x82\xac" },
	{ "", "" },
};

static int failures = 0;

static void check (const char * input, const char * expected, const char * what) {
	char * result = convertCP1250ToUTF8 (input);
	if (! result) {
		fprintf (stderr, "FAIL: %s: the conversion failed\n", what);
		failures ++;
		return;
	}
	if (strcmp (result, expected)) {
		fprintf (stderr, "FAIL: %s: got \"%s\", expected \"%s\"\n", what, result, expected);
		failures ++;
	}
	delete [] result;
}

// Write a character as UTF-8, with a zero after it.
static void toUTF8 (unsigned int c, char * out) {
	if (c < 0x80) {
		*out ++ = (char) c;
	} else if (c < 0x800) {
		*out ++ = (char) (0xC0 | (c >> 6));
		*out ++ = (char) (0x80 | (c & 0x3F));
	} else {
		*out ++ = (char) (0xE0 | (c >> 12));
		*out ++ = (char) (0x80 | ((c >> 6) & 0x3F));
		*out ++ = (char) (0x80 | (c & 0x3F));
	}
	*out = 0;
}

int main () {
	char input[2], expected[4], what[32];
	input[1] = 0;
	for (unsigned int b = 1; b < 256; b ++) {
		unsigned int c = b < 0x80 ? b : upperHalf[b - 0x80];
		if (! c) continue;
		input[0] = (char) b;
		toUTF8 (c, expected);
		snprintf (what, sizeof (what), "byte 0x%02X", b);
		check (input, expected, what);
	}
	for (int i = 0; i < (int) (sizeof (sentences) / sizeof (sentences[0])); i ++) {
		snprintf (what, sizeof (what), "sentence %d", i + 1);
		check (sentences[i][0], sentences[i][1], what);
	}

	if (failures) {
		fprintf (stderr, "%d failures\n", failures);
		return 1;
	}
	printf ("All conversions are right.\n");
	return 0;
}
