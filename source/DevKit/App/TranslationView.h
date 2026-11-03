#ifndef SLUDGE_DEVKIT_TRANSLATION_VIEW_H
#define SLUDGE_DEVKIT_TRANSLATION_VIEW_H

/*
 * The translation editor: the strings in a table that sorts by number,
 * original or translation, shows all of them or those of one kind, and
 * finds text. The translations are edited in the table; Enter keeps one
 * and goes to the next string. The language and loading the strings of
 * the project are in the Inspector.
 */

#include <memory>
#include <string>
#include <vector>

#include "DocumentView.h"
#include "TranslationDocument.h"

namespace devkit {

class WorkspaceActions;

class TranslationView : public DocumentView {
public:
	TranslationView (TranslationDocument * translation, WorkspaceActions & actions);

	Document * document () { return translation; }
	void draw ();
	void drawInspector ();

private:
	void drawToolbar ();
	void drawTable (float height);
	void drawPreview ();
	// The lines that the filter and the search show, in the sorted order.
	void updateShown ();
	void loadStrings (const std::string & projectFile);
	void chooseProject ();
	void say (const std::string & text);

	TranslationDocument * translation;
	WorkspaceActions & actions;

	int filter;				// A TranslationFilter
	std::string search;
	int sortColumn;			// 0 number, 1 original, 3 translation
	bool sortAscending;
	std::vector<int> shown;

	int selected;			// A line, or -1
	// The translation that the user is typing, in the table.
	int editRow;
	std::string editBuffer;
	// The line whose field gets the keyboard next, after Enter.
	int focusRow;

	// The answer of the file dialog, which may come after we are gone.
	std::shared_ptr<std::string> chosenProject;
	std::string notice;
	double noticeUntil;
};

} // namespace devkit

#endif
