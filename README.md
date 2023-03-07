A GEDCOM parsers in C11 (could be backported to other C standards by removing the anonymous union).
Only works for ASCII-compatible character sets.

Supports three different "dialects" of the GEDCOM object model:

- **7** enforces FamilySearch GEDCOM 7 syntax, including
    - no indentation or blank lines
    - single space delimiters
    - no `CONC` tags
    - no empty line values
    - xref as `[_A-Z0-9]+` but not `VOID`
    - tag as `[_A-Z][_A-Z0-9]*` but not `_`
    - level without leading zeros
    - leading @ must be doubled
    - xrefs only on records, not substructures

- **5** enforces GEDCOM 5.5 syntax, including
    - xref as `[_A-Za-z0-9][^@\r\n]*`, case-folded to upper case during parsing
    - tag as `[_A-Za-z0-9]+`, case-folded to upper case during parsing
    - level without leading zeros
    - leading @ must be doubled it not part of an escape sequence
    - xrefs anywhere but pointers only to records, not substructures

- **1** tries to parse anything, including missing delimiters and strange characters in xrefs and tags

All dialects 

- Removes CONT pseudo-structures, using `\n` U+000A as line breaks in payloads.
- Undouble leading @@ in text payloads.
- Represent empty payloads as a null pointer, not as a pointer to an empty string.
- Resolves all xref_id pointers into C pointers and errors if this is not possible.

Pointers excepted, this is a syntax-only parser.
It dos not check or enforce any other structure-specific constraints
such as the the presence of `HEAD` and `TRLR`, `SCHMA`, datatypes, etc.

The main function, `parseGEDCOM`, requires as input a single mutable array containing the entire GEDCOM file
and returns the encoded DOM tree using a first-child next-sibling encoding of the data tree, with parent pointers.
The provided `parser_test.c` shows an example of using this:
it reads any number of .ged files from the command line, parses them, then re-serializes them and prints them to stdout.

Requiring the entire file to be read into a single array may seem wasteful,
but this code re-uses almost all of that memory to store tags, ids, and payloads
resulting in both time and space savings over a streaming parser.
For applications that handle the data line by line without the need to revisit previous lines, a streaming parser could be more efficient.

