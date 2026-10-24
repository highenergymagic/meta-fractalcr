/* SPDX-License-Identifier: MIT -- offline container/host networking witness. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <time.h>

static void require(int value, const char *message)
{
    if (!value) { fprintf(stderr, "PROBE FAILED: %s (%s)\n", message, strerror(errno)); exit(1); }
}

static int contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char line[1024]; int found = 0;
    while (fgets(line, sizeof(line), f)) if (strstr(line, needle)) found = 1;
    fclose(f); return found;
}

static int network(void)
{
    struct ifaddrs *interfaces=NULL;
    require(!getifaddrs(&interfaces),"enumerate container interfaces");
    for (struct ifaddrs *nic=interfaces;nic;nic=nic->ifa_next) {
        if (!nic->ifa_addr || nic->ifa_addr->sa_family!=AF_INET || (nic->ifa_flags&IFF_LOOPBACK)) continue;
        printf("CONTAINER_BRIDGE_OK %s %s\n",nic->ifa_name,
               inet_ntoa(((struct sockaddr_in *)nic->ifa_addr)->sin_addr));
        freeifaddrs(interfaces); return 1;
    }
    freeifaddrs(interfaces); return 0;
}

static void server(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(8080)};
    require(s >= 0 && !bind(s, (void *)&address, sizeof(address)) && !listen(s, 4), "listen");
    const char reply[] = "HTTP/1.0 200 OK\r\nContent-Length: 15\r\n\r\nFCR_NETWORK_OK\n";
    for (;;) {
        int client = accept(s, NULL, NULL);
        if (client < 0) continue;
        char buffer[1024];
        struct timeval timeout = {.tv_sec = 2};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (read(client, buffer, sizeof(buffer)) > 0)
            send(client, reply, sizeof(reply)-1, MSG_NOSIGNAL);
        close(client);
    }
}

static int request(const char *host, const char *port)
{
    struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*target=NULL;
    if (getaddrinfo(host,port,&hints,&target)) return 1;
    for (int retry = 0; retry < 30; retry++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct timeval timeout = {.tv_sec = 1};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (s >= 0 && !connect(s, target->ai_addr, target->ai_addrlen)) {
            const char query[] = "GET / HTTP/1.0\r\n\r\n";
            char response[1024] = {0};
            send(s, query, sizeof(query)-1, MSG_NOSIGNAL);
            ssize_t n = read(s, response, sizeof(response)-1);
            close(s);
            if (n > 0 && strstr(response, "FCR_NETWORK_OK")) { puts("CONTAINER_PORT_PUBLISH_OK"); freeaddrinfo(target);return 0; }
        } else if (s >= 0) close(s);
        usleep(100000);
    }
    freeaddrinfo(target);return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "ntp-state")) {
        FILE *f = fopen("/state/drift", "r");
        require(f != NULL, "persistent NTP drift file exists");
        double frequency, skew;
        require(fscanf(f, "%lf %lf", &frequency, &skew) == 2, "NTP drift state is readable");
        fclose(f);
        printf("NTP_PERSISTENT_STATE_OK %.3f %.3f\n", frequency, skew);
        return 0;
    }
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "factory-state")) {
        const char *path = argc == 3 ? argv[2] : "/factory-state";
        int fd = open(path, O_RDWR | O_CREAT, 0600);
        require(fd >= 0, "factory state open");
        unsigned long long count = 0;
        ssize_t n = read(fd, &count, sizeof(count));
        require(n == 0 || n == sizeof(count), "factory state read");
        count++;
        require(pwrite(fd, &count, sizeof(count), 0) == sizeof(count), "factory state write");
        require(fsync(fd) == 0, "factory state sync");
        close(fd);
        unsigned char random[32];
        fd = open("/dev/urandom", O_RDONLY);
        require(fd >= 0 && read(fd, random, sizeof(random)) == sizeof(random), "factory randomness");
        close(fd);
        printf("FACTORY_STATE=%llu\nFACTORY_RANDOM=", count);
        for (size_t i = 0; i < sizeof(random); i++) printf("%02x", random[i]);
        struct timespec now;
        require(clock_gettime(CLOCK_REALTIME, &now) == 0, "factory wall clock");
        printf("\nWALL_CLOCK=%lld.%09ld\n", (long long)now.tv_sec, now.tv_nsec);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "clock")) {
        struct timespec now;
        require(!clock_gettime(CLOCK_REALTIME, &now), "wall clock");
        printf("WALL_CLOCK=%lld.%09ld\n", (long long)now.tv_sec, now.tv_nsec);
        return 0;
    }
    if (argc==2 && !strcmp(argv[1],"identity")) {
        struct utsname identity; require(!uname(&identity),"kernel identity");
        printf("KERNEL=%s\nPID=%d\n",identity.release,getpid()); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "request")) return request("127.0.0.1","18080");
    if (argc == 4 && !strcmp(argv[1], "request")) return request(argv[2],argv[3]);
    require(getpid() == 1, "private PID namespace");
    if (argc == 2 && !strcmp(argv[1], "swarm-material")) {
        const char *paths[] = {"/run/secrets/qualification-secret", "/qualification-config"};
        const mode_t modes[] = {0400, 0444};
        for (unsigned i=0; i<2; i++) {
            struct stat st;
            require(contains(paths[i], "FractalCR"), "Swarm material delivered");
            require(!stat(paths[i], &st) && (st.st_mode & 0777) == modes[i], "Swarm material permissions");
            int fd = open(paths[i], O_WRONLY);
            require(fd < 0 && errno == EROFS, "Swarm material mounted read-only");
        }
        puts("SWARM_SECRET_CONFIG_OK"); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "swap-limit")) {
        require(contains("/sys/fs/cgroup/memory.max", "33554432"), "memory cgroup limit");
        require(contains("/sys/fs/cgroup/memory.swap.max", "16777216"), "swap cgroup limit");
        puts("CONTAINER_SWAP_LIMIT_OK"); return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "serve")) { server(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "network-wait")) {
        /* Kata's Docker integration hotplugs networking asynchronously after
         * Start. Keep a separate bounded readiness check from the immediate
         * network witness so this timing limitation remains visible. */
        for (int i=0;i<100;i++) { if (network()) return 0; usleep(100000); }
        errno=ENODEV; require(0,"no non-loopback IPv4 interface after 10 seconds");
    }
    if (argc == 2 && !strcmp(argv[1], "network")) {
        int ready=network(); errno=ENODEV;
        require(ready,"container has a non-loopback IPv4 interface"); return 0;
    }
    require(contains("/proc/self/status", "CapEff:\t0000000000000000"), "capabilities dropped");
    require(contains("/proc/self/status", "Seccomp:\t2"), "seccomp filter active");
    require(contains("/proc/self/status", "NoNewPrivs:\t1"), "no-new-privileges");
    require(contains("/sys/fs/cgroup/memory.max", "33554432"), "memory cgroup limit");
    require(contains("/sys/fs/cgroup/pids.max", "32"), "PID cgroup limit");
    errno = 0;
    int fd = open("/must-not-write", O_WRONLY | O_CREAT, 0600);
    require(fd < 0 && errno == EROFS, "container root read-only");
    FILE *f = fopen("/state/counter", "r");
    unsigned count = 0;
    if (f) { require(fscanf(f, "%u", &count) == 1, "counter parse"); fclose(f); }
    f = fopen("/state/counter.tmp", "w");
    require(f != NULL, "persistent bind mount writable");
    fprintf(f, "%u\n", ++count); fflush(f);
    require(!fsync(fileno(f)), "counter fsync"); fclose(f);
    require(!rename("/state/counter.tmp", "/state/counter"), "counter rename");
    fd = open("/state", O_RDONLY | O_DIRECTORY);
    require(fd >= 0 && !fsync(fd), "directory fsync"); close(fd);
    printf("CONTAINER_ISOLATION_OK\nPERSISTENT_COUNTER=%u\n", count);
    return 0;
}
