/* SPDX-License-Identifier: MIT -- serial pagination, search and line filters. */
#include "pager.h"
#include <stdlib.h>
#include <string.h>

#define PAGER_LINE_LIMIT (7U*1024U*1024U)
static void emit(struct pager *p,const char *s) { p->write(p->context,s,strlen(s)); }
static int interrupted(int key) { return key<0 || key==3 || key==26 || key==30; }

void pager_reset(struct pager *p,unsigned length,unsigned width,int locked)
{
    if(p->filter_active)regfree(&p->filter);
    if(p->search_active)regfree(&p->search);
    p->filter_active=p->search_active=p->exclude=0;
    p->length=length;p->width=width?width:80;
    p->rows=p->column=0;p->aborted=0;p->locked=locked;p->used=0;
}
void pager_destroy(struct pager *p)
{
    pager_reset(p,0,80,0);free(p->line);p->line=NULL;p->capacity=0;
}
static int expression(struct pager *p,int operation)
{
    char text[2048];size_t used=0;
    char prefix=(char)operation;p->write(p->context,&prefix,1);
    for(;;) {
        int key=p->read_key(p->context);
        if(interrupted(key)){p->aborted=1;return 0;}
        if(key=='\r'||key=='\n')break;
        if(key==8||key==127){if(used){used--;emit(p,"\b \b");}continue;}
        if(key>=32 && key<127 && used<sizeof(text)-1){text[used++]=(char)key;prefix=(char)key;p->write(p->context,&prefix,1);}
        else emit(p,"\a");
    }
    text[used]=0;emit(p,"\n");
    if(!used){emit(p,"\a");return 0;}
    regex_t compiled;
    if(regcomp(&compiled,text,REG_EXTENDED|REG_NOSUB|REG_NEWLINE)){emit(p,"% Invalid regular expression.\n");return 0;}
    if(operation=='/'){
        if(p->search_active)regfree(&p->search);
        p->search=compiled;p->search_active=1;
    }else{
        p->filter=compiled;p->filter_active=1;p->exclude=operation=='-';p->locked=1;
    }
    emit(p,"filtering...\n");p->rows=0;return 1;
}
static void pause_output(struct pager *p)
{
    unsigned page=p->length>1?p->length-1:1;
    for(;;){
        emit(p," --More-- ");
        int key=p->read_key(p->context);
        emit(p,"\r\033[2K");
        if(interrupted(key)||key=='q'||key=='Q'){p->aborted=1;return;}
        if(key==' '){p->rows=0;return;}
        if(key=='\r'||key=='\n'){p->rows=page-1;return;}
        if(key=='/' || ((key=='+'||key=='-')&&!p->locked)){
            if(expression(p,key)||p->aborted)return;
        }else emit(p,"\a");
    }
}
static int visible(struct pager *p,const char *line)
{
    if(p->filter_active && (regexec(&p->filter,line,0,NULL,0)==0)==p->exclude)return 0;
    if(p->search_active){
        if(regexec(&p->search,line,0,NULL,0))return 0;
        regfree(&p->search);p->search_active=0;
    }
    return 1;
}
static void display_line(struct pager *p)
{
    if(!p->used || p->aborted)return;
    p->line[p->used]=0;
    if(!visible(p,p->line))return;
    unsigned page=p->length>1?p->length-1:1;
    for(size_t i=0;i<p->used && !p->aborted;i++){
        unsigned char c=(unsigned char)p->line[i];
        if(c!='\r' && c!='\n' && p->column>=p->width){p->column=0;p->rows++;}
        if(p->length && p->rows>=page){
            pause_output(p);
            if(p->aborted || !visible(p,p->line+i))return;
        }
        p->write(p->context,p->line+i,1);
        if(c=='\r')p->column=0;
        else if(c=='\n'){p->column=0;p->rows++;}
        else if(c=='\t')p->column=(p->column/8+1)*8;
        else p->column++;
    }
}
void pager_write(struct pager *p,const char *text)
{
    while(*text && !p->aborted){
        if(p->used+1>=p->capacity){
            size_t capacity=p->capacity?p->capacity*2:4096;
            if(capacity>PAGER_LINE_LIMIT+1)capacity=PAGER_LINE_LIMIT+1;
            if(capacity<=p->used+1){emit(p,"\n% Output line exceeds display limit.\n");p->aborted=1;break;}
            char *line=realloc(p->line,capacity);
            if(!line){emit(p,"\n% Insufficient memory for display.\n");p->aborted=1;break;}
            p->line=line;p->capacity=capacity;
        }
        p->line[p->used++]=*text++;
        if(p->line[p->used-1]=='\n'){display_line(p);p->used=0;}
    }
}
void pager_finish(struct pager *p)
{
    display_line(p);p->used=0;
}
