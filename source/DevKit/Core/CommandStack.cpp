#include "CommandStack.h"

namespace devkit {

CommandStack::CommandStack ()
	: position (0), savedKnown (true), savedPosition (0), merging (false) {}

void CommandStack::run (Command * command) {
	command->apply ();
	keep (command);
}

void CommandStack::add (Command * command) {
	keep (command);
}

void CommandStack::keep (Command * command) {
	// What was undone can't be redone after a new change.
	if (position < commands.size ()) {
		if (savedKnown && savedPosition > position) savedKnown = false;
		commands.resize (position);
	}
	// We don't merge into the saved state, so that undo gets back to it.
	bool atSaved = savedKnown && savedPosition == position;
	if (merging && position > 0 && ! atSaved && commands[position - 1]->mergeWith (*command)) {
		delete command;
	} else {
		commands.push_back (std::unique_ptr<Command> (command));
		position ++;
	}
	merging = true;
	if (changed) changed ();
}

std::string CommandStack::undoLabel () const {
	return canUndo () ? commands[position - 1]->label () : std::string ();
}

std::string CommandStack::redoLabel () const {
	return canRedo () ? commands[position]->label () : std::string ();
}

void CommandStack::undo () {
	if (! canUndo ()) return;
	position --;
	commands[position]->revert ();
	merging = false;
	if (changed) changed ();
}

void CommandStack::redo () {
	if (! canRedo ()) return;
	commands[position]->apply ();
	position ++;
	merging = false;
	if (changed) changed ();
}

void CommandStack::clear () {
	if (! (savedKnown && savedPosition == position)) savedKnown = false;
	else savedPosition = 0;
	commands.clear ();
	position = 0;
	merging = false;
	if (changed) changed ();
}

void CommandStack::clearAfterChange () {
	savedKnown = false;
	commands.clear ();
	position = 0;
	merging = false;
	if (changed) changed ();
}

void CommandStack::markSaved () {
	savedKnown = true;
	savedPosition = position;
	merging = false;
}

bool CommandStack::isModified () const {
	return ! savedKnown || savedPosition != position;
}

} // namespace devkit
