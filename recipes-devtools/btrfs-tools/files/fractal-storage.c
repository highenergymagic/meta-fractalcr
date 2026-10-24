/* SPDX-License-Identifier: GPL-2.0-only
 * Typed local storage service, linked with the upstream Btrfs formatter.
 * No command interpreter or executable helper is used.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <blkid/blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

int mkfs_main(int argc, char **argv);
/* x86-64 appliance wire format: little-endian uint32 fields, fixed strings. */
#define MAGIC 0x46535431U
struct request { uint32_t magic, version, operation, profile, count; char disks[16][64]; };
struct response { uint32_t magic, version, status; };
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }

static unsigned existing_datastore(void)
{
 DIR *directory=opendir("/sys/class/block");
 if (!directory) return 3;
 struct dirent *entry;
 unsigned status=0;
 while ((entry=readdir(directory))) {
  if (entry->d_name[0]=='.') continue;
  char path[300];snprintf(path,sizeof(path),"/dev/%s",entry->d_name);
  /* Empty optical drives and absent removable media are not filesystems. */
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
  if (fd<0) { if (errno==ENOMEDIUM || errno==ENXIO) continue; status=3;break; }
  blkid_probe probe=blkid_new_probe();
  if (!probe) {close(fd);status=3;break;}
  errno=0;
  if (blkid_probe_set_device(probe,fd,0,0)) {
   int absent=errno==ENOMEDIUM || errno==ENXIO;
   blkid_free_probe(probe);close(fd);
   if(absent)continue;
   status=3;break;
  }
  int result=blkid_do_safeprobe(probe);
  const char *label=NULL;
  if (!result && !blkid_probe_lookup_value(probe,"LABEL",&label,NULL) && !strcmp(label,"FRDATA")) status=2;
  if (result<0) status=3;
  blkid_free_probe(probe);close(fd);
  if (status) break;
 }
 closedir(directory);return status;
}
static unsigned validate(const struct request *r)
{
 if (r->magic!=MAGIC || r->version!=1 || (r->operation!=1 && r->operation!=2)) return 1;
 if (r->operation==1) return 0;
 if (!r->count || r->count>16 || r->profile>1 || (r->profile==1 && r->count<2)) return 1;
 for (unsigned i=0;i<r->count;i++) {
  if (!memchr(r->disks[i],0,64) || !r->disks[i][0]) return 1;
  for (const char *p=r->disks[i];*p;p++) if (!isalnum((unsigned char)*p)) return 1;
  for (unsigned j=0;j<i;j++) if (!strcmp(r->disks[i],r->disks[j])) return 1;
  char path[128];snprintf(path,sizeof(path),"/sys/class/block/%s/partition",r->disks[i]);
  if (!access(path,F_OK) || errno!=ENOENT) return 1;
  snprintf(path,sizeof(path),"/dev/%s",r->disks[i]);
  int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
  struct stat st;
  int valid=fd>=0 && !fstat(fd,&st) && S_ISBLK(st.st_mode);
  if(fd>=0)close(fd);
  if(!valid)return 1;
 }
 return 0;
}
static unsigned format(const struct request *request)
{
 unsigned status=existing_datastore();if(status)return status;
 pid_t child=fork();if(child<0)return 4;
 if (!child) {
  int log=open("/run/log/storage-format.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);
  if(log<0)_exit(126);
  if(dup2(log,STDOUT_FILENO)<0 || dup2(log,STDERR_FILENO)<0)_exit(126);
  close(log);
  int input=open("/dev/null",O_RDONLY|O_CLOEXEC);
  if(input<0 || dup2(input,STDIN_FILENO)<0)_exit(126);
  close(input);
  char paths[16][80];char *args[25];unsigned n=0;
  char *profile=request->profile?"raid1":"single";
  args[n++]="fractal-storage";args[n++]="-L";args[n++]="FRDATA";
  args[n++]="-d";args[n++]=profile;args[n++]="-m";args[n++]=profile;
  for(unsigned i=0;i<request->count;i++){snprintf(paths[i],sizeof(paths[i]),"/dev/%s",request->disks[i]);args[n++]=paths[i];}
  args[n]=NULL;
  /* Upstream owns filesystem detection, mounted-device checks and formatting.
   * Never pass -f. The worker isolates its globals and exit/error paths. */
  _exit(mkfs_main((int)n,args));
 }
 int result;
 while(waitpid(child,&result,0)<0){if(errno!=EINTR)return 4;}
 return WIFEXITED(result) && !WEXITSTATUS(result)?0:4;
}
static int deny_exec(void)
{
 struct sock_filter filter[]={
  BPF_STMT(BPF_LD|BPF_W|BPF_ABS,4),
  BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_X86_64,1,0),
  BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS),
  BPF_STMT(BPF_LD|BPF_W|BPF_ABS,0),
  BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K,0x40000000,3,0),
  BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_execve,2,0),
  BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_execveat,1,0),
  BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW),
  BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)
 };
 struct sock_fprog program={.len=sizeof(filter)/sizeof(filter[0]),.filter=filter};
 return prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) || prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program);
}
int main(void)
{
 if(deny_exec())return 1;
 const char *path="/run/fractal-storage.sock";
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
 int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(listener<0)return 1;
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
 struct stat old;
 if(!lstat(path,&old)){if(!S_ISSOCK(old.st_mode)||old.st_uid!=0)return 1;if(unlink(path))return 1;}
 umask(0077);
 if(bind(listener,(struct sockaddr *)&address,sizeof(address)) || chmod(path,0600) || listen(listener,4))return 1;
 const char *ready=getenv("FCR_READY_FD");
 if(ready){int fd=atoi(ready);if(write(fd,"R",1)!=1)return 1;close(fd);unsetenv("FCR_READY_FD");}
 while(!stopping){
  struct pollfd event={.fd=listener,.events=POLLIN};
  if(poll(&event,1,200)<=0)continue;
  int client=accept4(listener,NULL,NULL,SOCK_CLOEXEC);if(client<0)continue;
  struct ucred peer;socklen_t length=sizeof(peer);
  if(getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&length)||peer.uid!=0){close(client);continue;}
  struct timeval timeout={.tv_sec=5};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  struct request request;ssize_t n=recv(client,&request,sizeof(request),MSG_TRUNC);
  unsigned status=n==sizeof(request)?validate(&request):1;
  if(!status)status=request.operation==1?existing_datastore():format(&request);
  struct response reply={MAGIC,1,status};send(client,&reply,sizeof(reply),MSG_NOSIGNAL);close(client);
 }
 close(listener);unlink(path);return 0;
}
