/**
 * A C implementation of parsing a FamilySearch GEDCOM 7 file into a tree.
 * Uses C11's anonymous union feature.
 */

#include "parser.h"

#include <stdlib.h>
#include <string.h>


/**
 * Aggressive free: this, its kids, and its siblings (but not its parents)
 */
void freeGedStructure(GedStructure *g) {
    if (!g) return;
    freeGedStructure(g->firstChild); g->firstChild = 0;
    freeGedStructure(g->nextSibling); g->nextSibling = 0;
    free(g);
}


/** Figure out a structure's line number. Inefficient, but only used in error reporting. */
static size_t lineOfStructure(GedStructure *s, GedStructure *head) {
    if (!s || !head) return 0;
    size_t ans = 1;
    GedStructure *p = head;
    while(p && p != s) {
        ans += 1;
        if (p->firstChild) p = p->firstChild;
        else {
            while (p && !p->nextSibling) p = p->parent;
            if (p) p = p->nextSibling;
        }
    }
    if (!p) return 0;
    return ans;
}



static int xrefCmp(const void *_a, const void *_b) {
    const GedStructure **a = (const GedStructure **)_a, **b = (const GedStructure **)_b;
    return strcmp((*a)->id, (*b)->id);
}
static int fixPointers(GedStructure *p, GedStructure **targs, size_t tlen, size_t *line) {
    if (!p) return 1;
    *line += 1;
    if (p->payloadType == GEDC_PAYLOAD_POINTER) {
        if (strcmp(p->string, "VOID")) {
            GedStructure dummy; dummy.id = p->string;
            GedStructure *key = &dummy;
            GedStructure **found = bsearch(&key, targs, tlen, sizeof(GedStructure *), xrefCmp);
            if (!found) return 0;
            p->pointer = *found;
        } else {
            p->pointer = 0;
        }
    }
    return fixPointers(p->firstChild, targs, tlen, line) 
        && fixPointers(p->nextSibling, targs, tlen, line);
}
static int unCONT(GedStructure *p, size_t *line) {
    if (!p) return 1;
    *line += 1;
    if (!strcmp(p->tag, "CONT")) return 0;
    char *nul = 0;
    while (p->firstChild && !strcmp(p->firstChild->tag, "CONT")) {
        *line += 1;
        if (p->payloadType == GEDC_PAYLOAD_POINTER
        || p->id
        || p->payloadType == GEDC_PAYLOAD_POINTER
        || p->firstChild->firstChild) return 0;
        if (p->payloadType == GEDC_PAYLOAD_NONE) {
            p->payloadType = GEDC_PAYLOAD_STRING;
            if (p->firstChild->payloadType == GEDC_PAYLOAD_NONE) {
                p->string = p->firstChild->tag;
                p->string[0] = '\n';
                p->string[1] = '\0';
            } else {
                p->string = p->firstChild->string - 1;
                p->string[0] = '\n';
            }
        } else {
            if (!nul) { nul=p->string; while(*nul) nul+=1; }
            if (p->firstChild->payloadType == GEDC_PAYLOAD_NONE) {
                *(nul++) = '\n';
                *nul = '\0';
            } else {
                // strcat, but strcat doesn't allow overlap
                // also optimized for repeated calls, unlike strcat
                char *src = p->firstChild->string;
                *(nul++) = '\n';
                while(*src) *(nul++) = *(src++);
                *nul = '\0';
            }
        }
        GedStructure *delme = p->firstChild;
        p->firstChild = p->firstChild->nextSibling;
        free(delme);
    }
    return unCONT(p->firstChild, line)
        && unCONT(p->nextSibling, line);
}

