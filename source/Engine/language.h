#ifndef __LANGUAGE_H__
#define __LANGUAGE_H__

struct settingsStruct
{
	unsigned int languageID;
	unsigned int numLanguages;
	bool userFullScreen;
	unsigned int refreshRate;
	int antiAlias;
	bool fixedPixels;
	bool noStartWindow;
	bool debugMode;
	char renderer[8];	// auto, gpu or gl (see requestedRenderer)
	// The GPU memory that sprite banks no animation uses may keep (see sprbanks.h)
	unsigned int spriteBankCacheBudgetMB;
	// SPRITEEDGES: 1 to extrude the edges of sprites into their gutters,
	// 0 not to, -1 (no key) for what the game says (see spriteEdgesExtruded)
	int spriteEdges;
};

extern settingsStruct gameSettings;

void readIniFile (char * filename);
void saveIniFile (char * filename);
int getLanguageForFileB ();
void makeLanguageTable (FILE * table);

#endif
