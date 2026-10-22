/* SPDX-License-Identifier: MIT -- retain virtiofsd's namespace sandbox on rootfs. */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argc;
    openlog("fractal-virtiofsd",LOG_PID|LOG_NDELAY,LOG_DAEMON);
    /* pivot_root cannot detach the initial rootfs mount. Give this process
     * an attached bind mount as its root in a private mount namespace.
     * virtiofsd still creates its own PID/mount sandbox and detaches this
     * whole view before serving requests. No host mount is changed.
     */
    const char *root="/tmp/fractal-virtiofsd-root";
    struct stat st;
    if ((mkdir(root,0700) && errno!=EEXIST) || lstat(root,&st) || !S_ISDIR(st.st_mode)) {
        syslog(LOG_ERR,"root directory: %m"); perror("virtiofsd root directory"); return 126;
    }
    if (unshare(CLONE_NEWNS) ||
        mount(NULL,"/tmp",NULL,MS_PRIVATE,NULL) ||
        mount("/",root,NULL,MS_BIND,NULL) ||
        mount(NULL,root,NULL,MS_BIND|MS_REMOUNT|MS_RDONLY,NULL)) {
        syslog(LOG_ERR,"root namespace: %m"); perror("virtiofsd root namespace"); return 126;
    }
    /* The appliance's read-only root is itself a covering bind mount.
     * Copy the runtime mount trees explicitly instead of recursively copying
     * both root views and hiding their children behind the covering mount.
     */
    const char *views[]={"/dev","/proc","/sys","/run","/var","/datastore"};
    char target[256];
    for (unsigned i=0;i<sizeof(views)/sizeof(views[0]);i++) {
        snprintf(target,sizeof(target),"%s%s",root,views[i]);
        if (mount(views[i],target,NULL,MS_BIND|MS_REC,NULL)) {
            syslog(LOG_ERR,"mount %s: %m",views[i]); return 126;
        }
    }
    snprintf(target,sizeof(target),"%s/tmp",root);
    if (mount("tmpfs",target,"tmpfs",MS_NOSUID|MS_NODEV,"mode=1777") ||
        chdir(root) || chroot(".") || chdir("/")) {
        syslog(LOG_ERR,"enter root: %m"); return 126;
    }
    argv[0]="/opt/kata/libexec/virtiofsd";
    execv(argv[0],argv);
    syslog(LOG_ERR,"exec: %m"); perror("virtiofsd exec"); return 127;
}
