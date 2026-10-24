/* SPDX-License-Identifier: MIT */
#ifndef FCR_PAGER_H
#define FCR_PAGER_H
#include <stddef.h>
#include <regex.h>
struct pager {
    unsigned length, width, rows, column;
    int aborted, locked, filter_active, search_active, exclude;
    regex_t filter, search;
    char *line;
    size_t used, capacity;
    int (*read_key)(void *);
    void (*write)(void *, const char *, size_t);
    void *context;
};
void pager_reset(struct pager *, unsigned, unsigned, int);
void pager_write(struct pager *, const char *);
void pager_finish(struct pager *);
void pager_destroy(struct pager *);
#endif
