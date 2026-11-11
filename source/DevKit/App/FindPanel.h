#ifndef SLUDGE_DEVKIT_FIND_PANEL_H
#define SLUDGE_DEVKIT_FIND_PANEL_H

/*
 * The Find in Project panel: a text in every script of the project, with
 * each place in a list that opens its script there.
 */

#include <string>
#include <vector>

#include "ProjectSearch.h"

namespace devkit {

class ProjectDocument;
class WorkspaceActions;

class FindPanel {
public:
	explicit FindPanel (WorkspaceActions & actions);

	// Draw the contents of the panel. openText gives the text of the open
	// scripts.
	void draw (ProjectDocument * project, OpenText openText);
	// Give the field the keyboard in the next frame, with a text to find
	// when it isn't empty.
	void focus (const std::string & text);
	// Find what the field has, as Enter does.
	void search (ProjectDocument * project, OpenText openText);

	const std::vector<SearchHit> & hits () const { return results; }

private:
	WorkspaceActions & actions;
	std::string wanted, searched;
	editing::FindOptions options;
	std::vector<SearchHit> results;
	std::vector<std::string> unread;
	bool more, focusField, searchedOnce;
	int selected;
};

} // namespace devkit

#endif
