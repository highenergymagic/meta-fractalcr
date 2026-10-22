/* SPDX-License-Identifier: MIT
 * BuildKit's upstream OCI executor does not expose NoPivot via buildkitd.
 * This fixed-program adapter supplies it only to runc create/run. Initramfs
 * cannot pivot_root; the platform has already bind-mounted its root. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    if (argc>256) return 2;
    char *args[259]; int n=0, command=0;
    args[n++]="/usr/bin/buildkit-runc";
    for (int i=1;i<argc;i++) {
        args[n++]=argv[i];
        if (command) continue;
        if (!strcmp(argv[i],"--root") || !strcmp(argv[i],"--log") || !strcmp(argv[i],"--log-format") || !strcmp(argv[i],"--rootless")) {
            if (++i==argc) return 2;
            args[n++]=argv[i]; continue;
        }
        if (argv[i][0]=='-') continue;
        command=1;
        if (!strcmp(argv[i],"run") || !strcmp(argv[i],"create")) args[n++]="--no-pivot";
    }
    args[n]=NULL; execv(args[0],args); perror("buildkit-runc"); return 127;
}