#ifdef FORGIVING
static int unCONTCONC(GedStructure *p, size_t *line) {
    if (!p) return 1;
    *line += 1;
    if (!strcmp(p->tag, "CONT") || !strcmp(p->tag, "CONC")) return 0;
    char *nul = 0;
    while (p->firstChild && (!strcmp(p->firstChild->tag, "CONT") || !strcmp(p->firstChild->tag, "CONC"))) {
        int isCONT = !strcmp(p->firstChild->tag, "CONT");
        *line += 1;
        if (p->payloadType == GEDC_PAYLOAD_POINTER
        || p->id
        || p->payloadType == GEDC_PAYLOAD_POINTER
        || p->firstChild->firstChild) return 0;
        if (p->payloadType == GEDC_PAYLOAD_NONE) {
            p->payloadType = GEDC_PAYLOAD_STRING;
            if (p->firstChild->payloadType == GEDC_PAYLOAD_NONE) {
                if (isCONT) {
                    p->string = p->firstChild->tag;
                    p->string[0] = '\n';
                    p->string[1] = '\0';
                }
            } else {
                p->string = p->firstChild->string - isCONT;
                if (isCONT) p->string[0] = '\n';
            }
        } else {
            if (!nul) { nul=p->string; while(*nul) nul+=1; }
            if (p->firstChild->payloadType == GEDC_PAYLOAD_NONE) {
                if (isCONT) {
                    *(nul++) = '\n';
                    *nul = '\0';
                }
            } else {
                // strcat, but strcat doesn't allow overlap
                // also optimized for repeated calls, unlike strcat
                char *src = p->firstChild->string;
                if (isCONT) *(nul++) = '\n';
                while(*src) *(nul++) = *(src++);
                *nul = '\0';
            }
        }
        GedStructure *delme = p->firstChild;
        p->firstChild = p->firstChild->nextSibling;
        free(delme);
    }
    return unCONTCONC(p->firstChild, line)
        && unCONTCONC(p->nextSibling, line);
}
#endif

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
 */
GedStructure *parseGEDCOM(char *input, const char **errmsg, size_t *errline) {
    GedStructure *ans = 0;
    size_t line = 1;

#define IS_TAGCHAR(c) (c == '_' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
#define IS_TAGSTART(c) (c == '_' || (c >= 'A' && c <= 'Z'))
#define IS_DIGIT(c) (c >= '0' && c <= '9')

#define DIE_ERROR(msg) do { \
    freeGedStructure(ans); \
    if (errmsg) *errmsg = msg; \
    if (errline) *errline = line; \
    return 0; \
} while(0)

    if (*input == '\xef') {
        input += 1;
        if (*input != '\xbb') DIE_ERROR("First character neither BOM nor 0");
        input += 1;
        if (*input != '\xbf') DIE_ERROR("First character neither BOM nor 0");
        input += 1;
    }
    
    int depth = -1;
    size_t xref_id_count = 0;
    GedStructure *parent = 0;
    GedStructure *sibling = 0;
    
    char *c = input;
    while(*c) {
        // parse level
        if (!IS_DIGIT(*c)) DIE_ERROR("Every line must begin with a number");
        int level = *c-'0';
        while (IS_DIGIT(*(++c))) { 
            if (!level) DIE_ERROR("Zero padding not permitted on levels");
            level = level*10 + *c-'0';
        }
        if (level > depth + 1) DIE_ERROR("Levels cannot skip values");
        while (level < depth + 1) {
            sibling = parent;
            if (parent) parent = parent->parent;
            depth -= 1;
        }
        depth = level;
        
        // delimiter
        if (*c != ' ') DIE_ERROR("Level must be followed by space");
        c += 1;
        
        // xref_id
        char *xref_id = 0;
        if (*c == '@') {
            c += 1;
            xref_id = c;
            while(IS_TAGCHAR(*c)) c += 1;
            if (c == xref_id) DIE_ERROR("Xref_ID must not be empty");
            if (*c != '@') DIE_ERROR("Xref_ID must be terminated with @");
            *c = '\0'; // null terminate xref_id
            if (!strcmp(xref_id, "VOID")) DIE_ERROR("Xref_ID cannot be @VOID@");
            c += 1;
            if (level != 0) DIE_ERROR("Xref_ID only allowed for records, not substructures");
            xref_id_count += 1;

            // delimiter
            if (*c != ' ') DIE_ERROR("Xref_ID must be followed by space");
            c += 1;
            
        }
        
        // tag
        char *tag = c;
        if (!IS_TAGSTART(*c)) DIE_ERROR("Tag must start with _ or A-Z");
        c += 1;
        while (IS_TAGCHAR(*c)) c += 1;
        if (*tag == '_' && c == tag + 1) DIE_ERROR("Extension tags must be at least two characters");
        
        // payload
        char *payload = 0;
        int payloadType = GEDC_PAYLOAD_NONE;
        if (*c == ' ') {
            *c = '\0'; // null terminate tag
            c += 1;
            
            if (*c == '@' && *(c+1) != '@') { // pointer
                c += 1;
                payload = c;
                payloadType = GEDC_PAYLOAD_POINTER;
                while (IS_TAGCHAR(*c)) c += 1;
                if (*c != '@') DIE_ERROR("Expected pointer on line opened with @; did you mean @@?");
                if (c == payload) DIE_ERROR("Pointer must not be empty");
                *c = '\0'; // null terminate pointer
                c += 1;
                if (*c && *c != '\n' && *c != '\r')
                    DIE_ERROR("Found text on line after pointer");
            } else {
                if (*c == '@' && *(c+1) == '@') c += 1;
                payload = c;
                payloadType = GEDC_PAYLOAD_STRING;
                while (*c && *c != '\n' && *c != '\r') c+=1;
                if (payload == c) DIE_ERROR("Empty payloads not permitted");
            }
        }
        
        GedStructure *s = calloc(1, sizeof(GedStructure));
        s->tag = tag;
        s->id = xref_id;
        s->payloadType = payloadType;
        s->string = payload; // even if pointer in this pass
        s->parent = parent;
        if (sibling) sibling->nextSibling = s;
        else if (parent) parent->firstChild = s;
        parent = s; sibling = 0;
        
        if (!ans) ans = s;
        
        if (*c == '\n') {
            *c = 0;
            c += 1;
            line += 1;
        } else if (*c == '\r') {
            *c = 0;
            c += 1;
            if (*c == '\n') c += 1;
            line += 1;
        } else if (*c) DIE_ERROR("Found characters on line after end of line. This error is not supposed to be possible; please report it to the developer.");
    }
    
