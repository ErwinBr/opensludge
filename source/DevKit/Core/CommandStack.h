#ifndef SLUDGE_DEVKIT_COMMAND_STACK_H
#define SLUDGE_DEVKIT_COMMAND_STACK_H

/*
 * Undo and redo. Every change that the user makes to a document is a
 * Command, which the document's CommandStack applies and keeps, so that it
 * can revert it again. The stack also knows whether the document has
 * changed since it was saved.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace devkit {

class Command {
public:
	virtual ~Command () {}

	// What the menu says, after "Undo" or "Redo".
	virtual std::string label () const = 0;
	// Make the change, the first time and to redo it.
	virtual void apply () = 0;
	// Undo the change.
	virtual void revert () = 0;
	// Take in a command that follows this one, such as the next step of a
	// drag, so that they undo together. Returns false when it can't.
	virtual bool mergeWith (const Command & next) { (void) next; return false; }
};

// A change from one value of some state to another, such as a hotspot or a
// whole floor: apply sets the new value and revert the old one. Commands
// with the same mergeKey (other than 0) that follow each other merge, and
// keep the first old value.
template <typename T>
class ValueCommand : public Command {
public:
	ValueCommand (const std::string & label, const T & before, const T & after,
			std::function<void (const T &)> set, int mergeKey = 0)
		: text (label), before (before), after (after), set (set), key (mergeKey) {}

	std::string label () const { return text; }
	void apply () { set (after); }
	void revert () { set (before); }
	bool mergeWith (const Command & next) {
		const ValueCommand * other = dynamic_cast<const ValueCommand *> (&next);
		if (! other || ! key || other->key != key) return false;
		after = other->after;
		return true;
	}

private:
	std::string text;
	T before, after;
	std::function<void (const T &)> set;
	int key;
};

class CommandStack {
public:
	CommandStack ();

	// Apply a command and keep it, merged with the last one if they merge.
	// Commands that were undone are gone then.
	void run (Command * command);
	// Keep a command whose change was already made.
	void add (Command * command);
	// Don't merge the next command with the last one, for example at the
	// end of a drag.
	void stopMerging () { merging = false; }

	bool canUndo () const { return position > 0; }
	bool canRedo () const { return position < commands.size (); }
	std::string undoLabel () const;
	std::string redoLabel () const;
	void undo ();
	void redo ();

	// Forget every command, after a change that we can't undo. A document
	// that wasn't saved stays changed.
	void clear ();

	// The document was saved, or loaded, as it is now.
	void markSaved ();
	bool isModified () const;

	// Called after every change, undo and redo.
	std::function<void ()> changed;

private:
	void keep (Command * command);

	std::vector<std::unique_ptr<Command> > commands;
	size_t position;		// The commands before it are applied
	bool savedKnown;		// Whether we can get back to the saved state
	size_t savedPosition;
	bool merging;
};

} // namespace devkit

#endif
