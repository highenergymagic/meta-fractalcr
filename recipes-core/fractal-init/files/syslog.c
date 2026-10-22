/* SPDX-License-Identifier: MIT -- local datagram syslog, never console output. */
#define _GNU_SOURCE
#include "supervisor.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int syslog_receiver(const char *socket_path,const char *log_path)
{
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if (strlen(socket_path)>=sizeof(address.sun_path)) return 1;
    strcpy(address.sun_path,socket_path);
    struct stat st;
    if (!lstat(socket_path,&st) && !S_ISSOCK(st.st_mode)) return 1;
    if (unlink(socket_path) && errno!=ENOENT) return 1;
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if (fd<0 || bind(fd,(struct sockaddr *)&address,sizeof(address)) || chmod(socket_path,0666)) return 1;
    service_notify();
    for (;;) {
        char message[8192];
        ssize_t n=recv(fd,message,sizeof(message)-1,0);
        if (n<0) { if (errno==EINTR) continue; close(fd); return 1; }
        if (!n) continue;
        while (n && (message[n-1]=='\0' || message[n-1]=='\n')) n--;
        message[n++]='\n';
        log_append(log_path,message,(size_t)n);
    }
}
