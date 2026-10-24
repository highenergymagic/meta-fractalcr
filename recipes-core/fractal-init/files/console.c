#include "release.h"
/* SPDX-License-Identifier: MIT -- restartable serial console; never PID 1. */
#define _GNU_SOURCE
#include "control.h"
#include "platform-info.h"
#include "line-editor.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <poll.h>
#include <regex.h>
#include <pty.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static char hostname[64] = "ctr", mode_suffix[64] = "# ";
static struct line_editor editors[2];
static unsigned editor_context;
#define editor editors[editor_context]
#define input_line editor.line
#define input_length editor.length
#define input_cursor editor.cursor
static int connected, command_busy;
static int challenged, secret_input, remote_console, close_session;
static int terminal_monitor;
static int terminal_privilege=-1;
static unsigned history_size=10;
static void reset_editors(void)
{
    editor_init(&editors[0]); editor_init(&editors[1]); editor_context=0;
}
static void history_limit(unsigned limit)
{
    editor_history_limit(&editors[0],limit);editor_history_limit(&editors[1],limit);
}
static unsigned baud_value(speed_t rate)
{
    switch(rate) {
    case B0:return 0; case B50:return 50; case B75:return 75; case B110:return 110;
    case B300:return 300; case B600:return 600; case B1200:return 1200;
    case B2400:return 2400; case B4800:return 4800; case B9600:return 9600;
    case B19200:return 19200; case B38400:return 38400; case B57600:return 57600;
    case B115200:return 115200; case B230400:return 230400; default:return 0;
    }
}
static char auth_prompt[2048];
static unsigned terminal_length=24, terminal_width=80, output_rows, output_column;
static int output_aborted;
static size_t view_start;
static off_t event_offset;
static ino_t event_inode;
static void local_execute(const char *line);
static void management_event(const char *action, const char *line);
static void output_text(const char *text)
{
    while (*text && !output_aborted) {
            unsigned page=terminal_length>1 ? terminal_length-1 : 1;
            if (terminal_length && output_rows>=page) {
                fputs(" --More-- ",stdout); fflush(stdout);
                unsigned char key=0;
                while (read(0,&key,1)==1) {
                    if (key=='q' || key=='Q' || key==3) { output_aborted=1; break; }
                    if (key==' ') { output_rows=0; break; }
                    if (key=='\r' || key=='\n') { output_rows=page-1; break; }
                }
                fputs("\r\033[2K",stdout);
            }
        unsigned char c=(unsigned char)*text++;
        if (output_aborted) break;
        putchar(c);
        if (c=='\r') output_column=0;
        else if (c=='\n' || ++output_column>=terminal_width) { output_column=0; output_rows++; }
    }
}
/* A stream grant belongs to this console PID and the resolved command kept by
 * management. A restart, lost socket, revoked policy or login expires it. */
static int stream_policy(const char *action)
{
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(fd<0)return 0;
    struct timeval timeout={.tv_sec=2};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strcpy(address.sun_path,"/run/fractal-management.sock");
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))) { close(fd);return 0; }
    char request[128];
    int length=snprintf(request,sizeof(request),"{\"action\":\"%s\",\"command\":\"\"}\n",action);
    size_t sent=0;
    while(sent<(size_t)length) {
        ssize_t n=send(fd,request+sent,(size_t)length-sent,MSG_NOSIGNAL);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){close(fd);return 0;}
        sent+=(size_t)n;
    }
    char buffer[8192];size_t used=0;
    while(used<sizeof(buffer)-1) {
        ssize_t n=recv(fd,buffer+used,sizeof(buffer)-1-used,0);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        used+=(size_t)n;
        if(memchr(buffer,'\n',used))break;
    }
    close(fd);buffer[used]=0;
    json_object *response=json_tokener_parse(buffer),*grant;
    int allowed=response && json_object_object_get_ex(response,"stream",&grant) && json_object_get_boolean(grant);
    if(response)json_object_put(response);
    return allowed;
}

/* Native Docker clients keep their own streaming/interactive protocol. A PTY
 * gives Ctrl-C and attach/exec a real foreground terminal without a host shell. */
