SUMMARY = "FractalCR immutable container appliance root"
LICENSE = "MIT"
inherit image
SOURCE_DATE_EPOCH = "1792454400"
REPRODUCIBLE_TIMESTAMP_ROOTFS = "1792454400"
IMAGE_FEATURES = ""
IMAGE_FSTYPES = "cpio.gz"
# kernel.bbclass looks for INITRAMFS_IMAGE_NAME.cpio.gz without .rootfs.
IMAGE_NAME_SUFFIX = ""
IMAGE_LINGUAS = ""
PACKAGE_INSTALL = "base-files base-passwd glibc ca-certificates \
    fractal-init busybox docker-moby containerd runc iptables iptables-modules iproute2 \
    fractal-storage fractal-kata fractal-container-tools fractal-blockstore \
    os-release git openssh-ssh openssh-scp openssh-sftp chrony"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
IMAGE_ROOTFS_SIZE = "0"
IMAGE_OVERHEAD_FACTOR = "1.0"
IMAGE_ROOTFS_MAXSIZE = "1572864"
# No external kernel package: the entire root is bundled into the signed kernel.
IMAGE_INSTALL = ""

# The kernel opens PID 1's standard streams before PID 1 mounts devtmpfs.
# USE_DEVFS defaults to 1 in OE, leaving /dev empty in the generated archive.
# Supply only these bootstrap nodes; the supervisor mounts dynamic /dev later.
ROOTFS_POSTPROCESS_COMMAND += "fractal_bootstrap_devices; "
fractal_bootstrap_devices() {
    install -d ${IMAGE_ROOTFS}/dev
    test -c ${IMAGE_ROOTFS}/dev/console || mknod -m 0600 ${IMAGE_ROOTFS}/dev/console c 5 1
    test -c ${IMAGE_ROOTFS}/dev/null || mknod -m 0666 ${IMAGE_ROOTFS}/dev/null c 1 3
}

# Backend implementations may use their own internal helpers. The appliance
# command boundary uses native APIs; helper availability is not an interactive
# shell affordance. Do not strip interpreters or reject backend helper scripts.
