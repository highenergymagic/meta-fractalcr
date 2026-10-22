#ifndef FCR_LINE_EDITOR_H
#define FCR_LINE_EDITOR_H
#include <stddef.h>
#define LINE_CAPACITY 2048
#define HISTORY_CAPACITY 256
enum edit_action { EDIT_NONE, EDIT_REDRAW, EDIT_INSERT, EDIT_ERASE, EDIT_HELP, EDIT_COMPLETE,
    EDIT_SUBMIT, EDIT_CANCEL, EDIT_END, EDIT_BELL };
struct line_editor {
    char line[LINE_CAPACITY], draft[LINE_CAPACITY];
    size_t length, cursor;
    char history[HISTORY_CAPACITY][LINE_CAPACITY], kills[10][LINE_CAPACITY];
    unsigned history_count, history_back, history_limit, kill_count, kill_back;
    size_t yank_start, yank_length;
    int escape, literal, overflow, yank_valid;
};
void editor_init(struct line_editor *e);
void editor_clear(struct line_editor *e);
void editor_record(struct line_editor *e);
enum edit_action editor_feed(struct line_editor *e, unsigned char c);
#endif
