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

static int mount_ok = 1, configured;
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

static int run(char *const args[], unsigned timeout)
{
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        int null = open("/dev/null", O_RDONLY);
        if (null < 0 || dup2(null, 0) < 0) _exit(126);
        close(null);
        execvp(args[0], args); perror(args[0]); _exit(127);
    }
    long long end = monotonic_ms() + timeout * 1000LL;
    int status;
    while (monotonic_ms() < end) {
        if (waitpid(child, &status, WNOHANG) == child)
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        usleep(100000);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    puts("% Child operation timed out.");
    return -1;
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
    char *loopback[] = {"ip", "link", "set", "lo", "up", NULL};
    run(loopback, 5);
}

static int mount_data(void)
{
    int status = mounted(data);
    if (status < 0) { perror("datastore mount identity"); return -1; }
    if (status) {
        struct statfs st;
        return !statfs(data, &st) && (unsigned long)st.f_type == 0x9123683EUL ? 0 : -1;
    }
    char *scan[] = {"btrfs", "device", "scan", NULL};
    run(scan, 15);
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

static const char *string_value(json_object *object, const char *key)
{
    json_object *v;
    return json_object_object_get_ex(object, key, &v) && json_object_is_type(v, json_type_string) ?
           json_object_get_string(v) : NULL;
}

static int address_ok(const char *text)
{
    struct in_addr a;
    return text && inet_pton(AF_INET, text, &a) == 1;
}

static int configure(int network_only)
{
    if (!network_only) {
        char *prepare[] = {"/usr/libexec/fractal-management", "prepare", NULL};
        if (run(prepare, 15)) return -1;
    }
    const char *path = "/run/startup-config.json";
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT && !network_only) {
        if (sethostname("ctr", 3)) { perror("sethostname"); return -1; }
        puts("No startup-config; using hostname ctr with no external IP configuration.");
        return text_file("/run/startup-config.json", "{\"version\":1,\"hostname\":\"ctr\"}\n");
    }
    struct stat st;
    char buf[65537];
    ssize_t n = -1;
    if (fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 65536)
        n = read(fd, buf, (size_t)st.st_size);
    if (fd >= 0) close(fd);
    if (n <= 0 || n != st.st_size) { puts("% Cannot read startup-config."); return -1; }
    buf[n] = 0;
    json_tokener *tokener = json_tokener_new();
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *root = json_tokener_parse_ex(tokener, buf, (int)n);
    size_t end = json_tokener_get_parse_end(tokener);
    int valid = json_tokener_get_error(tokener) == json_tokener_success;
    while (end < (size_t)n && isspace((unsigned char)buf[end])) end++;
    json_tokener_free(tokener);
    json_object *version = NULL, *net = NULL;
    const char *hostname = root ? string_value(root, "hostname") : NULL;
    if (!valid || end != (size_t)n || !root || !json_object_is_type(root, json_type_object) ||
        !json_object_object_get_ex(root, "version", &version) ||
        !json_object_is_type(version, json_type_int) || json_object_get_int(version) != 1 ||
        !hostname || !*hostname || strlen(hostname) > 63) goto invalid;
    for (const char *c = hostname; *c; c++)
        if (!isalnum((unsigned char)*c) && *c != '-') goto invalid;
    if (json_object_object_get_ex(root, "network", &net)) {
        if (!json_object_is_type(net, json_type_object)) goto invalid;
        const char *nic = string_value(net, "interface"), *address = string_value(net, "address");
        const char *gateway = string_value(net, "gateway"), *dns = string_value(net, "dns");
        char ip[64], cidr[64], *slash, *tail;
        if (!nic || !*nic || strlen(nic) >= IF_NAMESIZE || !if_nametoindex(nic) ||
            !address || strlen(address) >= sizeof(ip)) goto invalid;
        strcpy(ip, address); slash = strchr(ip, '/');
        if (!slash) goto invalid;
        *slash++ = 0;
        long prefix = strtol(slash, &tail, 10);
        if (!*slash || *tail || prefix < 1 || prefix > 32 || !address_ok(ip) ||
            (gateway && !address_ok(gateway)) || (dns && !address_ok(dns))) goto invalid;
        if (network_only) {
        snprintf(cidr, sizeof(cidr), "%s", address);
        char *up[] = {"ip", "link", "set", (char *)nic, "up", NULL};
        char *addr[] = {"ip", "address", "replace", cidr, "dev", (char *)nic, NULL};
        if (run(up, 5) || run(addr, 5)) goto invalid;
        if (gateway) {
            char *route[] = {"ip", "route", "replace", "default", "via", (char *)gateway, "dev", (char *)nic, NULL};
            if (run(route, 5)) goto invalid;
        }
        if (dns) {
            char resolv[64]; snprintf(resolv, sizeof(resolv), "nameserver %s\n", dns);
            if (text_file("/run/resolv.conf", resolv)) goto invalid;
        }
        printf("Configured %s on %s from JSON startup-config.\n", address, nic);
        }
    }
    if (!network_only && (sethostname(hostname, strlen(hostname)) || text_file("/run/startup-config.json", buf))) goto invalid;
    printf("Startup configuration loaded: hostname %s.\n", hostname);
    json_object_put(root);
    return 0;
invalid:
    json_object_put(root);
    puts("% Invalid/unsupported startup-config or network failure; Docker remains stopped.");
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

static void network_stop(void)
{
    json_object *root = json_object_from_file("/run/startup-config.json"), *net = NULL;
    if (root && json_object_object_get_ex(root, "network", &net)) {
        const char *nic = string_value(net, "interface"), *address = string_value(net, "address");
        const char *gateway = string_value(net, "gateway");
        if (nic && address) {
            if (gateway) {
                char *route[] = {"ip", "route", "del", "default", "via", (char *)gateway, "dev", (char *)nic, NULL};
                run(route, 3);
            }
            char *addr[] = {"ip", "address", "del", (char *)address, "dev", (char *)nic, NULL};
            run(addr, 3);
        }
    }
    json_object_put(root); text_file("/run/resolv.conf", "");
}
extern int platform_console(void);
extern int syslog_receiver(const char *,const char *);
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
    execlp("dockerd", "dockerd", "--config-file=/run/docker-daemon.json", (char *)NULL);
    perror("dockerd"); return 1;
}
static int buildkit_service(void)
{
    pid_t child=fork();
    if (child<0) return 1;
    if (!child) {
        execlp("buildkitd","buildkitd","--root=/datastore/data0/buildkit",
            "--addr=unix:///run/buildkit/buildkitd.sock","--containerd-worker=false",
            "--oci-worker=true","--oci-worker-snapshotter=overlayfs",
            "--oci-worker-net=bridge","--oci-worker-binary=/usr/libexec/fractal-buildkit-runc",(char *)NULL);
        _exit(127);
    }
    int status=0, ready=0;
    for (int attempt=0;attempt<30 && !stopping;attempt++) {
        if (waitpid(child,&status,WNOHANG)==child) return 1;
        char *probe[]={"buildctl","--addr=unix:///run/buildkit/buildkitd.sock","--timeout=2","debug","workers",NULL};
        if (!run(probe,3)) { ready=1; break; }
        sleep(1);
    }
    if (!ready) { kill(child,SIGTERM); return 1; }
    service_notify();
    while (waitpid(child,&status,0)<0 && errno==EINTR) {}
    return stopping ? 0 : 1;
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
    if (!strcmp(argv[1],"buildkit")) return buildkit_service();
    if (!strcmp(argv[1], "platform")) {
        mounts();
        if (!mount_ok || container_config() || freeze_root()) return 1;
    } else if (!strcmp(argv[1], "datastore")) {
        if (mount_data()) return 1;
    } else if (!strcmp(argv[1], "configuration")) {
        if (configure(0)) return 1;
        configured = 1;
    } else if (!strcmp(argv[1], "networking")) {
        if (configure(1)) { network_stop(); return 1; }
    } else return 2;
    service_notify();
    while (!stopping) { struct timespec delay = {.tv_nsec=100000000}; nanosleep(&delay, NULL); }
    if (!strcmp(argv[1], "networking")) network_stop();
    if (!strcmp(argv[1], "datastore")) {
        sync();
        if (umount(data)) { perror("datastore unmount"); return 1; }
    }
    if (configured) unlink("/run/startup-config.json");
    return 0;
}
