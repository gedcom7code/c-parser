/**
 * A C implementation of parsing a FamilySearch GEDCOM 7 file into a tree.
 * Depends on
 * 
 * stdlib.h -- calloc, free, qsort, bsearch
 * string.h -- strcmp
 * 
 * Requires the entire file to be read into memory as a single mutable string.
 * Reuses that memory to store tags, ids, and payloads so there is very little
 * wasted space from this requirement.
 * 
 * Uses '\n' (U+000A) as the line break character CONT adds to payloads.
 * 
 * Does not (yet) perform any tag-aware operations except CONT:
 * no SCHMA handling, no requiring HEAD and TRLR, etc.
 */

#include <stdlib.h>
#include <string.h>

#define GEDC_PAYLOAD_NONE 0
#define GEDC_PAYLOAD_STRING 1
#define GEDC_PAYLOAD_POINTER 2

typedef struct t_GedStructure {
    char *tag;
    char *id;
    int payloadType;
    union {
        char *string;
        struct t_GedStructure *pointer;
    };
    struct t_GedStructure *firstChild;
    struct t_GedStructure *nextSibling;
    struct t_GedStructure *parent;
} GedStructure;

/**
 * Aggressive free: this, its kids, and its siblings (but not its parents)
 */
void freeGedStructure(GedStructure *g);

/**
 * Leaves character data in the input array,
 * inserting null bytes to terminate strings;
 * allocates records on the heap (with `malloc`)
 * and returns the first (others available via firstChild and nextSibling).
 * 
 * Given an invalid file
 * - returns NULL and allocates nothing
 * - if errmsg is not NULL, sets *errmsg to a string describing the error type
 * - if errline is not NULL, sets *errline to the line number of the error
 * 
 * dialect should be an integer
 * 
 * - 1 or less will do the most forgiving parse possible
 * - 2-5 will allow CONC, multi-space delimiters, blank lines, indentation, v5.5 tag andxref characters
 * - 7 will enforce v7 syntactic rules
 */
GedStructure *parseGEDCOM(char *input, int dialect, const char **errmsg, size_t *errline);