#undef IS_DIGIT
#undef IS_TAGSTART
#undef IS_TAGCHAR

    // make pointers into pointers
    // 1. make array of records with ids
    GedStructure **array = calloc(xref_id_count, sizeof(GedStructure *));
    size_t i=0;
    for(GedStructure *p = ans; p; p = p->nextSibling) {
        if (p->id) array[i++] = p;
    }
    // 2. sort array by id
    qsort(array, i, sizeof(GedStructure *), xrefCmp);
    for(size_t j=1; j<i; j+=1)
        if (strcmp(array[j]->id,array[j-1]->id) == 0) {
            line = lineOfStructure(array[j], ans);
            DIE_ERROR("Duplicate ID");
        }
    // 3. iterate entire forest, looking up IDs
    line = 0;
    if (!fixPointers(ans, array, i, &line))
        DIE_ERROR("Pointer with no target");
    free(array);

    
    // remove CONT
    line = 0;
    if (!unCONT(ans, &line))
        DIE_ERROR("Incorrect use of CONT");

#undef DIE_ERROR

    return ans;
}

#ifdef FORGIVING
/**
 * like parseGEDCOM, by tries to guess what was meant when given malformed data
 * instead of halting with an error. This may result in it returning data with
 * illegal characters in tags and IDs and in skipping non-GEDCOM lines.
 */
