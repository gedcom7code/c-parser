/**
 * A C implementation of parsing a FamilySearch GEDCOM 7 file into a tree.
 */

#include "parser.h"

#include <stdlib.h>
#include <stdio.h>

static int dialect = 7;

void dumpStructure(GedStructure *s, int level) {
    if (!s) return;
    printf("%d", level);
    if (s->id) printf(" @%s@", s->id);
    printf(" %s", s->tag);
    if (s->payloadType == GEDC_PAYLOAD_STRING) {
        char *p = strchr(s->string, '\n');
        if (p) *p = '\0';
        if (s->string[0] == '@' && (dialect >= 7 || s->string[1] != '#')) printf(" @%s", s->string);
        else printf(" %s", s->string);
        while (p) {
            *p = '\n';
            p += 1;
            char *p2 = strchr(p, '\n');
            if (p2) *p2 = '\0';
            printf("\n%d CONT %s%s", level+1, (*p=='@'?"@":""), p);
            p = p2;
        }
    }
    else if (s->payloadType == GEDC_PAYLOAD_POINTER) {
        if (s->pointer) printf(" @%s@", s->pointer->id);
        else printf(" @VOID@");
    }
    printf("\n");
    dumpStructure(s->firstChild, level+1);
    dumpStructure(s->nextSibling, level);
}

int main(int argc, const char *argv[]) {
    if (argc <= 1) {
        printf("USAGE: %s filename.ged\n", argv[0]);
        printf("USAGE: %s -[L] filename.ged\n    where [L] is a 1-digit integer (usually 1, 5, or 7)", argv[0]);
        return 1;
    }
    
    int i = 1;
    if (argv[i][0] == '-' && '0' <= argv[i][1] && argv[i][1] <= '9' && !argv[i][2]) {
        dialect = argv[i][1] - '0';
        i += 1;
    }
    
    for(; i<argc; i+=1) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            continue;
        }
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *text = malloc((size+1)*sizeof(char));
        size = fread(text, sizeof(char), size, f) * sizeof(char);
        text[size] = '\0';
        
        const char *errmsg = 0; size_t errline;
        GedStructure *s = parseGEDCOM(text, dialect, &errmsg, &errline);
        fclose(f);
        if (errmsg) fprintf(stderr, "ERROR(%s %zd): %s\n", argv[i], errline, errmsg);
        else dumpStructure(s, 0);
        freeGedStructure(s);
        free(text);
    }
}