static void native_execute(json_object *arguments)
{
    if(!stream_policy("stream-check")){puts("% Authorization failed.");return;}
    size_t count=json_object_array_length(arguments);
    if (!count || count>256) { puts("% Invalid native command."); return; }
    char *argv[257];
    for (size_t i=0;i<count;i++) argv[i]=(char *)json_object_get_string(json_object_array_get_idx(arguments,i));
    argv[count]=NULL;
    if (strcmp(argv[0],"/usr/libexec/fractal-workload")) {
        puts("% Invalid workload service."); return;
    }
    struct termios term;
    if (tcgetattr(0,&term)) return;
    struct termios console_term=term;
    term.c_lflag |= ICANON | ECHO | ISIG;
    struct winsize size={.ws_row=terminal_length?terminal_length:24,.ws_col=terminal_width};
    int master;
    pid_t parent=getpid(), child=forkpty(&master,NULL,&term,&size);
    if (child<0) { puts("% Cannot start native client."); return; }
    if (!child) {
        prctl(PR_SET_PDEATHSIG,SIGKILL);
        if (getppid()!=parent) _exit(126);
        setenv("HOME","/datastore/data0/client",1);
        setenv("DOCKER_CONFIG","/datastore/data0/client/docker",1);
        setenv("BUILDKIT_HOST","unix:///run/buildkit/buildkitd.sock",1);
        setenv("TERM","vt100",1);
        if (chdir("/datastore/data0/projects")) { perror("Datastore workspace"); _exit(126); }
        execv(argv[0],argv); puts("% Workload service is unavailable."); _exit(127);
    }
    struct termios stream_term=console_term;
    stream_term.c_oflag &= ~OPOST;
    stream_term.c_iflag &= ~(ICRNL|INLCR|IGNCR|ISTRIP|BRKINT|INPCK|PARMRK);
    tcsetattr(0,TCSANOW,&stream_term);
    fcntl(master,F_SETFL,fcntl(master,F_GETFL)|O_NONBLOCK);
    char pending[8192]; size_t pending_size=0;
    long long checked=monotonic_ms();
    for (;;) {
        if(monotonic_ms()-checked>=1000) {
            if(!stream_policy("stream-check")) {
                kill(-child,SIGKILL);kill(child,SIGKILL);
                puts("% Command authorization revoked.");break;
            }
            checked=monotonic_ms();
        }
        struct pollfd fds[2]={{.fd=master,.events=POLLIN | (pending_size?POLLOUT:0)},
                             {.fd=0,.events=pending_size<4096?POLLIN:0}};
        if (poll(fds,2,250)<0) { if (errno==EINTR) continue; break; }
        char buffer[4096];
        if (fds[0].revents & (POLLIN|POLLHUP|POLLERR)) {
            ssize_t n=read(master,buffer,sizeof(buffer));
            if (n>0) fwrite(buffer,1,(size_t)n,stdout);
            else if (!n || errno!=EAGAIN) break;
        }
        if (pending_size && (fds[0].revents & POLLOUT)) {
            ssize_t sent=write(master,pending,pending_size);
            if (sent>0) { pending_size-=(size_t)sent; memmove(pending,pending+sent,pending_size); }
            else if (sent<0 && errno!=EAGAIN && errno!=EINTR) break;
        }
        if (fds[1].revents & POLLIN) {
            ssize_t n=read(0,buffer,sizeof(buffer));
            if (n<=0) break;
            memcpy(pending+pending_size,buffer,(size_t)n); pending_size+=(size_t)n;
        }
    }
    close(master);
    int status=0;
    while (waitpid(child,&status,0)<0 && errno==EINTR) {}
    stream_policy("stream-end");
    tcsetattr(0,TCSANOW,&console_term);
    if (WIFEXITED(status) && WEXITSTATUS(status)) printf("%% Client exited with status %d.\n",WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) printf("%% Client terminated by signal %d.\n",WTERMSIG(status));
}
static void prompt(void)
{
    if (challenged) { printf("%s%s",auth_prompt,secret_input?"":input_line); return; }
    if (gethostname(hostname, sizeof(hostname))) strcpy(hostname, "ctr");
    size_t prefix=strlen(hostname)+strlen(mode_suffix);
    size_t width=terminal_width>prefix+4 ? terminal_width-prefix-3 : 8;
    if (input_length<=width) view_start=0;
    else if (input_cursor<view_start) view_start=input_cursor;
    else if (input_cursor>view_start+width) view_start=input_cursor-width/2;
    if (view_start>input_length) view_start=0;
    size_t visible=input_length-view_start;
    if (visible>width) visible=width;
    printf("%s%s%s%.*s",hostname,mode_suffix,view_start?"$":"",(int)visible,input_line+view_start);
    int right=input_length>view_start+visible;
    if (right) putchar('$');
    size_t back=visible+right-(input_cursor-view_start);
    if (back) printf("\033[%zuD",back);
}
static void events(void)
{
    if ((remote_console && !terminal_monitor) || challenged) return;
    FILE *f = fopen("/run/log/events.log", "r");
    struct stat st;
    if (!f) return;
    if (fstat(fileno(f), &st)) { fclose(f); return; }
    if (st.st_ino != event_inode || st.st_size < event_offset) event_offset = 0;
    event_inode = st.st_ino;
    fseeko(f, event_offset, SEEK_SET);
    char line[512]; int printed = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!printed && connected && !command_busy) putchar('\n');
        fputs(line, stdout); printed = 1;
    }
    event_offset = ftello(f); fclose(f);
    if (printed && connected && !command_busy) prompt();
}
static void filtered_reply(json_object *reply,json_object *filter)
{
    json_object *mode,*pattern,*item;
    if (!json_object_object_get_ex(filter,"mode",&mode) || !json_object_object_get_ex(filter,"pattern",&pattern)) return;
    const char *kind=json_object_get_string(mode);
    int redirect=!strcmp(kind,"redirect")||!strcmp(kind,"append")||!strcmp(kind,"tee")||!strcmp(kind,"tee-append");
    regex_t regex;
    if (!redirect && regcomp(&regex,json_object_get_string(pattern),REG_EXTENDED|REG_NOSUB)) { puts("% Invalid output filter.");return; }
    char *text=NULL;size_t length=0;
    FILE *capture=open_memstream(&text,&length),*original=stdout;
    if (!capture) { if(!redirect)regfree(&regex);return; }
    unsigned saved_length=terminal_length;
    terminal_length=0;stdout=capture;
    if (json_object_object_get_ex(reply,"output",&item) && *json_object_get_string(item)) puts(json_object_get_string(item));
    if (json_object_object_get_ex(reply,"local",&item)) local_execute(json_object_get_string(item));
    fflush(stdout);stdout=original;fclose(capture);terminal_length=saved_length;
    output_rows=output_column=0;
    if(redirect) {
        if(length>1024*1024) puts("% Command output exceeds redirection limit.");
        else management_event("output",text?text:"");
        free(text);return;
    }
    unsigned count=0;int begun=0,section=0;
    for(char *line=text;line && *line;) {
        char *end=strchr(line,'\n');if(end)*end=0;
        int match=regexec(&regex,line,0,NULL,0)==0;
        if(match){count++;begun=1;}
        if (*line && *line!=' ' && *line!='\t') section=0;
        section=section||match;
        if ((!strcmp(kind,"include")&&match)||(!strcmp(kind,"exclude")&&!match)||(!strcmp(kind,"begin")&&begun)||
            (!strncmp(kind,"section",7) && section!=(!strcmp(kind,"section-exclude")))) {
            output_text(line);output_text("\n");
        }
        if(!end)break;
        line=end+1;
    }
    if(!strcmp(kind,"count"))printf("Number of lines which match regexp = %u\n",count);
    free(text);regfree(&regex);
}
static void management_event(const char *action, const char *line)
{
    if (!*action && !challenged && !strncmp(line, "request image pull ", 19)) puts("Receiving container image...");
    else if (!*action && !challenged && !strcmp(line, "request platform debug service-container enable")) puts("Preparing platform debug service container...");
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    pid_t recovery=-1;
    struct sockaddr_un address = {.sun_family=AF_UNIX};
    strcpy(address.sun_path, "/run/fractal-management.sock");
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if (fd >= 0) close(fd);
        int pipes[2];
        if (remote_console || !strcmp(action,"output") || pipe2(pipes,O_CLOEXEC)) {
            puts("% Management unavailable; access denied.");
            if (remote_console) close_session=1;
            return;
        }
        recovery=fork();
        if (!recovery) {
            dup2(pipes[1],1); close(pipes[0]); close(pipes[1]);
            execl("/usr/libexec/fractal-management","fractal-management","recover",action,line,(char *)NULL);
            _exit(127);
        }
        close(pipes[1]); fd=pipes[0];
        if (recovery<0) { close(fd); puts("% Management unavailable; access denied."); return; }
    }
    json_object *request = json_object_new_object();
    json_object_object_add(request,"action",json_object_new_string(action));
    json_object_object_add(request, "command", json_object_new_string(line));
    json_object *terminal=json_object_new_object();
    json_object_object_add(terminal,"length",json_object_new_int((int)terminal_length));
    json_object_object_add(terminal,"width",json_object_new_int((int)terminal_width));
    json_object_object_add(terminal,"history_size",json_object_new_int((int)history_size));
    json_object_object_add(terminal,"history",json_object_new_boolean(editor.history_limit!=0));
    json_object_object_add(terminal,"editing",json_object_new_boolean(editor.editing));
    json_object_object_add(terminal,"monitor",json_object_new_boolean(terminal_monitor));
    struct termios current_term;
    if (!remote_console && !tcgetattr(0,&current_term)) {
        json_object_object_add(terminal,"tx",json_object_new_int((int)baud_value(cfgetospeed(&current_term))));
        json_object_object_add(terminal,"rx",json_object_new_int((int)baud_value(cfgetispeed(&current_term))));
    }
    json_object_object_add(request,"terminal",terminal);
    if (remote_console) {
        json_object_object_add(request,"transport",json_object_new_string("ssh"));
        json_object_object_add(request,"user",json_object_new_string(getenv("FCR_SSH_USER")));
        const char *verifier=getenv("FCR_SSH_VERIFIER");
        json_object_object_add(request,"verifier",json_object_new_string(verifier?verifier:""));
        const char *vty=getenv("FCR_SSH_VTY");
        const char *peer=getenv("FCR_SSH_PEER");
        if(peer)json_object_object_add(request,"peer",json_object_new_string(peer));
        json_object_object_add(request,"vty",json_object_new_int(vty?atoi(vty):0));
    }
    const char *text = json_object_to_json_string_ext(request, JSON_C_TO_STRING_PLAIN);
    size_t remaining = strlen(text);
    while (remaining && recovery<0) {
        ssize_t sent = send(fd, text, remaining, MSG_NOSIGNAL);
        if (sent <= 0) break;
        text += sent; remaining -= (size_t)sent;
    }
    int sent = recovery>=0 || (!remaining && send(fd, "\n", 1, MSG_NOSIGNAL) == 1);
    json_object_put(request);
    const size_t response_limit=7*1024*1024;
    char *buffer = calloc(1, response_limit+1);
    size_t used = 0;
    long long deadline = monotonic_ms() + 360000;
    while (buffer && sent && used < response_limit && monotonic_ms() < deadline) {
        struct pollfd p = {.fd=fd,.events=POLLIN};
        events();
        if (poll(&p, 1, 100) <= 0) continue;
        ssize_t n = read(fd, buffer+used, response_limit-used);
        if (n <= 0) break;
        used += (size_t)n;
        if (memchr(buffer, '\n', used)) break;
    }
    close(fd);
    if (recovery>0) { int status; while (waitpid(recovery,&status,0)<0 && errno==EINTR) {} }
    json_object *reply = buffer ? json_tokener_parse(buffer) : NULL, *item;
    free(buffer);
    if (!reply) {
        puts("% Management request interrupted; inspect running-config before retrying.");
        return;
    }
    int was_challenged=challenged;
    challenged=json_object_object_get_ex(reply,"challenge",&item) && json_object_get_boolean(item);
    secret_input=json_object_object_get_ex(reply,"secret",&item) && json_object_get_boolean(item);
    close_session=json_object_object_get_ex(reply,"close",&item) && json_object_get_boolean(item);
    if (challenged!=was_challenged) editor_clear(&editor);
    json_object *filter=NULL;
    int filtered=json_object_object_get_ex(reply,"filter",&filter);
    if(filtered)filtered_reply(reply,filter);
    if (!filtered && json_object_object_get_ex(reply, "output", &item) && *json_object_get_string(item)) {
        const char *output=json_object_get_string(item);
        if (!strcmp(output,"\a")) putchar('\a');
        else { output_text(output); if (!output_aborted) putchar('\n'); }
    }
    if (json_object_object_get_ex(reply, "error_column", &item)) {
        int column=json_object_get_int(item);
        if (column>=0 && column<2048) printf("%*s^\n", (int)(strlen(hostname)+strlen(mode_suffix))+column, "");
    }
    if (json_object_object_get_ex(reply, "error", &item)) printf("%% %s\n", json_object_get_string(item));
    if (json_object_object_get_ex(reply, "prompt", &item)) {
        const char *p = json_object_get_string(item);
        if (challenged) snprintf(auth_prompt,sizeof(auth_prompt),"%s",p);
        /* An unchanged heartbeat at Username must not erase partially typed
         * input. Reset only when entering login or handling an explicit reply. */
        if (!strcmp(action,"connect") ||
            (!strcmp(p,"Username: ") && (!was_challenged || strcmp(action,"tick"))) || close_session) {
            reset_editors(); history_size=10;
            terminal_length=24; terminal_width=80; terminal_monitor=0;
            terminal_privilege=-1;
        }
        const char *suffix = strchr(p, '(');
        if (!suffix) suffix = strchr(p,'>') ? "> " : "# ";
        struct json_object *privilege;
        int next_privilege=terminal_privilege;
        if (json_object_object_get_ex(reply,"privilege",&privilege)) next_privilege=json_object_get_int(privilege);
        if ((terminal_privilege>=0 && next_privilege<terminal_privilege) || (strchr(mode_suffix,'#') && strchr(suffix,'>'))) {
            unsigned limit=editor.history_limit;
            int editing=editor.editing;
            reset_editors(); history_limit(limit);
            editors[0].editing=editors[1].editing=editing;
        }
        terminal_privilege=next_privilege;
        if (!challenged) {
            unsigned next=strstr(suffix,"config")!=NULL;
            if (next!=editor_context) { editor_clear(&editor); editor_context=next; editor_clear(&editor); }
        }
        snprintf(mode_suffix, sizeof(mode_suffix), "%.63s", suffix);
    }
    if (json_object_object_get_ex(reply, "completion", &item)) {
        const char *completed = json_object_get_string(item);
        if (strlen(completed) < sizeof(input_line)) {
            strcpy(input_line, completed); input_length = input_cursor = strlen(completed);
        }
    }
    if (!filtered && json_object_object_get_ex(reply, "local", &item)) local_execute(json_object_get_string(item));
    if (json_object_object_get_ex(reply, "argv", &item)) native_execute(item);
    if (json_object_object_get_ex(reply,"notify",&item) && *json_object_get_string(item)) {
        fputs("\r\033[2K",stdout); fputs(json_object_get_string(item),stdout);
        if (!strcmp(action,"tick")) prompt();
    }
    json_object_put(reply);
    if (!strcmp(action,"tick") && challenged!=was_challenged) { putchar('\n'); prompt(); }
}
static void management(const char *line) { management_event("",line); }
static int snapshot(struct control_reply *reply)
{
    if (!control_call(CT_LIST, NULL, reply)) return 0;
    puts("% Supervisor control channel is unavailable."); return -1;
}
static int running(const struct control_reply *reply, const char *name)
{
    const struct service_status *s = control_find(reply, name);
    return s && !s->blocked && s->state == SV_RUNNING;
}
static void display_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { puts("% No log records available."); return; }
    char buffer[2048]; struct stat st;
    off_t remaining = !fstat(fileno(f), &st) ? st.st_size : 0;
    while (remaining > 0 && fgets(buffer, sizeof(buffer), f)) {
        remaining -= (off_t)strlen(buffer); output_text(buffer);
        if (output_aborted) break;
    }
    fclose(f);
}
static void diagnostic(void)
{
    struct control_reply reply;
    if (snapshot(&reply)) return;
    if (!running(&reply, "container-engine")) { puts("% Container service is not ready."); return; }
    const struct service_status *s = control_find(&reply, "diagnostics");
    if (!s || s->state == SV_STARTING || s->state == SV_STOPPING) { puts("% Diagnostics are already active."); return; }
    unsigned starts = s->starts;
    if (control_call(CT_START, "diagnostics", &reply)) { puts("% Cannot start diagnostics."); return; }
    puts("Running container diagnostics...");
    long long deadline = monotonic_ms() + 135000;
    int passed = 0;
    while (monotonic_ms() < deadline) {
        events();
        if (snapshot(&reply)) break;
        s = control_find(&reply, "diagnostics");
        if (!s) break;
        if (s->starts > starts && s->state == SV_COMPLETE) { passed = 1; break; }
        if (s->state == SV_FAILED || s->blocked) break;
        usleep(100000);
    }
    puts(passed ? "%FCR-5-DIAGNOSTIC_PASS: Container diagnostics passed." :
         "%FCR-3-DIAGNOSTIC_FAILED: Container diagnostics failed. See show logging diagnostics.");
}
static void local_execute(const char *line)
{
    const char *original = line;
    if (!strncmp(line, "do ", 3)) line += 3;
    struct control_reply reply;
    if (!strncmp(line,"terminal ",9)) {
        if (!strcmp(line,"terminal editing")) { editors[0].editing=editors[1].editing=1; return; }
        if (!strcmp(line,"terminal no editing")) { editors[0].editing=editors[1].editing=0; return; }
        if (!strcmp(line,"terminal history")) { history_limit(history_size); return; }
        if (!strcmp(line,"terminal no history")) { history_limit(0); return; }
        if (!strcmp(line,"terminal no history size")) { history_size=10; history_limit(10); return; }
        if (!strcmp(line,"terminal monitor")) { terminal_monitor=1; return; }
        if (!strcmp(line,"terminal no monitor")) { terminal_monitor=0; return; }
        const char *number=NULL; unsigned minimum=0, maximum=512;
        unsigned *setting=NULL;
        if (!strncmp(line,"terminal length ",16)) { number=line+16; setting=&terminal_length; }
        else if (!strncmp(line,"terminal width ",15)) { number=line+15; setting=&terminal_width; minimum=40; }
        else if (!strncmp(line,"terminal history size ",22)) { number=line+22; setting=&editor.history_limit; maximum=HISTORY_CAPACITY; }
        if (!number || !*number) { puts("% Invalid terminal setting."); return; }
        char *end; unsigned long value=strtoul(number,&end,10);
        if (*end || value<minimum || value>maximum) {
            puts("% Invalid terminal setting value."); return;
        }
        *setting=(unsigned)value;
        if (setting==&editor.history_limit) { history_size=(unsigned)value; history_limit(history_size); }
    } else if (!strcmp(line,"show history")) {
        unsigned count=editor.history_kept;
        if (count>editor.history_limit) count=editor.history_limit;
        for (unsigned i=editor.history_count-count;i<editor.history_count;i++) {
            output_text("  "); output_text(editor.history[i%HISTORY_CAPACITY]); output_text("\n");
        }
    } else if (!strcmp(line, "show platform software service")) {
        if (!snapshot(&reply)) control_display(&reply, stdout);
    } else if (!strcmp(line, "show platform")) {
        if (snapshot(&reply)) return;
        const struct service_status *s = control_find(&reply, "container-engine");
        struct statfs root;
        printf("Datastore data0: %s\nSystem image: %s\nContainer service: %s\n",
            running(&reply, "datastore") ? "online" : "unavailable",
            !statfs("/", &root) && (root.f_flags & ST_RDONLY) ? "read-only" : "FAILED",
            s ? service_state_name(s->state) : "unavailable");
    } else if (!strcmp(line,"show inventory")) {
        platform_inventory(1);
    } else if (!strcmp(line, "show version")) {
        struct utsname uts; uname(&uts);
        printf("FractalCR Software, Version %s, %s\nTechnical Support: %s\n%s\n\nPlatform kernel: %s\n", FCR_VERSION, FCR_RELEASE_KIND, FCR_SUPPORT, FCR_COPYRIGHT, uts.release);
        char rom[128]="Unavailable", image[257]="Unavailable", cmdline[4096];
        int ignored_startup=0;
        FILE *cmd=fopen("/proc/cmdline","r");
        if (cmd) {
            if (fgets(cmdline,sizeof(cmdline),cmd)) {
                char *save=NULL;
                for (char *token=strtok_r(cmdline," \r\n",&save);token;token=strtok_r(NULL," \r\n",&save)) {
                    if (!strcmp(token,"fcr.ignore_startup=1")) ignored_startup=1;
                    if (!strncmp(token,"fcr.rommon=",11)) snprintf(rom,sizeof(rom),"%s",token+11);
                    if (!strncmp(token,"fcr.image_hex=",14)) {
                        const char *hex=token+14; size_t n=strlen(hex);
                        if (n && !(n%2) && n/2<sizeof(image) && strspn(hex,"0123456789abcdef")==n) {
                            for (size_t i=0;i<n/2;i++) { unsigned v=0; (void)sscanf(hex+2*i,"%2x",&v); image[i]=(v>=32 && v<127)?(char)v:'?'; }
                            image[n/2]=0;
                        }
                    }
                }
            }
            fclose(cmd);
        }
        printf("ROM: FractalCR System Bootstrap, Version %s\nSystem image file is \"%s\"\n",rom,image);
        struct sysinfo information;
        if (!sysinfo(&information)) printf("%s uptime is %ld days, %ld hours, %ld minutes\n",hostname,information.uptime/86400,information.uptime/3600%24,information.uptime/60%60);
        puts("Last reload reason: Not recorded\n");
        platform_inventory(1);
        unsigned interfaces=0;
        DIR *network=opendir("/sys/class/net");
        if (network) {
            struct dirent *entry;
            while ((entry=readdir(network))) {
                char device[512];
                snprintf(device,sizeof(device),"/sys/class/net/%s/device",entry->d_name);
                if (!access(device,F_OK)) interfaces++;
            }
            closedir(network);
            printf("%u physical network interface%s\n",interfaces,interfaces==1?"":"s");
        }
        const char *mounts[]={"/run/filesystems/bootflash","/datastore/data0"};
        const char *labels[]={"bootflash:","data0:"};
        for (unsigned i=0;i<2;i++) {
            struct statfs fs;
            if (!statfs(mounts[i],&fs) && (unsigned long)fs.f_type==(i?0x9123683eUL:0x4d44UL))
                printf("%lluK bytes of %s storage (%lluK bytes available)\n",(unsigned long long)fs.f_blocks*fs.f_bsize/1024,labels[i],(unsigned long long)fs.f_bavail*fs.f_bsize/1024);
        }
        printf("Configuration register is %s\n",ignored_startup?"0x2142":"0x2102");
        FILE *identity=fopen("/etc/fractal/build-id","r");
        if (identity) {
            char build[65];
            if (fscanf(identity,"%64[0-9a-f]",build)==1) printf("Source build: %s\n",build);
            fclose(identity);
        }
    } else if (!strcmp(line, "show logging")) display_file("/run/log/events.log");
    else if (!strncmp(line, "show logging ", 13)) {
        const char *name = line+13;
        if (!strcmp(name,"aaa")) { display_file("/datastore/data0/log/aaa.jsonl"); return; }
        /* Logs remain accessible even when the supervisor's socket is broken. */
        if (!*name || strlen(name) > 31 || strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(name)) {
            puts("% Invalid service log name."); return;
        }
        char path[96]; snprintf(path, sizeof(path), "/run/log/%.31s.log", name); display_file(path);
    } else if (!strncmp(line, "request platform software service ", 34)) {
        char name[32], action[16], extra;
        if (sscanf(line+34, "%31s %15s %c", name, action, &extra) != 2) { puts("% Expected service name and start, stop, or restart."); return; }
        enum control_op op;
        if (!strcmp(action, "start")) op = CT_START;
        else if (!strcmp(action, "stop")) op = CT_STOP;
        else if (!strcmp(action, "restart")) op = CT_RESTART;
        else { puts("% Expected start, stop, or restart."); return; }
        int result = control_call(op, name, &reply);
        if (result == -2) puts("% Service prerequisites are not satisfied.");
        else if (result == -3) puts("% This stop would remove the console; use restart to reconnect.");
        else if (result) puts("% Unknown service or unavailable supervisor.");
        events();
    } else if (!strcmp(line, "request platform diagnostics containers")) diagnostic();
    else if (!strcmp(line, "request platform debug service-container enter")) {
        if (snapshot(&reply) || !running(&reply, "debug-container")) {
            puts("% Debug service is not ready; enable it first."); return;
        }
        puts("Entering platform debug container. Host filesystem: /host. Type exit to return.");
        const char *args[]={"/usr/libexec/fractal-workload","engine","exec","-it","--env",
            "PS1=(platform debug container) \\u@\\h:\\w# ","fractal-platform-debug","/bin/bash","--norc"};
        json_object *arguments=json_object_new_array();
        for (unsigned i=0;i<sizeof(args)/sizeof(args[0]);i++) json_object_array_add(arguments,json_object_new_string(args[i]));
        native_execute(arguments); json_object_put(arguments);
        puts("\nReturned to appliance console.");
    }
    else if (!strcmp(line, "reload")) {
        puts("\n%FCR-5-RELOAD: System reload requested.\nStopping platform services...");
        if (control_call(CT_RELOAD, NULL, &reply)) puts("% Reload request failed.");
    } else if (*original && strcmp(original,"__connect"))
        puts("% Management service unavailable. Use request platform software service management restart.");
}
int platform_console(void)
{
    remote_console=getenv("FCR_SSH_USER")!=NULL;
    struct stat st;
    if (!stat("/run/log/events.log", &st)) { event_inode = st.st_ino; event_offset = st.st_size; }
    if (gethostname(hostname, sizeof(hostname))) strcpy(hostname, "ctr");
    struct termios term;
    if (tcgetattr(0, &term)) return 1;
    term.c_lflag &= ~(ICANON | ECHO | ISIG);
    term.c_iflag &= ~(ICRNL | INLCR | IGNCR);
    term.c_oflag |= OPOST | ONLCR;
    term.c_oflag &= ~(OCRNL | ONOCR | ONLRET);
    term.c_iflag &= ~(IXON | IXOFF | IXANY);
    term.c_cc[VMIN] = 1; term.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &term)) return 1;
    if (!remote_console) service_notify();
    int input_after_cr=0;
    long long deadline = monotonic_ms() + 3000;
    struct control_reply reply;
    while (!remote_console && monotonic_ms() < deadline) {
        if (!control_call(CT_LIST, NULL, &reply) && running(&reply, "console")) break;
        usleep(50000);
    }
    if (!stat("/run/log/events.log", &st)) { event_inode = st.st_ino; event_offset = st.st_size; }
    reset_editors();
    management_event("connect","");
    if (close_session) return 0;
    FILE *command_line = fopen("/proc/cmdline", "r");
    if (command_line) {
        char argument[256];
        while (fscanf(command_line, "%255s", argument) == 1)
            if (!strcmp(argument, "fcr.ignore_startup=1"))
                puts("% Startup configuration ignored for physical-console recovery. Restore ROMMON confreg 0x2102 before normal boot.");
        fclose(command_line);
    }
    puts("\nPress RETURN to get started!");
    int reconnect=0;
    long long tick=monotonic_ms();
    for (;;) {
        if (close_session) {
            if (remote_console) return 0;
            connected=0; close_session=0; challenged=0; secret_input=0;
            reconnect=1;
            editor_init(&editor);
            puts("\nPress RETURN to get started!");
        }
        if (connected && monotonic_ms()-tick>=1000) { management_event("tick","");tick=monotonic_ms(); }
        events();
        struct pollfd input = {.fd=0, .events=POLLIN};
        if (poll(&input, 1, 100) <= 0) continue;
        unsigned char chars[1]; ssize_t count = read(0, chars, sizeof(chars));
        if (count <= 0) {
            if (remote_console) return 0;
            usleep(100000); continue;
        }
        for (ssize_t i = 0; i < count; i++) {
            unsigned char c = chars[i];
            if (!terminal_input_byte(&c,&input_after_cr)) continue;
            if (!connected) {
                if (c == '\n' || c == '\r') {
                    connected = 1; putchar('\n');
                    if (reconnect) { management_event("connect",""); reconnect=0; }
                    if (!close_session) prompt();
                }
                continue;
            }
            if (challenged) {
                if (c=='\r'||c=='\n') {
                    putchar('\n');management(input_line);editor_clear(&editor);prompt();
                } else if (c==3) {
                    editor_clear(&editor);putchar('\n');
                    management_event("cancel","");
                    prompt();
                }
                else if (c==127||c==8) { if(input_length) { input_line[--input_length]=0;input_cursor=input_length;if(!secret_input)fputs("\b \b",stdout); } }
                else if(c>=32&&c<127&&input_length<256) { input_line[input_length++]=(char)c;input_line[input_length]=0;input_cursor=input_length;if(!secret_input)putchar(c); }
                continue;
            }
            enum edit_action action=editor_feed(&editor,c);
            switch (action) {
            case EDIT_NONE: break;
            case EDIT_INSERT:
                if (!view_start && input_length+strlen(hostname)+strlen(mode_suffix)+3<terminal_width) putchar(input_line[input_cursor-1]);
                else { fputs("\r\033[2K",stdout); prompt(); }
                break;
            case EDIT_ERASE:
                if (!view_start && input_length+strlen(hostname)+strlen(mode_suffix)+3<terminal_width) fputs("\b \b",stdout);
                else { fputs("\r\033[2K",stdout); prompt(); }
                break;
            case EDIT_BELL: putchar('\a'); break;
            case EDIT_REDRAW: fputs("\r\033[2K",stdout); prompt(); break;
            case EDIT_HELP: {
                output_rows=output_column=0; output_aborted=0;
                char query[2080];
                snprintf(query,sizeof(query),"%.*s",(int)input_cursor,input_line);
                fputs("?\n",stdout); command_busy=1; management_event("help",query); command_busy=0; prompt();
                break;
            }
            case EDIT_COMPLETE: {
                char query[2080],suffix[2048];
                strcpy(suffix,input_line+input_cursor);
                snprintf(query,sizeof(query),"%.*s",(int)input_cursor,input_line);
                input_line[input_cursor]=0; input_length=input_cursor;
                command_busy=1; management_event("complete",query); command_busy=0;
                if (input_length+strlen(suffix)<sizeof(input_line)) {
                    strcat(input_line,suffix); input_length=strlen(input_line);
                }
                fputs("\r\033[2K",stdout); prompt(); break;
            }
            case EDIT_SUBMIT:
                output_rows=output_column=0; output_aborted=0;
                putchar('\n');
                if (editor.overflow) puts("% Command too long.");
                else { editor_record(&editor); command_busy=1; management(input_line); command_busy=0; }
                editor_clear(&editor); if (!close_session) prompt(); break;
            case EDIT_CANCEL: puts("^C"); prompt(); break;
            case EDIT_END:
                putchar('\n'); command_busy=1; management_event("end",""); command_busy=0; prompt(); break;
            case EDIT_EOF:
                putchar('\n'); command_busy=1;
                management(strstr(mode_suffix,"config") ? "exit" : "logout");
                command_busy=0; editor_clear(&editor); if (!close_session) prompt(); break;
            }
        }
    }
}
