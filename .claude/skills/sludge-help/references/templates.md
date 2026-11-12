# Page templates

These templates are based on original pages (`addOverlay.html`, and `About_the_SLUDGE_Floor_Maker.html` before November 2026). Replace everything in `[brackets]`.

## Function page

Keep the headings exactly as they are here: `make_script_catalog` finds the sections by "Syntax:", "Purpose:" and "Return value:". The syntax goes in one `<pre class="syntax">` with the parameters in `<i>`. A function with more than one form (different parameters) gets one line per form in the same `<pre>`. A pair page (two functions, see SKILL.md) gets one `<pre>` for each function.

```html
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
<TITLE>[functionName]</TITLE>
  <link rel="stylesheet" type="text/css" href="style.css" />
</head>
<BODY>
<HR>
<div align="center"><img id="headerGraphic" src="images/sludge300.png" alt="SLUDGE"/></div>
<h2>[functionName]</h2>
<HR>

<H3>Syntax:</H3>

<P>
<pre class="syntax">[functionName] (<i>[firstParameter, secondParameter]</i>);</pre>
</P>

<H3>Purpose:</H3>

<P>
[What it does, with each parameter in <i>italics</i> where the text names it. One idea per paragraph.]
</P>

<H3>Return value:</H3>

<P>
[What it returns, or "No return value."]
</P>

<H3>Example:</H3>

<P>
<pre>sub init () {
   [functionName] ([...]);
}</pre>
</P>

<H3>See also:</H3>

<P>
<a href="[Related_Topic].html">[Related Topic]</a>
</P>

<P>
<a href="[otherFunction].html">[otherFunction]</a>
</P>

<P class="copyright-notice">SLUDGE and this SLUDGE documentation are <A HREF="Copyright.html">copyright</A> Hungry Software and contributors 2000-[current year]
</P>

<HR>
</BODY>
</html>
```

Notes:

- Return values: the original pages say "No return value." for functions that return nothing. Otherwise describe the value, for example "Returns TRUE if ..., otherwise FALSE." Name constants in capitals, as on `Built-in_Constants.html`.
- Each "See also" link is a paragraph of its own.

## Tool page (About_the_SLUDGE_[Tool].html)

```html
[same head and header as above, with the title "About the SLUDGE [Tool]"]

<H3>[What a ... is]:</H3>

<P>
[One or two paragraphs on what the tool makes, and the idea behind it, before any buttons.]
</P>

<H3>Creating a [thing]:</H3>

<P>
[The steps in the order someone does them. Name menus and buttons in <b>bold</b>, as the program shows them.]
</P>

<P>
<IMG SRC="images/devkit-[tool].png" alt="[What the screenshot shows]">
</P>

<H3>[The next task]:</H3>

<P>
[...]
</P>

[footer as above]
```

## Topic page

The same head, header and footer. In between, prose under `<H3>` headings that end with a colon, with code in `<pre>`, and links to the function pages the topic uses.

## Entries in the listings

Contents (`doc/Table of Contents.hhc`). Use tabs for indenting, one level deeper than the section's own entry:

```html
				<LI> <OBJECT type="text/sitemap">
					<param name="Name" value="[functionName]">
					<param name="Local" value="SLUDGEDevKitHelp\[functionName].html">
					</OBJECT>
```

Index (`doc/SLUDGE Dev Kit Help.hhk`):

```html
	<LI> <OBJECT type="text/sitemap">
		<param name="Name" value="[functionName]">
		<param name="Name" value="[functionName] (built-in function)">
		<param name="Local" value="SLUDGEDevKitHelp\[functionName].html">
		</OBJECT>
```

Web menu (`doc/SLUDGEDevKitHelp/HtmlMenu.html`):

```html
					<li><a href="[functionName].html" target="Frame">[functionName]</a></li>
```
