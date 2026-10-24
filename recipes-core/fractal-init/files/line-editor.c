#include "line-editor.h"
#include <ctype.h>
#include <string.h>

/* Preserve CR until this point so CRLF is one submit, even when a dialog
 * changes state between the two bytes. Consecutive LF/CR presses stay distinct. */
int terminal_input_byte(unsigned char *c, int *after_cr) {
    if (*after_cr && *c=='\n') { *after_cr=0; return 0; }
    *after_cr=(*c=='\r');
    if (*after_cr) *c='\n';
    return 1;
}

void editor_init(struct line_editor *e) { memset(e,0,sizeof(*e)); e->history_limit=10; e->editing=1; }
void editor_clear(struct line_editor *e) {
    e->line[0]=0; e->length=e->cursor=0; e->overflow=e->escape=e->literal=e->yank_valid=0;
    e->history_back=0; e->draft[0]=0;
}
void editor_record(struct line_editor *e) {
    if (!e->overflow && e->length && e->history_limit &&
        (!e->history_kept || strcmp(e->line,e->history[(e->history_count-1)%HISTORY_CAPACITY]))) {
        strcpy(e->history[e->history_count++%HISTORY_CAPACITY],e->line);
        if(e->history_kept<e->history_limit)e->history_kept++;
    }
}
void editor_history_limit(struct line_editor *e, unsigned limit) {
    e->history_limit=limit>HISTORY_CAPACITY?HISTORY_CAPACITY:limit;
    if(limit && e->history_kept>limit)e->history_kept=limit;
    e->history_back=0;
}
static void erase(struct line_editor *e, size_t from, size_t to, int save) {
    if (from==to) return;
    if (save) {
        char *slot=e->kills[e->kill_count++%10];
        memcpy(slot,e->line+from,to-from); slot[to-from]=0; e->kill_back=0;
    }
    memmove(e->line+from,e->line+to,e->length-to+1);
    e->length-=to-from; e->cursor=from; e->overflow=0;
}
static enum edit_action recall(struct line_editor *e, int older) {
    unsigned available=e->history_kept;
    if (available>e->history_limit) available=e->history_limit;
    if (older) {
        if (e->history_back>=available) return EDIT_BELL;
        if (!e->history_back) strcpy(e->draft,e->line);
        e->history_back++;
    } else { if (!e->history_back) return EDIT_BELL; e->history_back--; }
    strcpy(e->line,e->history_back ? e->history[(e->history_count-e->history_back)%HISTORY_CAPACITY] : e->draft);
    e->length=e->cursor=strlen(e->line); e->overflow=0;
    return EDIT_REDRAW;
}
static enum edit_action yank(struct line_editor *e, int older) {
    unsigned available=e->kill_count<10 ? e->kill_count : 10;
    if (!available || (older && !e->yank_valid)) return EDIT_BELL;
    if (older) {
        erase(e,e->yank_start,e->yank_start+e->yank_length,0);
        e->kill_back=(e->kill_back+1)%available;
    } else e->kill_back=0;
    const char *text=e->kills[(e->kill_count-1-e->kill_back)%10]; size_t n=strlen(text);
    if (e->length+n>=LINE_CAPACITY) return EDIT_BELL;
    memmove(e->line+e->cursor+n,e->line+e->cursor,e->length-e->cursor+1);
    memcpy(e->line+e->cursor,text,n); e->yank_start=e->cursor; e->yank_length=n;
    e->length+=n; e->cursor+=n; e->yank_valid=1;
    return EDIT_REDRAW;
}
enum edit_action editor_feed(struct line_editor *e, unsigned char c) {
    if (!e->editing && !e->literal) {
        if (c==27) { e->escape=1; return EDIT_NONE; }
        if (e->escape) {
            if (e->escape==1 && (c=='[' || c=='O')) { e->escape=2; return EDIT_NONE; }
            e->escape=0; return EDIT_BELL;
        }
        if (c<32 && c!=3 && c!=4 && c!=8 && c!=9 && c!=10 && c!=13 && c!=14 && c!=16 && c!=22 && c!=26) return EDIT_BELL;
    }
    if (e->literal) { e->literal=0; goto literal; }
    if (e->escape) {
        int state=e->escape; e->escape=0;
        if (state==1) {
            if (c=='[' || c=='O') { e->escape=2; return EDIT_NONE; }
            if (c=='q' || c=='Q') { e->literal=1; return EDIT_NONE; }
            if (c=='y' || c=='Y') return yank(e,1);
            if (c=='d' || c=='D' || c=='c' || c=='C' || c=='u' || c=='U' || c=='l' || c=='L') {
                size_t start=e->cursor, first=start, end;
                while(first<e->length && isspace((unsigned char)e->line[first]))first++;
                end=first;
                while(end<e->length && !isspace((unsigned char)e->line[end]))end++;
                e->yank_valid=0;
                if(c=='d'||c=='D')erase(e,start,end,1);
                else {
                    for(size_t i=first;i<end;i++) {
                        unsigned char ch=(unsigned char)e->line[i];
                        e->line[i]=(char)((c=='u'||c=='U'||((c=='c'||c=='C')&&i==first))?toupper(ch):tolower(ch));
                    }
                    e->cursor=end;
                }
                return EDIT_REDRAW;
            }
            if (c=='b' || c=='B') {
                while (e->cursor && isspace((unsigned char)e->line[e->cursor-1])) e->cursor--;
                while (e->cursor && !isspace((unsigned char)e->line[e->cursor-1])) e->cursor--;
                e->yank_valid=0; return EDIT_REDRAW;
            }
            if (c=='f' || c=='F') {
                while (e->cursor<e->length && !isspace((unsigned char)e->line[e->cursor])) e->cursor++;
                while (e->cursor<e->length && isspace((unsigned char)e->line[e->cursor])) e->cursor++;
                e->yank_valid=0; return EDIT_REDRAW;
            }
            return EDIT_BELL;
        }
        e->yank_valid=0;
        if (state>=10) {
            if (c!='~') return EDIT_BELL;
            if (state==11 || state==17) e->cursor=0;
            else if (state==14 || state==18) e->cursor=e->length;
            else if (state==13 && e->cursor<e->length) erase(e,e->cursor,e->cursor+1,0);
            else return EDIT_BELL;
            return EDIT_REDRAW;
        }
        if (c>='1' && c<='8') { e->escape=10+c-'0'; return EDIT_NONE; }
        if (c=='A') return recall(e,1);
        if (c=='B') return recall(e,0);
        if (c=='D') { if (!e->cursor) return EDIT_BELL; e->cursor--; }
        else if (c=='C') { if (e->cursor==e->length) return EDIT_BELL; e->cursor++; }
        else if (c=='H') e->cursor=0;
        else if (c=='F') e->cursor=e->length;
        else return EDIT_BELL;
        return EDIT_REDRAW;
    }
    if (c==27) { e->escape=1; return EDIT_NONE; }
    if (c==25) return yank(e,0);
    e->yank_valid=0;
    if (c==22) { e->literal=1; return EDIT_NONE; }
    if (c=='?') return EDIT_HELP;
    if (c==9) return EDIT_COMPLETE;
    if (c=='\r' || c=='\n') return EDIT_SUBMIT;
    if (c==3) { editor_clear(e); return EDIT_CANCEL; }
    if (c==26) { editor_clear(e); return EDIT_END; }
    if (c==16) return recall(e,1);
    if (c==14) return recall(e,0);
    if (c==1) { e->cursor=0; return EDIT_REDRAW; }
    if (c==5) { e->cursor=e->length; return EDIT_REDRAW; }
    if (c==2) { if (!e->cursor) return EDIT_BELL; e->cursor--; return EDIT_REDRAW; }
    if (c==6) { if (e->cursor==e->length) return EDIT_BELL; e->cursor++; return EDIT_REDRAW; }
    if (c==12 || c==18) return EDIT_REDRAW;
    if (c==11) { erase(e,e->cursor,e->length,1); return EDIT_REDRAW; }
    if (c==21 || c==24) { erase(e,0,e->cursor,1); return EDIT_REDRAW; }
    if (c==23) {
        size_t start=e->cursor;
        while (start && isspace((unsigned char)e->line[start-1])) start--;
        while (start && !isspace((unsigned char)e->line[start-1])) start--;
        erase(e,start,e->cursor,1); return EDIT_REDRAW;
    }
    if (c==20) {
        if (e->cursor==0 || e->length<2) return EDIT_BELL;
        size_t at=e->cursor==e->length ? e->cursor-1 : e->cursor;
        char tmp=e->line[at-1]; e->line[at-1]=e->line[at]; e->line[at]=tmp;
        if (e->cursor<e->length) e->cursor++;
        return EDIT_REDRAW;
    }
    if (c==4) {
        if (!e->length) return EDIT_EOF;
        if (e->cursor==e->length) return EDIT_BELL;
        erase(e,e->cursor,e->cursor+1,0); return EDIT_REDRAW;
    }
    if (c==8 || c==127) {
        if (!e->cursor) return EDIT_BELL;
        int end=e->cursor==e->length;
        erase(e,e->cursor-1,e->cursor,0);
        return end ? EDIT_ERASE : EDIT_REDRAW;
    }
literal:
    if (c<32 || c>=127) return EDIT_BELL;
    if (e->length==LINE_CAPACITY-1) { e->overflow=1; return EDIT_BELL; }
    int end=e->cursor==e->length;
    memmove(e->line+e->cursor+1,e->line+e->cursor,e->length-e->cursor+1);
    e->line[e->cursor++]=(char)c; e->length++;
    return end ? EDIT_INSERT : EDIT_REDRAW;
}
