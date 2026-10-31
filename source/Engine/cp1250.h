#ifndef SLUDGE_CP1250_H
#define SLUDGE_CP1250_H

// Convert text in the Windows-1250 code page, in which games made before
// SLUDGE 2.2 store their text, to UTF-8. Returns a new string, which the
// caller frees with delete [], or NULL when the conversion fails.
char * convertCP1250ToUTF8 (const char * s);

#endif
