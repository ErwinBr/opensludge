# The style of the original SLUDGE help

Base the style on pages that are still as their authors wrote them, not on pages written or rewritten since November 2026. Those newer pages are good at being exact, but they don't sound like the rest of the help. These topic pages are original and good to reread before you write:

- `Introduction_to_SLUDGE_Concepts.html`: explaining an idea to a beginner
- `Scaling_Characters.html`: a how-to with figures
- `The_Worlds_Easiest_Load_Save_Mechanism.html`: a short recipe
- `Using_Audio_in_SLUDGE.html`, `Scrolling_Scenes.html`, `Variables.html`
- Most function pages that start with a lower-case letter, such as `addOverlay.html`, `say_and_think.html` and `setScale.html`

To see whether another page is original, run `git log --format='%an %ad' -- <page>`.

## The voice

- **It talks to the reader as a person.** It uses "you" throughout, and sometimes "I" ("personally, if I had an object called..."). It asks the reader questions ("Confused?", "So why give each object an internal name and an on-screen name?") and answers them.
- **It is relaxed and a little funny, and the facts are still right.** A function is a "useful little blighter", and the compiler, being kind, lets you use names. A figure has a caption with a joke ("Welcome to the Tutorial City Art Gallery"). The jokes are short and never get in the way of the instructions. One or two per page is enough. A function page needs none.
- **It meets beginners where they are.** It starts from what the reader probably thinks ("if you're still thinking in terms of 'I want the character to be 100% here'") and goes from there to how SLUDGE does it. It admits when something is rough ("Finding a horizon isn't an exact science.").
- **It explains why.** A rule usually comes with its reason, often a funny one: put the name in speech marks, "otherwise the compiler will try and turn the name into code and get very confused".
- **It gives the simple way first.** Show the version that works, then what else is possible: "Easy! Of course, you might want to do more than just..."
- **It uses British English**: colour, centre, behaviour, initialise, "speech marks", "brackets". Where the engine accepts both spellings, give the other one in brackets: "CENTRE (or CENTER)".

## Function pages are plainer

Function pages are reference pages, so they're shorter and drier than topic pages:

- Start the purpose with a verb that says what the function does: "Loads an image from...", "Adds a character on the screen which represents...".
- Name every parameter in `<i>italics</i>` and say what it means, including special values such as CENTRE.
- Mention the catch that people will run into, with an exclamation mark if it's a surprise: "(except for 32-bit images, where the alpha channel is used!)".
- Keep the example short and complete. It's often a `sub init ()` with the call in it.

## Layout

- `<H3>` headings that end with a colon. Each paragraph is a `<P>` of its own.
- Code in `<pre>`, with three spaces of indent inside braces, as in the existing examples. Write `&quot;` for quote marks in examples, as the original pages do.
- Name menus, menu items and buttons in `<b>bold</b>`.
- Figures as `<IMG SRC="images/...">` in a paragraph of their own, with a short caption below when the text refers to them ("Fig. 1: ...").
- Name other pages in the text by their titles, and link them: `<a href="File_Handling.html">File Handling</a>`.

## Updating an original page

Change only what the code made wrong, and keep the rest of the author's text and voice. If a step is now different (a tool that has moved into the Dev Kit, a menu item that has a new name), rewrite that step in the same style. Don't rewrite whole paragraphs that are still right. Notes such as "On Mac, ..." or "On other platforms, ..." about old programs can go once the new Dev Kit works the same everywhere. Say so to the user when you remove them.
