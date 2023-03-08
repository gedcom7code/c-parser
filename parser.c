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
        || p->firstChild->id
        || p->firstChild->payloadType == GEDC_PAYLOAD_POINTER
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

static int unCONTCONC(GedStructure *p, size_t *line) {
    if (!p) return 1;
    *line += 1;
    if (!strcmp(p->tag, "CONT") || !strcmp(p->tag, "CONC")) return 0;
    char *nul = 0;
    while (p->firstChild && (!strcmp(p->firstChild->tag, "CONT") || !strcmp(p->firstChild->tag, "CONC"))) {
        int isCONT = !strcmp(p->firstChild->tag, "CONT");
        *line += 1;
        if (p->payloadType == GEDC_PAYLOAD_POINTER
        || p->firstChild->id
        || p->firstChild->payloadType == GEDC_PAYLOAD_POINTER
        || p->firstChild->firstChild) return 0;
        if (p->payloadType == GEDC_PAYLOAD_NONE) {
            p->payloadType = GEDC_PAYLOAD_STRING;
            if (p->firstChild->payloadType == GEDC_PAYLOAD_NONE) {
                if (isCONT) {
                    p->string = p->firstChild->tag;
                    p->string[0] = '\n';
                    p->string[1] = '\0';
                } else {
                    p->payloadType = GEDC_PAYLOAD_NONE;
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


// helpers for parts of line; return length if found, 0 if not found

// lstart handles non-level content before a line starts
static int lstart5(char *s) {
    int i = 0;
    while(s[i]) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\r') i += 1;
        else if (s[i+0] == '\xc2' && s[i+1] == '\xa0') i += 2; // NBSP
        else if (s[i+0] == '\xef' && s[i+1] == '\xbb' && s[i+2] == '\xbf') i += 3; // BOM
        else break;
    }
    return i;
}
static int lstart7(char *s) {
    int i = 0;
    if (s[i+0] == '\xef' && s[i+1] == '\xbb' && s[i+2] == '\xbf') return 3; // BOM
    return i;
}

// level handles level, including converting to integer
static int level1(char *s, int *level) {
    int i = 0; *level = 0;
    while(s[i] >= '0' && s[i] <= '9')
        *level = (*level * 10) + s[i++] - '0';
    return i;
}
static int level7(char *s, int *level) {
    if (*s == '0') { *level = 0; return 1; }
    int i = 0; *level = 0;
    while(s[i] >= '0' && s[i] <= '9')
        *level = (*level * 10) + s[i++] - '0';
    return i;
}

// delim handles mid-line delimiters before the payload
static int delim5(char *s) {
    int i = 0;
    while(s[i]) {
        if (s[i] == ' ' || s[i] == '\t') i += 1;
        else if (s[i+0] == '\xc2' && s[i+1] == '\xa0') i += 2; // NBSP
        else if (s[i+0] == '\xef' && s[i+1] == '\xbb' && s[i+2] == '\xbf') s += 3; // BOM
        else break;
    }
    return i;
}
static int delim7(char *s) {
    return s[0] == ' ' ? 1 : 0;
}

// xref handles xref_id and pointer
static int xref1(char *s) {
    if (s[0] != '@') return 0;
    int i=1;
    if (s[i] == '@' || s[i] == '#') return 0;
    while(s[i] && s[i] != '@' && s[i] != '\r' && s[i] != '\n') i+=1;
    if (s[i] != '@') return 0;
    return i+1;
}
static int xref5(char *s) {
    if (s[0] != '@') return 0;
    int i=1;
    if (!(s[i] == '_') && !(s[i] >= 'A' && s[i] <= 'Z') && !(s[i] >= 'a' && s[i] <= 'z') && !(s[i] >= '0' && s[i] <= '9')) return 0;
    while(s[i] && s[i] != '@' && s[i] != '\r' && s[i] != '\n') {
        if (s[i] >= 'a' && s[i] <= 'z') s[i] += 'A'-'a'; // case normalize
        i+=1;
    }
    if (s[i] != '@') return 0;
    return i+1;
}
static int xref7(char *s) {
    if (s[0] != '@') return 0;
    int i=1;
    while ((s[i] == '_') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9')) i+=1;
    if (i == 1) return 0;
    if (s[i] != '@') return 0;
    // if (i == 5 && s[1] == 'V' && s[2] == 'O' && s[3] = 'I' && s[4] == 'D') return 0; // ok for pointers
    return i+1;
}

// tag handles tags
static int tag1(char *s) {
    if (s[0] == '@') return 0;
    int i=0;
    while (s[i] && s[i] != ' ' && s[i] != '\r' && s[i] != '\n' && s[i] != '\t') i+=1;
    return i;
}
static int tag5(char *s) {
    int i=0;
    while ((s[i] == '_') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9')) {
        if (s[i] >= 'a' && s[i] <= 'z') s[i] += 'A'-'a'; // case normalize
        i+=1;
    }
    return i;
}
static int tag7(char *s) {
    int i=0;
    while ((s[i] == '_') || (s[i] >= 'A' && s[i] <= 'Z') || (i > 0 && s[i] >= '0' && s[i] <= '9')) {
        i+=1;
    }
    return i;
}

// text handles payloads, without escapes
static int text7(char *s) {
    int i=0;
    while(s[i] && s[i] != '\n' && s[i] != '\r') i+=1;
    return i;
}

// eol handles advancing to the next line, including incriminating line number
static int eol1(char *s, size_t *line) {
    int i = text7(s); // skip post-pointer content if any
    int hadone = 0;
    while(s[i]) {
        if (s[i] == '\n') { hadone = 1; *line += 1; i += 1; if (s[i] == '\r') i += 1; }
        else if (s[i] == '\r') { hadone = 1; *line += 1; i += 1; if (s[i] == '\n') i += 1; }
        else break;
        while (s[i] == ' ' || s[i] == '\t') i += 1;
    }
    if (!hadone && s[i]) return 0;
    return i;
}
static int eol5(char *s, size_t *line) {
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i += 1; // skip post-pointer content if any
    int hadone = 0;
    while(s[i]) {
        if (s[i] == '\n') { hadone = 1; *line += 1; i += 1; if (s[i] == '\r') i += 1; }
        else if (s[i] == '\r') { hadone = 1; *line += 1; i += 1; if (s[i] == '\n') i += 1; }
        else break;
        while (s[i] == ' ' || s[i] == '\t') i += 1;
    }
    if (!hadone && s[i]) return 0;
    return i;
}
static int eol7(char *s, size_t *line) {
    if (s[0] == '\r') {*line += 1; return s[1] == '\r' ? 2 : 1;}
    if (s[0] == '\n') {*line += 1; return 1; }
    return 0;
}

#include <stdio.h>

GedStructure *parseGEDCOM(char *input, int dialect, const char **errmsg, size_t *errline) {
    int (*lstart)(char *s);
    int (*level)(char *s, int *level);
    int (*delim)(char *s);
    int (*xref)(char *s);
    int (*tag)(char *s);
    int (*text)(char *s);
    int (*eol)(char *s, size_t *line);

    if (dialect <= 1) {
        lstart = lstart5;
        level = level1;
        delim = delim5;
        xref = xref1;
        tag = tag1;
        text = text7;
        eol = eol1;
    } else if (dialect < 7) {
        lstart = lstart5;
        level = level7;
        delim = delim5;
        xref = xref5;
        tag = tag5;
        text = text7;
        eol = eol5;
    } else {
        lstart = lstart7;
        level = level7;
        delim = delim7;
        xref = xref7;
        tag = tag7;
        text = text7;
        eol = eol7;
    }

    GedStructure *ans = 0;
    size_t line = 1;
    int depth = -1;
    size_t xref_id_count = 0;
    GedStructure *parent = 0;
    GedStructure *sibling = 0;
    
    #define DIE_ERROR(msg) do { \
        freeGedStructure(ans); \
        if (errmsg) *errmsg = msg; \
        if (errline) *errline = line; \
        fprintf(stderr, "[%.20s]\n", c); \
        return 0; \
    } while(0)

    
    char *c = input;
    int tmp;
    while(*c) {
        c += lstart(c);
        int l = -1;
        tmp = level(c, &l); 
        if (!tmp) DIE_ERROR("Missing level");
        c += tmp;
        if (l > depth + 1) DIE_ERROR("Levels cannot skip values");
        while (l < depth + 1) {
            sibling = parent;
            if (parent) parent = parent->parent;
            depth -= 1;
        }
        depth = l;
        
        tmp = delim(c);
        if (dialect > 1 && tmp == 0) DIE_ERROR("Level must be followed by delimiter");
        c += tmp;
        
        char *xref_id = 0;
        tmp = xref(c);
        if (tmp) {
            if (dialect >= 7 && depth > 0)
                DIE_ERROR("Xref_id only allowed on records, not substructures");
            if (dialect >= 7 && tmp == 6 && c[1] == 'V' && c[2] == 'O' && c[3] == 'I' && c[4] == 'D')
                DIE_ERROR("@VOID@ is not allowed as an Xref_id");
            xref_id = c+1;
            c[tmp-1] = 0; // null terminator to slice string
            c += tmp;
            
            tmp = delim(c);
            if (dialect > 1 && tmp == 0) DIE_ERROR("Xref_id must be followed by delimiter");
            c += tmp;
        } else if (c[0] == '@') DIE_ERROR("Invalid Xref_id");
        
        tmp = tag(c);
        if (!tmp) DIE_ERROR("Line without a permitted tag");
        char *t = c;
        c += tmp;
        
        
        char *payload = 0;
        int payloadType = GEDC_PAYLOAD_NONE;
        tmp = delim(c);
        if (tmp) {
            *c = 0; // null terminator to slice string
            c += tmp;
            tmp = xref(c);
            if (tmp) {
                payloadType = GEDC_PAYLOAD_POINTER;
                payload = c+1;
                c[tmp-1] = 0; // null terminator to slice string
                c += tmp;
            } else {
                tmp = text(c);
                if (tmp) {
                    if (c[0] == '@') {
                        if (c[1] == '@') { c += 1; tmp -= 1; }
                        else if (dialect >= 7) DIE_ERROR("Leading @ must be doubled (or be part of valid pointer)");
                        else if (dialect > 1 && c[1] != '#') DIE_ERROR("Leading @ must be doubled (or be part of valid pointer or escape)");
                    }
                    payloadType = GEDC_PAYLOAD_STRING;
                    payload = c;
                    c += tmp;
                } else if (dialect >= 7) DIE_ERROR("Empty payloads must be encoded as no line value");
            }
        }
        tmp = eol(c, &line);
        if (!tmp && *c) DIE_ERROR("Expected line break not found");
        *c = 0; // null terminator to slice string
        c += tmp;

        
        GedStructure *s = calloc(1, sizeof(GedStructure));
        s->tag = t;
        s->id = xref_id;
        s->payloadType = payloadType;
        s->string = payload; // even if pointer in this pass
        s->parent = parent;
        if (sibling) sibling->nextSibling = s;
        else if (parent) parent->firstChild = s;
        parent = s; sibling = 0;
        if (!ans) ans = s;
    }



    // make pointers into pointers
    // 1. make array of records with ids
    GedStructure **array = calloc(xref_id_count, sizeof(GedStructure *));
    size_t i=0;
    if (dialect <= 1) { // allow pointers to substructures
        for(GedStructure *p = ans; p; ) {
            if (p->id) array[i++] = p;
            if (p->firstChild) p = p->firstChild;
            else {
                while (!p->nextSibling && p->parent) p = p->parent;
                p = p->nextSibling;
            }
        }
    } else { // pointers only on records
        for(GedStructure *p = ans; p; p = p->nextSibling) {
            if (p->id) array[i++] = p;
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

    
    // remove CONT (and CONC)
    line = 0;
    if (dialect < 7) { // CONT and CONT
        if (!unCONTCONC(ans, &line))
            DIE_ERROR("Incorrect use of CONT or CONC");
    } else { // just CONT
        if (!unCONT(ans, &line))
            DIE_ERROR("Incorrect use of CONT");
    }

    #undef DIE_ERROR
    
    return ans;

}
