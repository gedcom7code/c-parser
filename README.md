A GEDCOM parsers in C11 (could be backported to other C standards by removing the anonymous union).
Enforces lexical and syntactic constraints of the [specification](https://gedcom.io/specifications/FamilySearchGEDCOMv7.html),
including resolving all pointers.
Removes CONT pseudo-structures, using `\n` U+000A as line breaks in payloads.
Does not check or enforce any other structure-specific constraints
such as the the presence of `HEAD` and `TRLR`, `SCHMA`, datatypes, etc.

The main function, `parseGEDCOM`, requires as input a single mutable array containing the entire GEDCOM file
and returns the encoded DOM tree using a first-child next-sibling encoding of the data tree, with parent pointers.
The provided `parser_test.c` shows an example of using this:
it reads any number of .ged files from the command line, parses them, then re-serializes them and prints them to stdout.

Requiring the entire file to be read into a single array may seem wasteful,
but this code re-uses almost all of that memory to store tags, ids, and payloads
resulting in both time and space savings over a streaming parser.
For applications that handle the data line by line without the need to revisit previous lines, a streaming parser could be more efficient.
