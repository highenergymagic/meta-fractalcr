/* SPDX-License-Identifier: MIT -- restartable platform service entry points. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <blkid/blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <net/if.h>
#include <linux/btrfs.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <termios.h>
#include "control.h"
#include <unistd.h>

static int mount_ok = 1;
static char mount_errors[2048];
static volatile sig_atomic_t stopping;
static void stop_signal(int sig) { (void)sig; stopping = 1; }
static int mounted(const char *target)
{
    /* Query mount identity directly. A failed text-table probe must never
     * hide PID 1's IPC sockets beneath a new /run tmpfs. */
    char parent[256];
    if (strlen(target) >= sizeof(parent)) { errno = ENAMETOOLONG; return -1; }
    strcpy(parent, target);
    char *slash = strrchr(parent, '/');
    if (!slash) { errno = EINVAL; return -1; }
    if (slash == parent) slash[1] = 0; else *slash = 0;
    struct statx child, ancestor;
    if (statx(AT_FDCWD, target, AT_NO_AUTOMOUNT, STATX_MNT_ID, &child) ||
        statx(AT_FDCWD, parent, AT_NO_AUTOMOUNT, STATX_MNT_ID, &ancestor)) return -1;
    if (!(child.stx_mask & ancestor.stx_mask & STATX_MNT_ID)) { errno = ENOTSUP; return -1; }
    return child.stx_mnt_id != ancestor.stx_mnt_id;
}
static void attach(const char *source, const char *target, const char *type,
                   unsigned long flags, const char *options)
{
    int status = mounted(target);
    if (status > 0 || (!status && !mount(source, target, type, flags, options))) return;
    mount_ok = 0;
    size_t used = strlen(mount_errors);
    snprintf(mount_errors + used, sizeof(mount_errors) - used, "%s: %s\n", target, strerror(errno));
}
static int root_bound;
static const char *data = "/datastore/data0";

static void directory(const char *path, mode_t mode)
{
    if (mkdir(path, mode) && errno != EEXIST) {
        mount_ok = 0;
        printf("%% mkdir %s: %s\n", path, strerror(errno));
    }
}

static void link_path(const char *target, const char *path)
{
    char current[128];
    ssize_t n = readlink(path, current, sizeof(current)-1);
    if (n >= 0) { current[n] = 0; if (!strcmp(current, target)) return; }
    if ((unlink(path) && errno != ENOENT) || symlink(target, path)) {
        mount_ok = 0; perror(path);
    }
}

static int text_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return -1;
    size_t n = strlen(text);
    int ok = write(fd, text, n) == (ssize_t)n;
    close(fd);
    return ok ? 0 : -1;
}