GedStructure *parseGEDCOMForgivingly(char *input, const char **errmsg, size_t *errline) {
    GedStructure *ans = 0;

#define NON_EOL(c) (c && c != '\n' && c != '\r')
#define IS_DIGIT(c) (c >= '0' && c <= '9')
#define EAT_SPACE(s) do { \
    if (*s == ' ' || *s == '\t') s += 1; \
    else if (s[0] == '\xc2' && s[1] == '\xa0') s += 2; \
    else if (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf') s += 3; \
    else break; \
} while (1)
#define EAT_WHITE(s) do { \
    if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\t' || *s == '\v') s += 1; \
    else if (s[0] == '\xc2' && s[1] == '\xa0') s += 2; \
    else if (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf') s += 3; \
    else break; \
} while (1)
#define SPACE_WIDTH(s) ((*s == ' ' || *s == '\t') ? 1 : \
    (s[0] == '\xc2' && s[1] == '\xa0') ? 2 : \
    (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf') ? 3 : 0)
#define EAT_NONSPACE(s) do { \
    if ((!*s || *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') \
    || (s[0] == '\xc2' && s[1] == '\xa0') \
    || (s[0] == '\xef' && s[1] == '\xbb' && s[2] == '\xbf')) break; \
    s += 1; \
} while (1)
#define SKIP_LINE(s) do { \
    while (*s && *s != '\n' && *s != '\r') s += 1; \
    if (*s == '\r') {*s = 0; s += 1; } \
    if (*s == '\n') {*s = 0; s += 1; } \
} while(0)

    int depth = -1;
    size_t xref_id_count = 0;
    GedStructure *parent = 0;
    GedStructure *sibling = 0;
    
    char *c = input;
    while(*c) {
        EAT_WHITE(c);
        if (!*c) break;
        // parse level
        if (!IS_DIGIT(*c)) { SKIP_LINE(c); continue; } // no level
        int level = 0;
        while (IS_DIGIT(*c)) level = level*10 + *(c++)-'0';
        if (level > depth + 1) { SKIP_LINE(c); continue; } // too deep
        while (level < depth + 1) {
            sibling = parent;
            if (parent) parent = parent->parent;
            depth -= 1;
        }
        depth = level;
        
        EAT_SPACE(c);
        
        // xref_id
        char *xref_id = 0;
        if (*c == '@') {
            c += 1;
            xref_id = c;
            while(*c != '@' && NON_EOL(*c)) c += 1;
            if (c == xref_id) { SKIP_LINE(c); continue; } // empty xref_id
            if (*c != '@') { SKIP_LINE(c); continue; } // unterminated xref_id
            *c = '\0'; // null terminate xref_id
            if (!strcmp(xref_id, "VOID")) xref_id = 0;
            c += 1;
            xref_id_count += 1;

            EAT_SPACE(c);
        }
        
        // tag
        char *tag = c;
        if (!NON_EOL(*c) || *c == '@') { SKIP_LINE(c); continue; }; // no tag
        c += 1;
        EAT_NONSPACE(c);
        
        // payload
        char *payload = 0;
        int payloadType = GEDC_PAYLOAD_NONE;
        if (SPACE_WIDTH(c) > 0) {
            int tmp = SPACE_WIDTH(c);
            *c = '\0'; // null terminate tag
            c += tmp;

            payload = c;
            int atCount = 0;
            while (*c && *c != '\n' && *c != '\r') {
                atCount += *c == '@';
                c+=1;
            }
            if (payload == c) {
                payloadType = GEDC_PAYLOAD_NONE;
                payload = 0; // empty = none
            } else if (atCount == 2 && c > payload+2 && payload[0] == '@' && c[-1] == '@') { // fix me: make more forgiving by stripping spaces
                payloadType = GEDC_PAYLOAD_POINTER;
                payload += 1;
                c[-1] = 0;
            } else {
                payloadType = GEDC_PAYLOAD_STRING;
                if (payload[0] == '@' && payload[1] == '@') payload += 1;
            }
        }
        
        GedStructure *s = calloc(1, sizeof(GedStructure));
        s->tag = tag;
        s->id = xref_id;
        s->payloadType = payloadType;
        s->string = payload; // even if pointer in this pass
        s->parent = parent;
        if (sibling) sibling->nextSibling = s;
        else if (parent) parent->firstChild = s;
        parent = s; sibling = 0;
        
        if (!ans) ans = s;
        SKIP_LINE(c);
    }

#undef NON_EOL
#undef IS_DIGIT
#undef EAT_SPACE
#undef EAT_WHITE
#undef SPACE_WIDTH
#undef EAT_NONSPACE
#undef SKIP_LINE

#define DIE_ERROR(msg) do { \
    freeGedStructure(ans); \
    if (errmsg) *errmsg = msg; \
    if (errline) *errline = line; \
    return 0; \
} while(0)

    size_t line;

    // make pointers into pointers
    // 1. make array of structures with ids
    GedStructure **array = calloc(xref_id_count, sizeof(GedStructure *));
    size_t i=0;
    for(GedStructure *p = ans; p; ) {
        if (p->id) array[i++] = p;
        if (p->firstChild) p = p->firstChild;
        else {
            while (!p->nextSibling && p->parent) p = p->parent;
            p = p->nextSibling;
        }
    }
    // 2. sort array by id
    qsort(array, i, sizeof(GedStructure *), xrefCmp);
    for(size_t j=1; j<i; j+=1)
        if (strcmp(array[j]->id,array[j-1]->id) == 0) {
            line = lineOfStructure(array[j], ans);
            DIE_ERROR("Duplicate ID");
        }
    // 3. iterate entire forest, looking up IDs
    line = 0;
    if (!fixPointers(ans, array, i, &line))
        DIE_ERROR("Pointer with no target");
    free(array);

    
    // remove CONT and CONC
    line = 0;
    if (!unCONTCONC(ans, &line))
        DIE_ERROR("Incorrect use of CONT or CONC");

#undef DIE_ERROR

    return ans;
}
#endif