static void mounts(void)
{
    /* Bind only the initial root, before adding child mounts. Recursively
     * binding it afterwards duplicates proc/sys mounts in mountinfo, which
     * breaks runc's no-pivot cleanup of the host pseudo-filesystems. */
    root_bound = 1; /* PID 1 creates the root bind before its bootstrap mounts. */
    if (!root_bound) perror("bind system root");
    directory("/proc", 0555); directory("/sys", 0555); directory("/dev", 0755);
    directory("/run", 0755); directory("/tmp", 01777); directory("/var", 0755);
    attach("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    attach("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    attach("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    attach("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    attach("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    attach("tmpfs", "/var", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
    directory("/dev/pts", 0755); directory("/dev/shm", 01777);
    attach("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "mode=0620,ptmxmode=0666");
    attach("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    directory("/sys/fs/cgroup", 0755);
    attach("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    directory("/var/lib", 0755); directory("/var/log", 0755); directory("/var/tmp", 01777);
    link_path("/run", "/var/run");
    directory("/run/lock", 0755);
    link_path("/run/lock", "/var/lock");
    text_file("/run/resolv.conf", "");
    link_path("/run/resolv.conf", "/etc/resolv.conf");
    directory("/datastore", 0755); directory(data, 0700);
    directory("/run/docker", 0700);
    text_file("/proc/sys/net/ipv4/ip_forward", "1\n");
    text_file("/proc/sys/kernel/printk", "0 4 0 0\n");
    directory("/run/log", 0700);
    log_append("/run/log/platform.log", mount_errors, strlen(mount_errors));
    int netfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct ifreq loopback = {0};
    strcpy(loopback.ifr_name, "lo");
    if (netfd < 0 || ioctl(netfd, SIOCGIFFLAGS, &loopback)) mount_ok = 0;
    else {
        loopback.ifr_flags |= IFF_UP;
        if (ioctl(netfd, SIOCSIFFLAGS, &loopback)) mount_ok = 0;
    }
    if (netfd >= 0) close(netfd);
}

static int mount_data(void)
{
    int status = mounted(data);
    if (status < 0) { perror("datastore mount identity"); return -1; }
    if (status) {
        struct statfs st;
        return !statfs(data, &st) && (unsigned long)st.f_type == 0x9123683EUL ? 0 : -1;
    }
    /* Register every member before mounting: multi-device Btrfs cannot mount
     * until its devices have been presented to the kernel together. */
    int control = open("/dev/btrfs-control", O_RDWR | O_CLOEXEC);
    DIR *devices = opendir("/sys/class/block");
    if (control < 0 || !devices) {
        if (control >= 0) close(control);
        if (devices) closedir(devices);
        return -1;
    }
    struct dirent *member;
    while ((member = readdir(devices))) {
        if (member->d_name[0] == '.') continue;
        struct btrfs_ioctl_vol_args scan = {0};
        snprintf(scan.name, sizeof(scan.name), "/dev/%s", member->d_name);
        /* Non-Btrfs devices are expected and do not stop discovery. */
        ioctl(control, BTRFS_IOC_SCAN_DEV, &scan);
    }
    closedir(devices);
    close(control);
    for (int retry = 0; retry < 30; retry++) {
        DIR *d = opendir("/sys/class/block");
        struct dirent *entry;
        if (!d) return -1;
        while ((entry = readdir(d))) {
            if (entry->d_name[0] == '.' || strlen(entry->d_name) > 90) continue;
            char device[128];
            snprintf(device, sizeof(device), "/dev/%.90s", entry->d_name);
            blkid_probe probe = blkid_new_probe_from_filename(device);
            if (!probe) continue;
            const char *label = NULL, *type = NULL;
            int found = !blkid_do_safeprobe(probe) &&
                !blkid_probe_lookup_value(probe, "LABEL", &label, NULL) &&
                !blkid_probe_lookup_value(probe, "TYPE", &type, NULL) &&
                !strcmp(label, "FRDATA") && !strcmp(type, "btrfs");
            if (found && !mount(device, data, "btrfs", MS_NOSUID | MS_NODEV | MS_NOATIME, NULL)) {
                printf("Datastore data0 mounted from %s (Btrfs).\n", device);
                blkid_free_probe(probe); closedir(d);
                directory("/datastore/data0/docker", 0700);
                directory("/datastore/data0/config", 0700);
                directory("/datastore/data0/client", 0700);
                directory("/datastore/data0/client/docker", 0700);
                directory("/datastore/data0/projects", 0700);
                directory("/datastore/data0/buildkit", 0700);
                directory("/datastore/data0/selftest", 0700);
                return 0;
            }
            blkid_free_probe(probe);
        }
        closedir(d); usleep(100000);
    }
    puts("% No usable FRDATA Btrfs datastore. No disk was formatted; Docker remains stopped.");
    return -1;
}

static int freeze_root(void)
{
    /* Change only the root bind mount created before the runtime/data mounts;
     * those child mounts retain their independent writable flags. */
    if (!root_bound) return -1;
    if (mount(NULL, "/", NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL)) {
        perror("read-only root"); return -1;
    }
    int fd = open("/.fractal-readonly-test", O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) { close(fd); unlink("/.fractal-readonly-test"); return -1; }
    if (errno != EROFS) return -1;
    puts("System root is read-only; runtime and datastore mounts remain writable.");
    return 0;
}


static int container_config(void)
{
    return text_file("/run/docker-daemon.json",
        "{\"data-root\":\"/datastore/data0/docker\",\"exec-root\":\"/run/docker\","
        "\"hosts\":[\"unix:///run/docker.sock\"],\"storage-driver\":\"overlay2\","
        "\"exec-opts\":[\"native.cgroupdriver=cgroupfs\"],"
        "\"features\":{\"containerd-snapshotter\":false},\"log-driver\":\"local\"}\n");
}

extern int platform_console(void);
extern int syslog_receiver(const char *, const char *);
static int container_engine_service(void)
{
    /* The root is exempt from cgroup v2's no-internal-process rule. Dockerd
     * needs a leaf for its capability detector; containers remain siblings. */
    if (text_file("/sys/fs/cgroup/cgroup.subtree_control", "+memory +pids +cpu +io\n")) {
        perror("enable service cgroup controllers"); return 1;
    }
    if (mkdir("/sys/fs/cgroup/fractal-engine", 0755) && errno != EEXIST) {
        perror("create engine cgroup"); return 1;
    }
    if (text_file("/sys/fs/cgroup/fractal-engine/cgroup.procs", "0\n") ||
        access("/sys/fs/cgroup/fractal-engine/memory.swap.max", F_OK)) {
        perror("engine swap accounting"); return 1;
    }
    execl("/usr/bin/dockerd", "dockerd", "--config-file=/run/docker-daemon.json", (char *)NULL);
    perror("dockerd"); return 1;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
    if (!strcmp(argv[1], "console")) return platform_console();
    if (!strcmp(argv[1], "logging")) return kernel_logger();
    if (!strcmp(argv[1], "syslog")) return syslog_receiver("/dev/log","/run/log/syslog.log");
    if (!strcmp(argv[1], "container-engine")) return container_engine_service();
    signal(SIGTERM, stop_signal); signal(SIGINT, stop_signal);
    if (!strcmp(argv[1], "platform")) {
        mounts();
        if (!mount_ok || container_config() || freeze_root()) return 1;
    } else if (!strcmp(argv[1], "datastore")) {
        if (mount_data()) return 1;
    } else return 2;
    service_notify();
    while (!stopping) { struct timespec delay = {.tv_nsec=100000000}; nanosleep(&delay, NULL); }
    if (!strcmp(argv[1], "datastore")) {
        sync();
        if (umount(data)) { perror("datastore unmount"); return 1; }
    }
    return 0;
}
